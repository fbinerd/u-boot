// SPDX-License-Identifier: GPL-2.0+

#include <init.h>
#include <cyclic.h>
#include <env.h>
#include <net.h>
#include <vsprintf.h>
#include <asm/global_data.h>
#include <asm/addrspace.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <time.h>
#include <mach/rtl8231.h>
#include <mach/rtl838x-eth.h>
#include <mach/rtl838x-led.h>

DECLARE_GLOBAL_DATA_PTR;

/* Address observed in the original handoff's uncached argv table. */
#define RTL8380M_DEMO_BOOT_PARAMS	0x83dcff10
#define RTL8380M_DEMO_SPI_XIP_BASE	0xb4000000
#define RTL8380M_DEMO_SPI_SIZE		0x01000000
/*
 * Which image to boot: an OpenWrt uImage at $openwrt_addr if its legacy
 * magic is there, otherwise the original VxWorks image at $vxworks_addr.
 * The simple command parser has no "||", so bootcmd cannot express the
 * choice and it is decided here, on every boot.  boot_auto, openwrt_addr and
 * vxworks_addr live only in the environment (defaults in sg1002_mr.env):
 * there are no fallback values in C, and a missing or malformed variable is
 * reported on the console and disables autoboot, including when saveenv
 * preserved an earlier bootcmd.  boot_auto=0 freezes a manually set bootcmd.
 *
 * The addresses are validated before the magic is read: the load is a plain
 * 32-bit access, so an unaligned address raises an address error and one
 * outside the SPI XIP window can reach an unmapped or unrelated device.  This
 * runs before the autoboot countdown, so a typo saved with saveenv could
 * otherwise hang every later boot with no way to reach the prompt.
 */
#define RTL8380M_DEMO_LEGACY_IMG_MAGIC	0x27051956
#define RTL8380M_DEMO_KSEG0_BASE	0x80000000UL
#define RTL8380M_DEMO_KSEG1_END		0xbfffffffUL
#define RTL8380M_DEMO_RECOVERY_ETHADDR	"02:83:80:10:02:01"
#define RTL8380M_DEMO_WDT_BASE		CKSEG1ADDR(0x18003150)
#define RTL8380M_DEMO_WDT_INTR		((void __iomem *)(RTL8380M_DEMO_WDT_BASE + 0x04))
#define RTL8380M_DEMO_WDT_CTRL		((void __iomem *)(RTL8380M_DEMO_WDT_BASE + 0x08))
#define RTL8380M_DEMO_WDT_INTR_CLEAR	0xc0000000
#define RTL8380M_DEMO_WDT_CTRL_DISABLED	0x00000001

/* Legacy argument string observed in the preserved factory environment. */
#define RTL8380M_DEMO_FACTORY_BOOTARGS \
	"generic(0,0)host:vxWorks h=90.0.0.3 e=90.0.0.8 u=bdcom pw=switch"

/*
 * Lamp test of the front panel at the start of board_late_init(): the
 * SYS LED on, every amber LED, then every green LED, then the port LEDs
 * off.  SYS is left on afterwards: mainline U-Boot's own boot-LED
 * convention (CONFIG_LED_BOOT, not used on this board) blinks a boot LED
 * while board_init_r() runs and holds it solid once initialised, never
 * going dark before a working handoff; this board's own SYS LED used to
 * turn off right here and stay off through the whole autoboot countdown,
 * any TFTP and the kernel decompress, indistinguishable from a hung board.
 * Leaving it on is the cheapest way to stop that: no blink phase, since the
 * RTL8231's MDIO round trip was never measured fast enough for one and the
 * pin is otherwise only ever touched twice per boot (on, then never off).
 * Linux's own default-state="on" for this LED continues the same state
 * with no visible transition.
 * Whether it runs is decided by the environment (led_test), like boot_auto.
 *
 * Wiring, from the working Linux devicetree (measured on this board):
 *  - the 20 port LEDs are not on an RTL8231: the SoC's own LED engine drives
 *    them, two channels per port (0 = amber, 1 = green).  The engine's port
 *    numbers are 16..24 for lan1..lan9 and 26 for lan10 (lan9/lan10 are the
 *    SFP cages, MAC 24 and 26); the enable mask is the engine's own reset
 *    value.  Linux's port-LED driver programs the same registers.
 *  - the SYS LED is GPIO27 of the RTL8231 at address 0 of the auxiliary MDIO
 *    bus, active low.  The second RTL8231 (address 1) does not answer and,
 *    per the board owner, belongs to the SFP cages.
 *
 * The engine is left in software control with everything off, which is the
 * state Linux's driver sets up anyway; the vendor image (boot_auto without an
 * OpenWrt image) programs the engine itself, but that was not tested here.
 */
#define SG1002_LED_PORT_ENABLE_MASK	0x05ffffff
#define SG1002_LED_AMBER		0
#define SG1002_LED_GREEN		1
#define SG1002_LED_STEP_MS		700
#define SG1002_SYS_LED_EXPANDER		0
#define SG1002_SYS_LED_PIN		27
#define SG1002_LED_TICK_US		20000
#define SG1002_LED_POLL_US		200000
#define SG1002_LED_PULSE_US		40000

static const u8 sg1002_led_port[] = { 16, 17, 18, 19, 20, 21, 22, 23, 24, 26 };

struct sg1002_port_led_state {
	u32 rx, tx;
	u64 pulse_end;
	bool have_counters;
	bool link;
	bool green;
	bool pulse;
};

static struct sg1002_port_led_state sg1002_port_led_state[10];
static struct cyclic_info sg1002_led_cyclic;
static struct udevice *sg1002_led_eth;
static u64 sg1002_led_next_poll;

static unsigned int sg1002_led_mac(unsigned int index)
{
	return index < 8 ? index + 8 : sg1002_led_port[index];
}

static void sg1002_led_apply(unsigned int index)
{
	const struct sg1002_port_led_state *state = &sg1002_port_led_state[index];
	bool on = state->link && !state->pulse;

	rtl838x_port_led_set(sg1002_led_port[index], SG1002_LED_AMBER,
			     on && !state->green);
	rtl838x_port_led_set(sg1002_led_port[index], SG1002_LED_GREEN,
			     on && state->green);
}

static void sg1002_leds_set(unsigned int channel, bool on)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sg1002_led_port); i++)
		rtl838x_port_led_set(sg1002_led_port[i], channel, on);
}

static void sg1002_led_test(void)
{
	int test = env_get_yesno("led_test");
	int sys;

	if (test < 0) {
		printf("LED test: led_test is not set in the environment, skipped\n");
		return;
	}
	if (!test)
		return;

	printf("LED test: SYS, amber, green (SYS stays on)\n");
	rtl838x_port_led_init(SG1002_LED_PORT_ENABLE_MASK);

	sys = rtl8231_init(SG1002_SYS_LED_EXPANDER);
	if (!sys)
		sys = rtl8231_gpio_set_output(SG1002_SYS_LED_EXPANDER,
					      SG1002_SYS_LED_PIN, 0);
	if (sys)
		printf("LED test: SYS LED (RTL8231 #%d) unavailable, error %d\n",
		       SG1002_SYS_LED_EXPANDER, sys);

	sg1002_leds_set(SG1002_LED_AMBER, true);
	mdelay(SG1002_LED_STEP_MS);
	sg1002_leds_set(SG1002_LED_AMBER, false);
	sg1002_leds_set(SG1002_LED_GREEN, true);
	mdelay(SG1002_LED_STEP_MS);
	sg1002_leds_set(SG1002_LED_GREEN, false);
}

static void sg1002_led_service(struct cyclic_info *cyclic)
{
	struct rtl838x_link_snapshot snapshot;
	u64 now = get_timer_us(0);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sg1002_led_port); i++) {
		struct sg1002_port_led_state *state = &sg1002_port_led_state[i];

		if (state->pulse && now >= state->pulse_end) {
			state->pulse = false;
			sg1002_led_apply(i);
		}
	}
	if (now < sg1002_led_next_poll)
		return;
	sg1002_led_next_poll = now + SG1002_LED_POLL_US;
	if (rtl838x_eth_link_snapshot_now(sg1002_led_eth, &snapshot))
		return;

	for (i = 0; i < ARRAY_SIZE(sg1002_led_port); i++) {
		struct sg1002_port_led_state *state = &sg1002_port_led_state[i];
		unsigned int mac = sg1002_led_mac(i);
		bool link = snapshot.links & BIT(mac);
		bool green = link && (snapshot.gigabit & BIT(mac));
		u32 rx, tx;

		if (state->link != link || state->green != green) {
			state->link = link;
			state->green = green;
			state->pulse = false;
			state->have_counters = false;
		}
		if (!link) {
			sg1002_led_apply(i);
			continue;
		}
		if (!rtl838x_eth_port_octets(sg1002_led_eth, mac, &rx, &tx)) {
			if (state->have_counters && (rx != state->rx || tx != state->tx)) {
				state->pulse = true;
				state->pulse_end = now + SG1002_LED_PULSE_US;
			}
			state->rx = rx;
			state->tx = tx;
			state->have_counters = true;
		}
		sg1002_led_apply(i);
	}
}

static void sg1002_led_link_status(void)
{
	struct rtl838x_link_snapshot snapshot;
	struct udevice *eth;
	unsigned int i;
	int enabled = env_get_yesno("led_link_status");

	if (enabled < 0) {
		printf("LED link: led_link_status is not set in the environment, skipped\n");
		return;
	}
	if (!enabled)
		return;
	eth = eth_get_dev();
	if (!eth || rtl838x_eth_link_snapshot(eth, &snapshot)) {
		printf("LED link: Ethernet status unavailable, skipped\n");
		return;
	}

	rtl838x_port_led_init(SG1002_LED_PORT_ENABLE_MASK);
	for (i = 0; i < ARRAY_SIZE(sg1002_led_port); i++) {
		/* Copper MAC 8..15; the two SFP MACs equal their LED ports. */
		unsigned int mac = sg1002_led_mac(i);
		struct sg1002_port_led_state *state = &sg1002_port_led_state[i];
		bool link = snapshot.links & BIT(mac);
		bool green = link && (snapshot.gigabit & BIT(mac));
		bool amber = link && !green;
		u32 rx, tx;

		state->link = link;
		state->green = green;
		state->pulse = false;
		state->have_counters = link &&
			!rtl838x_eth_port_octets(eth, mac, &rx, &tx);
		if (state->have_counters) {
			state->rx = rx;
			state->tx = tx;
		}
		sg1002_led_apply(i);
		printf("LED link: lan%u %s\n", i + 1,
		       green ? "green 1000" :
		       amber && (snapshot.unknown_speed & BIT(mac)) ?
		       "amber (speed unknown)" : amber ? "amber 10/100" : "off");
	}
	sg1002_led_eth = eth;
	sg1002_led_next_poll = get_timer_us(0) + SG1002_LED_POLL_US;
	cyclic_register(&sg1002_led_cyclic, sg1002_led_service,
			SG1002_LED_TICK_US, "sg1002-port-leds");
}

int checkboard(void)
{
	/*
	 * CPU/LXB/MEM clocks are the public, documented RTL838x-family
	 * plan (same numbers this chip family's own historical U-Boot
	 * ports report), not a runtime clock-tree read -- this SoC family
	 * has no clock-tree query registers U-Boot can probe. LXB matches
	 * this board's own verified CONFIG_SYS_MIPS_TIMER_FREQ=200000000
	 * (the timer/delay loops that already work depend on this being
	 * correct).
	 */
	printf("Board: Intelbras SG1002 MR L2+  CPU:500MHz LXB:200MHz MEM:300MHz\n");
	printf("SPI-F: 1x%d MB\n", RTL8380M_DEMO_SPI_SIZE / (1024 * 1024));
	return 0;
}

int board_early_init_f(void)
{
	return 0;
}

int board_postclk_init(void)
{
	return 0;
}

/* Read an image address from the environment, with no compiled-in default. */
static bool sg1002_env_image_addr(const char *name, ulong *addr)
{
	const char *val = env_get(name);
	char *end;
	ulong a;
	ulong phys;

	if (!val || !*val) {
		printf("Boot: %s is not set in the environment\n", name);
		return false;
	}
	a = hextoul(val, &end);
	phys = a & 0x1fffffffUL;
	if (*end || (a & 3) || a < RTL8380M_DEMO_KSEG0_BASE ||
	    a > RTL8380M_DEMO_KSEG1_END - 3 ||
	    phys < (RTL8380M_DEMO_SPI_XIP_BASE & 0x1fffffffUL) ||
	    phys > (RTL8380M_DEMO_SPI_XIP_BASE & 0x1fffffffUL) +
		   RTL8380M_DEMO_SPI_SIZE - sizeof(u32)) {
		printf("Boot: %s=%s is not a valid 4-byte aligned SPI XIP address\n",
		       name, val);
		return false;
	}
	*addr = a;
	return true;
}

int board_late_init(void)
{
	/* Stop any reset countdown inherited from the factory bootstrap. */
	writel(RTL8380M_DEMO_WDT_INTR_CLEAR, RTL8380M_DEMO_WDT_INTR);
	writel(RTL8380M_DEMO_WDT_CTRL_DISABLED, RTL8380M_DEMO_WDT_CTRL);

	sg1002_led_test();

	/* Values consumed by the standard MIPS legacy bootm ABI. */
	gd->bd->bi_boot_params = RTL8380M_DEMO_BOOT_PARAMS;
	gd->bd->bi_flashstart = RTL8380M_DEMO_SPI_XIP_BASE;
	gd->bd->bi_flashsize = RTL8380M_DEMO_SPI_SIZE;

	/* Seed compatibility arguments if the SPI environment lacks them. */
	if (!env_get("bootargs"))
		env_set("bootargs", RTL8380M_DEMO_FACTORY_BOOTARGS);
	int boot_auto = env_get_yesno("boot_auto");
	ulong openwrt_addr, vxworks_addr;

	if (boot_auto < 0) {
		printf("Boot: boot_auto is not set in the environment, bootcmd left as is\n");
	} else if (boot_auto == 1) {
		if (sg1002_env_image_addr("openwrt_addr", &openwrt_addr) &&
		    sg1002_env_image_addr("vxworks_addr", &vxworks_addr)) {
			u32 magic = *(const u32 *)openwrt_addr;
			bool openwrt = magic == RTL8380M_DEMO_LEGACY_IMG_MAGIC;
			ulong target = openwrt ? openwrt_addr : vxworks_addr;
			char bootcmd_buf[32];

			if (openwrt)
				printf("Boot: uImage at openwrt_addr 0x%lx, bootm 0x%lx\n",
				       openwrt_addr, target);
			else
				printf("Boot: no uImage at openwrt_addr 0x%lx (found %08x), bootm vxworks_addr 0x%lx\n",
				       openwrt_addr, magic, target);
			snprintf(bootcmd_buf, sizeof(bootcmd_buf), "bootm 0x%lx",
				 target);
			env_set("bootcmd", bootcmd_buf);
		} else {
			env_set("bootcmd", NULL);
			printf("Boot: invalid image address, autoboot disabled\n");
		}
	}
	/* boot_auto=0 (or any non-yes value): keep the saved bootcmd */
	/* Use a locally administered recovery address if no MAC was saved;
	 * an operator may override ethaddr in the SPI environment. */
	if (!env_get("ethaddr"))
		env_set("ethaddr", RTL8380M_DEMO_RECOVERY_ETHADDR);

	/*
	 * Bring the switch/copper PHYs up here instead of at the first network
	 * command; the return value is ignored because an absent cable is not a
	 * boot failure (eth_init() logs its own PHY link diagnostics).
	 */
	eth_init();
	sg1002_led_link_status();

	return 0;
}
