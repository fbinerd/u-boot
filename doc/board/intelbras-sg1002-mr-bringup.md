# Intelbras SG1002 MR — diário de bring-up GPL

*(English note for reviewers: this is a long, chronological working diary
kept in Portuguese, the language it was written in day to day. It is
optional reading. `doc/board/intelbras/sg1002-mr.rst` and
`sg1002-mr-findings.md` in that directory are the English, review-facing
documents; they summarize what this diary proves and point back to
specific entries here only where the detail matters. Copyright (C) 2026
Fabiano Tassotti <fabianotassotti@gmail.com>, GPL-2.0+, like the rest of
this tree.)*

Este arquivo registra resultados **observados no equipamento**, e não apenas
hipóteses. O objetivo é impedir o retorno a tentativas já refutadas.

## Hardware confirmado

- SoC: Realtek RTL8380, MIPS 4KEc.
- SPI: Winbond W25Q128.V, 16 MiB.
- DDR: Winbond W631GG8NB-12, 1 Gbit = 128 MiB DDR3.
- Stage-1 GPL ocupa `0x000000..0x007fff`.
- Stage-2 GPL ocupa `0x050000..0x082fff` e é carregado em `0x83f01000`.

## Regras de teste físico

- Antes de cada gravação, detectar o CH341A (`lsusb -d 1a86:5512`).
- Se detectado, escrever somente as duas regiões acima e verificar ambas.
- Nunca substituir a SPI inteira para um teste de Stage-1/Stage-2.
- O preloader e o U-Boot devem permanecer integralmente GPL; não usar
  trampolins nem execução de binário proprietário como solução final.

## Resultados cronológicos

| Revisão | Resultado em TTL | Conclusão |
| --- | --- | --- |
| `a8fca436` | DDR reconhecida como 128 MiB | Geometria correta para W631GG8NB-12; a tentativa anterior de 64 MiB foi superada. |
| V103 | `DE` | O perfil normal do Stage-1 não completou o caminho DDR/handoff. |
| V104 | `... !JUMP! LFPDDUVWSX` | O Stage-1 GPL copia e entra no Stage-2 GPL. A falha já é pós-salto. |
| `2915db7c` / V105 | `...XYCDE` | Console adiado, `checkcpu()` e `dram_init()` completam. DDR inicial é utilizável para esta fase. |
| `27faba4d` / V106 | `...XYCDE` sem `0` | A falha é em `setup_dest_addr()`, antes de qualquer reserva/relocação. Não investigar novamente UART, `checkcpu()` ou `dram_init()` como causa imediata. |
| `09257c9e` / V107 | `LFPDD` sem `U` | A base KSEG0 não é executada neste ponto (ela ocorre depois de `E`), portanto não explica a falha. O Stage-2 chegou corrompido ou com instruções antigas em cache. |
| `43da3d25` / V108 | ainda falha antes do Stage-2 estável | Invalidação de D/I-cache não recuperou o carregamento; cache não é a causa principal. |
| `eeae6cd3` / V109 | `!COPY! Z` | A verificação integral dos `0x33000` bytes falhou antes do salto; o Stage-2 é corrompido durante a cópia para DDR. |
| `535ba1bc` / V110 | `S2:00000000`, depois `!COPY! Z` | A janela baixa de DDR falha já no teste de uma palavra; não é um problema exclusivo de relocação do U-Boot. |
| `5a9f37cf` / V111 | `M:abcdefghijkl` | Todas as 12 janelas de 202 KiB entre 64 KiB e 7 MiB falharam na cópia+comparação integral. A hipótese de endereço de carga nessas janelas foi descartada. |
| `07a47bf4` / V112 | `F R0:12347678`, `R1:76ff9abc`, `X0:e888a887`, `LF:ffffffff`, `S2:013fbfa4` | A tabela final da fábrica aplicada isoladamente degrada a RAM antes da cópia. Ela é o resultado de treinamento, não uma receita autossuficiente; não repetir essa aplicação direta. |

## Próxima correção

O próximo trabalho é portar para o Stage-1 apenas o algoritmo de treinamento
publicado em fonte GPL do RTL838x, para o perfil Winbond x8 equivalente. O
perfil local `W631GG8KB-15_838x_DEMO_300MHZ` coincide com os valores observados
de `DCR`, `DTR0..2`, `DCDR`, ZQ e a tabela PHY; ele ainda revela os atrasos de
entrada que faltavam (`TX=17`, `CLKM=13`, `CLKM90=10`). Não reutilizar o
preloader binário proprietário nem sua execução.

Antes do port, V113 (`7390b3bb`) mede o efeito do pulso de sincronização PHY
em `0xb8001038`: `N:` é o erro do padrão LFSR antes do pulso e `C:` é a
releitura do mesmo padrão depois dele. O Stage-1 para no resultado; não toca
no Stage-2. A imagem foi gravada somente em `0x000000..0x007fff` e verificada
pelo CH341A em `logs/spi-write-20260920-175159.log`.

Resultado físico V113: `FN:ffffffff` e `C:ffffffff`. A tabela final aplicada
diretamente já corrompe a RAM antes da sincronização; o pulso não é a causa.
V114 repete a medição usando o baseline V20, que mantinha as palavras simples
de RAM corretas.

Resultado físico V114: `N:00007f10`, `C:00007f10`. O pulso de sincronização
não muda a máscara; o próximo teste usa somente a varredura GPL por lane e
retém os mínimos medidos para W05, W00, W02, W17 e W18.

Resultado físico V115: W17 encontrou `w=0,r=24` e W18 `w=0,r=18`, ambos com
zero erro isolado. W05/W00/W02 mantiveram erro mínimo não nulo. Aplicar os
cinco mínimos resultou em `LF:00007f18`, pior que o baseline `00007f10`.
V116 testa os dois mínimos de zero erro separadamente, sempre reiniciando o
baseline antes de cada um.

Resultado físico V116: a imagem ainda executou as cinco varreduras `Wxx`
antes de `A:`/`B:` porque a guarda da varredura não excluía V116. Por isso,
`A:00001f1a` e `B:00001f18` não são medidas isoladas e não podem decidir a
calibração. V117 corrige somente a guarda: mede os mesmos W17 e W18 diretamente
a partir do baseline, sem varredura nem escrita intermediária, e para em
seguida.

Resultado físico V117: `A:00005f1a`, `B:00005f18`. Ambos os ajustes ainda
produziram máscara elevada, mas V117 não imprimiu o baseline daquela mesma
partida; não se deve compará-los quantitativamente ao `00007f10` de V114.
V118 acrescenta `N:` antes de A/B e recarrega o baseline antes de cada uma das
três amostras. A imagem continua sem varredura, sem Stage-2 e para após B.

Resultado físico V118: `N:00001f18`, `A:00001f1a`, `B:00001f18`. W17 piora
o baseline da própria partida; W18 é neutro. A auditoria posterior mostrou que
V118 não herdava `V20_FORCE_BOOT` e `V21_HANDOFF_TRACE`, presentes em V114.
V119 repete N/A/B com esses dois flags para reproduzir exatamente a base V114;
continua sem varredura e interrompe antes do Stage-2.

Resultado físico V119: `N:04001f18`, `A:00001f1a`, `B:04001f18`. A presença
de `V20/V21` não restaurou o valor de V114. A causa é mensurável no código:
V114 usa `ddr_error_sample 0`, sem acionar `DPHY_CAL_CTRL`; V119 usava a forma
padrão, que aciona essa sincronização antes de cada leitura. V120 mantém o
commit observado de cada candidato, mas usa a mesma amostra sem ressincronizar
N/A/B. Assim separa o efeito do candidato do efeito da sincronização.

Resultado físico V120: `N:00001f18`, `A:00001f1a`, `B:00001f18`. O resultado
repete V118, portanto é independente da ressincronização: W17 (`w=0,r=24`)
é rejeitado por piorar a máscara e W18 (`w=0,r=18`) é neutro. Não serão feitas
novas imagens alterando esses dois pontos. A próxima implementação substitui a
heurística de mínimo pontual pelo algoritmo GPL `DDR_Calibration`: matriz por
DQ de 16×16 (passos de 2), seleção da maior janela contígua e tap central.

Antes do porte integral, V121 mede uma comparação necessária para escolher a
semente do algoritmo: `N` é o baseline calibrado atual e `S` é o State3 já
validado em experimentos anteriores. As duas amostras usam KSEG1 e não acionam
nova sincronização PHY; a imagem para antes do Stage-2.

Resultado físico V121: `N:00001f18`, `S:0000ff18`. O State3 fecha bits
adicionais e é rejeitado como semente. O porte GPL partirá do baseline
calibrado atual; o algoritmo ainda conservará a regra GPL de selecionar centro
de janelas contíguas, mas não herdará a tabela estática do perfil W631GG8KB
como decisão final para a W631GG8NB da placa.

## V122 — varredura integral GPL em SRAM

V122 substitui as buscas pontuais por uma implementação nova, auditável e
100% GPL do algoritmo publicado no SDK RTL838x: `sg1002_mr_gpl_cali.c` porta
o padrão de 32 palavras, a matriz `DQ_RW_Array[32][32]`, a varredura 16×16
(taps pares), a conversão física x8 e a escolha de maior janela contígua de
`plr_memctl_cali_dram.c`. A fórmula final de tap é a de `plr_plat_dep.c`.

O código é residente em SRAM (`0xbf0033bc..0xbf00399f`), usa uma pilha de 4
KiB no topo da SRAM e não possui símbolos externos. A imagem mantém a região
de escrita limitada a `0x000000..0x007fff`; não escreve Stage-2 nem executa
salto para ele. Após o prefixo usual de inicialização, o único resultado novo
esperado é `G:XXXXXXXX`, em que cada bit indica uma lane cuja janela GPL foi
menor ou igual a sete taps; `80000000` indica timeout de transação MMIO.

A semente é deliberadamente o baseline calibrado que ainda preserva RAM
simples na placa, e não a tabela estática W631GG8KB: a RAM física é
W631GG8NB-12 e V112 demonstrou que a tabela final de fábrica não é uma receita
autônoma. O resultado de V122 decidirá a próxima imagem (verificação RAM da
configuração selecionada), sem saltar diretamente para o U-Boot.

Resultado físico V122: `G:00000000`. A varredura integral completou e todas
as lanes válidas x8 apresentaram simultaneamente janelas de escrita e leitura
maiores que sete taps. Este é o primeiro resultado que valida a seleção GPL de
tap por janela no hardware, em vez de um mínimo isolado. V123 repete o treino,
mantém os taps escolhidos e mede `R0`, `R1`, `X0` e `LF`; ela para antes de ler
ou copiar o Stage-2, separando a validação de DDR da próxima etapa de boot.

Resultado físico V123: `G:00000000`, `R0:12345678`, `R1:9abcdef0`,
`X0:00000000`, `LF:00007f18`. O treino GPL preserva as leituras simples da
DDR, mas o LFSR ainda acusa os bits `7f18` (não é seguro inferir boot só dele).
V124 trata a cópia íntegra do payload GPL de 0x050000 como o critério prático:
força a etapa diagnóstica, compara todos os `0x33000` bytes e para em
`!FULL_OK!`, antes do latch de DDR ou do salto.

Resultado físico V124: `G:00000000`, RAM simples correta e `LF:00007f18`,
mas a comparação integral após `!COPY!` terminou em `Z`. Assim, o resultado
GPL em `0xa0000000` não demonstra integridade no endereço onde o Stage-2 é
realmente carregado (`0xa1c10000..0xa1c42fff`). V125 não muda o algoritmo nem
o payload: altera somente o alvo dos mesmos padrões GPL de 64 KiB para
`0xa1c10000` e repete a cópia+comparação integral, ainda sem latch ou salto.

Resultado físico V125: igual a V124 (`G:00000000`, RAM simples correta,
`LF:00007f18`, `!COPY! Z`). O alvo do treino não altera a falha. V126 mantém
esse caminho e reporta o primeiro word divergente como
`E:<offset-no-slot>:<word-flash>:<word-ddr>`. Isso separa corrupção imediata,
erro em limite de cache/página e degradação durante a cópia, sem mudar outro
registro de DDR e sem saltar para Stage-2.

Resultado físico V126: `E:00000204:0080d825:0080d83d`; a divergência ocorre
no primeiro KiB do payload e troca `0x18`, não é limite de página, cache ou
endereço de carga. V127 preserva a varredura GPL e a seleção por maior janela,
mas substitui o padrão sintético pelos primeiros 64 KiB do Stage-2 armazenado
na SPI. É uma adaptação de vetor de teste motivada pelo dado físico, não uso de
código proprietário; a comparação final continua integral e reportará o
primeiro erro se existir.

Resultado físico V127: só `DCWK...` e `P123...`; não alcançou `G`, portanto
o treino de 64 KiB com o payload real não terminou de modo observável. V128
reduz exclusivamente esse vetor para 4 KiB (ainda inclui `+0x204`) e emite
`0..f`, uma vez para cada linha de write-tap concluída. Assim, o próximo log
identifica se a causa é custo de varredura ou uma combinação específica que
trava o controlador.

Resultado físico V128: `gG:00000000`, seguido de erro reduzido em V126 para
`E:00000204:0080d825:0080d82d` (diferença agora `0x08`). A marca `g` foi um
erro de escopo no progresso, não uma linha de scan: ela era emitida só após o
fim do laço, com `w=32`. V129 corrige esse escopo, mostra `0..f` por linha e
amplia o vetor de payload para 16 KiB. A redução de `0x18` para `0x08` justifica
amostrar mais conteúdo real antes de descartar essa seleção GPL.

Resultado físico V129: as 16 linhas `0..f` completaram, mas retornaram o
mesmo `E:00000204:0080d825:0080d82d` de V128. O tamanho do vetor não é a
variável. Auditoria da fonte GPL revelou uma divergência no porte: em barramento
x8 `_memctl_set_phy_delay_dqrf()` substitui a janela de escrita por
`static_cal_data[bit].phase` antes de chamar a fórmula final. V130 restaura
essa regra com as fases do perfil GPL Winbond irmão (`10,30,0,30` por grupos de
oito DQs) e conserva a seleção GPL da janela de leitura.

Resultado físico V130: `G:00ff00ff` e o mesmo erro de cópia `+0x204`.
As fases estáticas do perfil W631GG8KB não são adequadas à W631GG8NB desta
placa; estão rejeitadas. O XOR `0x00000008` corresponde, pela conversão GPL
x8, ao DQ físico 19. V131 preserva o resultado GPL original para todos os
outros campos e varre somente a fase de escrita de DQ19 de 0 a 30. Cada
`W0..Wf` informa o XOR do word alvo após copiar os primeiros 0x208 bytes;
nenhum Stage-2 é executado.

Resultado físico V131: `D19:001e0f00` e todos os `W0..Wf` retornaram
`00000008`. A fase de escrita é neutra para a falha. No formato GPL, a byte
central `0x0f` de D19 é o tap de leitura; V132 varre apenas esse byte em taps
pares (`R0..Rf`) e mantém write, read-end e read-start. Isso testa a próxima
variável física sem perturbar o restante da configuração selecionada.

Resultado físico V132: `R0..R7=00000018` e `R8..Rf=00000010`; o tap de
leitura central influencia o erro, mas não o elimina. V133 cria uma matriz
local 16×16 de write × read-centre de DQ19, mantendo os campos read-end/start
GPL. Em cada linha `M0..Mf`, `.` significa XOR zero no word `+0x204` e `x`
qualquer divergência. Isso fornece um candidato mensurável sem alterar os
outros 31 DQs.

Resultado físico V133: em quase todas as linhas `M0..Mf`, as colunas 9..f
(read-centre 18..30) são `.`; em `M6` e `Me` a janela inicia em 16. A fase de
escrita não limita o ponto. V134 aplica o centro da janela robusta, 24, como
`D19=001e1800`, preserva os demais 31 valores GPL e faz a primeira comparação
integral do Stage-2 com esse candidato. Ela continua sem latch/salto.

V134 foi gravada na SPI física em 2026-09-20: somente a região Stage-1
`0x000000..0x007fff` foi apagada, escrita e verificada pelo CH341A contra a
imagem `sg1002-mr-v134-dq19-r24-stage2-copy-16m.bin`
(`sha256 88c11c7360149960821808aab3d7bef85d3dcbb68132afaabadcb50a45effc9e`).
O resultado de TTL ainda é pendente. O resultado esperado é `!COPY_OK!` e
`!FULL_OK!`; qualquer `E:` novo continuará identificando o primeiro word
divergente sem executar Stage-2.

Resultado físico V134: `G:00000000`, mas a verificação básica já corrompeu
(`R0:12347678`, `R1:9abcfef0`, `X0:00002000`, `LF:0000ff7f`) e a cópia falhou
em `E:00000200:0320d025:0320f025`. A linha `D19:3a000000` não é o conteúdo do
registro: V134 a imprimiu de `t1` depois de `phy_delay_commit`, macro que usa
esse temporário. Mais importante, a fase 6 dispara `DPHY_CAL_CTRL` depois do
commit manual, podendo substituir DQ19 antes da cópia. V135 move o mesmo
candidato `001e1800` para depois dessa recalibração e lê `DPHY_DELAY0+76` de
volta antes de prosseguir. Ela separa a ordem de aplicação da validade do
candidato, sem salto para Stage-2.

V135 foi gravada na SPI física em 2026-09-20, limitada à região Stage-1
`0x000000..0x007fff`, com erase/write/verificação concluídos pelo CH341A.
Imagem: `sg1002-mr-v135-dq19-r24-post-cal-stage2-copy-16m.bin`
(`sha256 a5201dede085ecedfc79cf6fadc09d523fbe8037b76dbd27e0d1d3b9a63cca0f`).
O TTL pendente deve apresentar `D19:001e1800`; qualquer outro valor agora é
uma leitura física do registro, não um temporário de software.

Resultado físico V135: `G:00000000`, `R0/R1/X0` voltaram ao baseline bom e a
falha integral foi `E:00000200:0320d025:0320f025`, isto é, dois words antes do
erro V126. A impressão `D19:3a000000` continua inválida: embora a carga tenha
sido preservada antes de `puthex32`, a própria macro `putc` reutiliza os
temporários entre os caracteres. Não se pode tratar esse texto como readback.
A mudança de `+0x204` para `+0x200`, porém, é evidência operacional de que o
ajuste mudou a configuração usada no copy. V136 evita concluir a partir de um
único word: após a calibração GPL e a fase 6, testa os taps pares `18..30` de
DQ19. Para cada tap copia e compara os `0x33000` bytes completos de Stage-2 e
emite `T<tap/2>:<valor-lido>:<primeiro-offset>:<xor>`; offset e xor zero
significam cópia integral correta. Não faz latch nem salto.

V136 foi gravada na SPI física em 2026-09-20, novamente apenas em
`0x000000..0x007fff`, com erase/write/verificação do CH341A concluídos.
Imagem `sg1002-mr-v136-dq19-read-window-full-copy-16m.bin`
(`sha256 b7673d421a3c40885c2b1131f2358321216e682d93a6fbb328e6d88760041ab8`).

Resultado físico inicial V136: o arquivo TTL local contém somente seis bytes
ilegíveis e não contém `DCWK` nem `P123`. A imagem foi confirmada pelo
programador, mas esse resultado não é uma medição de DQ19: a análise binária
mostra que V135 e V136 têm o stub NOR idêntico e os primeiros 6546 bytes do
payload SRAM idênticos; os dois marcadores ocorrem antes de toda alteração
V136. Para recuperar um ponto observável e separar conexão/boot de regressão
posterior, a V135 foi restaurada e verificada na SPI em 2026-09-20, só na
região `0x000000..0x007fff` (SHA-256
`a5201dede085ecedfc79cf6fadc09d523fbe8037b76dbd27e0d1d3b9a63cca0f`).

Correção de evidência: a ausência de TTL atribuída inicialmente a V136 foi
causada por falha de alimentação, logo **não é resultado de V136** e fica
anulada. Após restaurar alimentação, V135 voltou a emitir `DCWK`,
`G:00000000`, `R0:12345678`, `R1:9abcdef0`, `X0:00000000`, `LF:00007f18` e
`E:00000200:0320d025:0320f025`. Isso reproduz o resultado V135 e confirma que
o próximo ensaio útil é V136, a varredura integral da janela DQ19; ela ainda
precisa ser recolocada na SPI quando o programador estiver conectado.

V136 foi gravada novamente depois dessa correção de alimentação, em
2026-09-20, e o CH341A confirmou erase/write/verificação de
`0x000000..0x007fff`. Esta é a gravação válida para o próximo TTL.

Resultado físico V136: não reiniciou porque o teste termina de propósito em
um loop após a varredura. As linhas observadas (`T9`, `Ta`, `Tb`, `Td`, `Tf`)
aplicaram respectivamente `001e1200`, `001e1400`, `001e1600`, `001e1a00` e
`001e1e00`; todas falharam no mesmo offset `00000200`, XOR `00002000`. As
linhas `Tc` e `Te` foram perdidas na UART — há linhas posteriores, portanto
não representam travamento. A pausa TX do modo raw era menor que um frame a
115200 e foi elevada de `0x4000` para `0x20000` ciclos para tornar a próxima
matriz auditável. Correção do mapeamento x8 GPL: XOR lógico `0x00002000`
mapeia para DQ5; DQ24–31 são lanes não usadas e são corretamente puladas no
modo x8. V137 preserva os outros campos GPL de DQ5, varre apenas seu centro de
leitura nos taps pares `0..30`, e faz copy+compare integral por tap. Formato:
`Q<tap/2>:<valor-aplicado>:<primeiro-offset>:<xor>`; zero/zero aprova o
payload completo. Não faz latch ou salto para Stage-2.

V137 foi gravada na SPI física em 2026-09-20, limitada a
`0x000000..0x007fff`, e o CH341A confirmou erase/write/verificação. Imagem
`sg1002-mr-v137-dq5-read-sweep-full-copy-16m.bin` (SHA-256
`783041617fe3ffbd08d4badf147f1231e7b0d12055deea5f8a260c2932c75a37`).

Resultado físico V137: completou os 16 taps e parou no loop diagnóstico
esperado, sem reiniciar. O estado GPL de DQ5 é `B5:051e1e00`. Em `Q0..Q4`,
o primeiro erro é no offset zero com XOR `20000000`; `Q5` move-o para `+0x50`.
Em `Q6..Qd`, o primeiro erro está em `+0x200`, XOR `00002000`; `Qe..Qf`
movem-no para `+0x204`, XOR `00000018`. Assim, variar DQ5 isoladamente não
resolve as duas palavras, mas muda a fronteira de falha de modo reproduzível.
V138 mede a interação que resta: linhas são DQ5 `12..26`, colunas são DQ19
`18..30`; `.` exige simultaneamente igualdade em `+0x200` e `+0x204`, `x`
marca qualquer falha. É só cópia de 0x208 bytes e não salta para payload.

V138 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`; o CH341A confirmou erase/write/verificação. Imagem
`sg1002-mr-v138-dq5-dq19-joint-prefix-matrix-16m.bin` (SHA-256
`8814042b076fce4bef62ed004b579e854128b59cf8232e4b89421faab8651f53`).

Resultado físico V138: as oito linhas `M6..Md` foram completadas com sete
`x` cada e o loop diagnóstico reteve o processador como projetado. Logo, o
retângulo DQ5-centro `12..26` × DQ19-centro `18..30` não contém uma solução
para ambas as palavras. A hipótese de apenas dois delays de dados fica
rejeitada. A lacuna no porte GPL é a etapa seguinte publicada
`memctlc_dqm_calibration()`: ela mede os dois delays de máscara de escrita
em `DCDQMR` usando stores de meio-word. V139 porta essa rotina para SRAM,
imprime o `DQM` final, e só então executa a cópia/comparação integral de
Stage-2.

V139 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v139-gpl-dqm-cal-stage2-copy-16m.bin` (SHA-256
`2e3b1cbc12ad61047ec83761e02dbf240d7e9f6cabafba09a86d037c621b02b1`).

Resultado físico V139: `G:00000000`, `DQM:0f0f0000` e os vetores básicos
(`R0`, `R1`, `X0`) permaneceram corretos. A execução parou imediatamente
após `LF:00007f18`, não por falha de DDR: o script habilitou por engano o
macro histórico V123, cujo objetivo é justamente entrar em loop após esse
diagnóstico. V140 remove esse macro de parada; conserva a calibração DQM GPL
e deixa o caminho V124/V126 alcançar a cópia integral e seu verificador.

V140 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v140-gpl-dqm-stage2-copy-16m.bin` (SHA-256
`efc9b84a926a04a45cff7a21b75596152156d232aa335e3d97f5f00eed9613e5`).

Resultado físico V140: a cópia passou os offsets antes problemáticos e a
primeira divergência passou a `E:00000518:1000fffe:1000ffff` (XOR
`00000001`). Pela conversão x8 do GPL, o bit lógico zero é DQ16 físico. V141
mantém o DQM calibrado (`0f0f0000`) e todos os outros campos GPL, varrendo só
o centro de leitura DQ16 nos taps pares `0..30`. Cada linha
`N<tap/2>:<word>:<primeiro-offset>:<xor>` é uma cópia/comparação integral;
`00000000:00000000` nos dois últimos campos aprova o payload completo.

Resultado físico V141: todos os taps, inclusive `Nf:001e1e00` que repõe o
word original, falharam primeiro em `+0x200` com XOR `00002000`. Como V140
com o mesmo word chegou antes até `+0x518`, a diferença é o commit de PHY
feito após escrever DQ16. V142 reproduz um commit sem mudança de DQ16,
reafirma DQM `0f0f0000` por uma segunda transação GPL e então faz a cópia
integral; a linha `K:<DQ16>:<DCDQMR>` registra os dois valores efetivos.

Resultado físico V142: `K:001e1e00:80000030` e a cópia voltou a falhar em
`+0x200`/XOR `00002000`. O segundo campo era uma leitura incorreta de DACCR
(o macro havia reutilizado `t0`), portanto não mede DCDQMR. A auditoria da
transação revelou também uma divergência concreta do GPL: o macro assembly
relia DACCR depois de limpá-lo, enquanto o GPL restaura `bit4` sobre o valor
capturado antes da limpeza. V143 corrige isso, recarrega `DCDQMR` antes da
leitura e repete o teste isolado de V142.

V143 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v143-dq16-dqm-gpl-commit-stage2-copy-16m.bin` (SHA-256
`c5d0501fdeb3476b4d8639a5c671717495ce015f178effa5ca9e6526743e3d79`).

Resultado físico V143: a leitura correta confirmou `K:001e1e00:0f0f0000`,
mas a cópia ainda retornou a `+0x200`/XOR `00002000`. Portanto, escrever
DCDQMR de volta não recompõe seu estado efetivo depois de um commit de DQ.
V144 faz o procedimento GPL completo de DQM após o commit DQ16 e testa a
cópia integral. Se ele recuperar a fronteira `+0x518`, essa será a ordem
usada para cada candidato DQ16 subsequente.

Resultado físico V144: após o commit DQ16, a rotina GPL de DQM selecionou
`000f0000`: DQM0 não encontrou janela, enquanto DQM1 escolheu tap 15; a
cópia voltou a `+0x200`/XOR `00002000`. V145 executa o mesmo recommit DQ16
no helper C SRAM (a mesma transação de `select_taps`) e então roda a mesma
calibração DQM C. Ele separa qualquer erro residual do macro assembly de uma
propriedade real do controlador ao reescrever DQ16.

Resultado físico V145: o helper C produziu o mesmo `K:001e1e00:000f0000`
e o mesmo erro `+0x200`, descartando o macro assembly como causa. A regra
validada é: não alterar DQ após DQM. V146 injeta DQ16 read-centre 28 durante
`select_taps()` GPL, antes de DQM; esse é o vizinho imediato do centro 30
que V140 usou para alcançar `+0x518`/XOR 1. A rotina DQM roda somente depois
de todos os commits de DQ, como no fluxo GPL.

Resultado físico V146: embora a imagem contenha a instrução para DQ16 r=28
(confirmada por desassemblagem), o readback foi `D16:001e1e00`; o
`phy_commit()` GPL reescreveu o campo para r=30. A cópia chegou a
`E:00000304:0080d825:0080d82d` (XOR 8), portanto a configuração padrão com
DQM ainda é a única efetivamente aplicada. V147 escreve r=28 depois da
calibração DQM, sem novo commit, e registra `K:<DQ16>:<DCDQMR>` antes da
cópia para testar se o registro pode atuar diretamente sem invalidar DQM.

V147 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v147-dq16-r28-post-dqm-no-commit-stage2-copy-16m.bin`
(SHA-256 `389800ad1ee474c4993a0cf9b64215db63abc39230f2082982263bc2dc38d230`).

Resultado físico V147: `K:001e1c00:0f0f0000` confirmou que a escrita direta
preserva DQM, mas a cópia permaneceu exatamente em `+0x518`/XOR 1; sem latch,
o novo centro não atua. V148 usa a única transação ainda não combinada com
esse estado: escreve r=28 e aciona `DPHY_CAL_CTRL`, sem DMCR/DACCR, então
registra DQM e executa a cópia integral.

Resultado físico V148: `K:001e1c00:0f0f0000` confirmou que DPHY_CAL_CTRL
latcha DQ16 sem derrubar DQM; a primeira falha mudou para `+0x204`, XOR
`00000018`. V149 usa esse mecanismo validado para varrer todos os 32 centros
de leitura DQ16. Cada `T:<tap>:<word>:<dqm>:<offset>:<xor>` compara os
`0x33000` bytes completos; offset e XOR zero identificam um candidato de
cópia integralmente correta. O teste termina em loop por projeto.

Resultado físico V149: os 32 taps de DQ16 preservaram `DCDQMR=0f0f0000`,
mas nenhum aprovou a cópia integral. Taps `3,14,15,17,19..25,27,29,31`
deixam apenas XOR `1` em `+0x518`; o tap 24 foi escolhido como representante
para V150. Ela mantém DQ16=24 e varre DQ19 por DPHY_CAL_CTRL, emitindo
`U:<tap>:<word19>:<dqm>:<offset>:<xor>` para cada cópia integral. Essa é a
primeira matriz conjunta que conserva DQM efetivamente ativo.

Resultado físico V150: nenhum tap DQ19, com DQ16=24, aprovou a cópia; DQM
permaneceu `0f0f0000` em toda a matriz. A falha alterna entre XOR 1 em
`+0x518` e XOR 8 em offsets `+0x204..+0x404`, logo centros de leitura não
resolvem a fronteira. V151 varre o campo de fase DQS de DQ16 (bits 23:16),
que ainda era `0x1e` em todos os testes anteriores, usando DPHY_CAL_CTRL e
comparação integral por ponto.

Resultado físico V151: nenhuma das 32 fases DQS de DQ16 aprovou a cópia,
mas DQM ficou `0f0f0000` em toda a varredura. As fases 3, 6, 7 e 13..29
chegam a `+0x518` com apenas XOR 1; as demais falham antes com XOR 8. Isso é
progresso de caracterização, não regressão: o latch e DQM estão estáveis e
fase não é a variável isolada que falta. V152 varre o campo de write delay
de DQ16, a última dimensão individual dessa lane, por DPHY_CAL_CTRL.

Resultado físico V152: a varredura de write delay DQ16 também não produziu
cópia íntegra; como nas duas dimensões anteriores, DQM foi `0f0f0000` em
todas as linhas. Read, phase e write de DQ16 isoladamente estão portanto
esgotados. V153 muda para a hipótese seguinte: matriz conjunta DQM0/DQM1,
programada pelo helper C GPL. Para poupar tempo, cada um dos 1024 pares copia
e compara só `0x51c` bytes, incluindo o primeiro erro conhecido; `.` exige
prefixo perfeito e produz candidatos para a próxima verificação integral.

V153 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v153-dqm-pair-prefix-matrix-16m.bin` (SHA-256
`0850b0ae0a9b08d8b09c5f830ee381cd354510bf999b42637c1b36762c9074b0`).
O teste emitirá 32 linhas `M:<dqm0>:<32 marcadores>`; `.` representa cópia
idêntica até `+0x518`, e `x` representa divergência. Qualquer `.` será
validado depois com cópia integral antes de habilitar o salto para U-Boot.

Resultado físico V153: todas as 32 linhas foram integralmente `x`; portanto
nenhum dos 1024 pares DQM0/DQM1 torna íntegro sequer o prefixo por `+0x518`.
Isso encerra DQM como variável independente sem alterar a estabilidade
observada (`DQM:0f0f0000`). V154 testa a interação ainda não coberta após a
calibração GPL: matriz completa dos centros de leitura DQ16 × DQ19, aplicada
por `DPHY_CAL_CTRL`, com o mesmo prefixo curto e DQM mantido ativo.

V154 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v154-dq16-dq19-dphy-prefix-matrix-16m.bin` (SHA-256
`93a328c1ebea21f52a6a53476cf24db11808be7e12f27811e3e75316476afa85`).
Ela emitirá 32 linhas `J:<dq16>:<32 marcadores>`; `.` indica que o par
DQ16/DQ19 copia exatamente até `+0x518`, e será então submetido à cópia
integral antes de qualquer salto ao U-Boot.

Resultado físico V154: todas as 1024 combinações DQ16/DQ19 produziram `x`.
Assim, DQ19 não complementa DQ16 no estado em que a calibração GPL de DQM
permanece ativa. V155 cobre a interação remanescente indicada por V137/V138:
matriz completa DQ16 × DQ5 de centros de leitura, aplicada por
`DPHY_CAL_CTRL` e verificada no mesmo prefixo.

V155 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v155-dq16-dq5-dphy-prefix-matrix-16m.bin` (SHA-256
`f412c0dc9808d2290cfc43d1ece45c1eddfcf74409e51b16309c1d968f462014`).
O teste produzirá 32 linhas `H:<dq16>:<32 marcadores>`; `.` identifica o
par DQ16/DQ5 que preserva todo o prefixo até `+0x518`.

Resultado físico V155: todas as combinações DQ16/DQ5 foram `x`. Com V154,
isso exclui DQ5 e DQ19 como parceiros de centro de leitura de DQ16 sob DQM
GPL. A auditoria do SDK mostra que o próximo grau de liberdade não coberto é
global: `DCDR=0x8b540000` codifica TXCLK=17, CLKM=13 e CLKM90=10. V156 varre
somente TXCLK `0..31`, preservando os dois últimos campos e o estado DQ/DQM
GPL, após o intervalo de estabilização publicado (`0x10000` leituras de MCR).

V156 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v156-dcdr-tx-prefix-scan-16m.bin` (SHA-256
`6433489d997ee5502db38e43e99d1ecd3b3dfdee2e8c679e226fcbf96e398324`).
O TTL reportará `C:<txclk>:<DCDR>:[.|x]`; `.` é igualdade até `+0x518`.

Resultado físico V156: TXCLK `0..31` produziu somente `x`, inclusive o valor
GPL original 17 (`DCDR=8b540000`). V157 mantém TXCLK=17 e CLKM90=10 e varre
CLKM `0..31` em `DCDR[26:22]`, no mesmo estado de DQ/DQM e critério curto.

V157 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v157-dcdr-clkm-prefix-scan-16m.bin` (SHA-256
`18df5ee3b77e64535242f0abdf602fb4f4b4558adf1eb38a5831b662377a5bfa`).
O TTL reportará `K:<clkm>:<DCDR>:[.|x]`; `.` identifica um CLKM que mantém
igualdade até `+0x518`. O DCDR inicial é o perfil GPL `8b540000` (TXCLK=17,
CLKM=13, CLKM90=10), mas a varredura testa o campo completo sem pressupor que
o bin de DDR3 W631GG8NB-12 use o mesmo centro do perfil KB-15.

Resultado físico V157: CLKM `0..31` também produziu somente `x`, inclusive
o valor GPL 13 (`DCDR=8b540000`). V158 preserva TXCLK=17 e CLKM=13 e varre
CLKM90 `0..31` em `DCDR[21:17]`; é o último campo global de DCDR ainda não
testado com a calibração GPL de DQ/DQM ativa.

V158 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v158-dcdr-clkm90-prefix-scan-16m.bin` (SHA-256
`8a1eee45cb905853103263db6f6834930f8bf97658d62455907f58cdd3053d64`).
O TTL reportará `L:<clkm90>:<DCDR>:[.|x]`; `.` preserva o prefixo até
`+0x518` e será validado por uma cópia integral na próxima imagem.

Resultado físico V158: CLKM90 `0..31` produziu somente `x`, inclusive o
valor GPL 10 (`DCDR=8b540000`). Portanto os três campos globais de DCDR foram
esgotados sem alterar a falha. Há uma lacuna de ordem entre os ensaios: V128/
V129 calibraram contra o payload Stage-2 real, mas precederam a porta GPL de
`memctlc_dqm_calibration()`; V140+ calibraram DQM, mas voltaram ao padrão
sintético publicado pelo SDK. V159 combina ambos sem código proprietário:
treina sobre os primeiros 4 KiB reais em `0xa1c10000`, aplica DQM GPL e só
então compara os `0x33000` bytes completos, reportando o primeiro `E:`.

V159 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v159-gpl-payload-dqm-stage2-copy-16m.bin` (SHA-256
`eb14e54af7f95b92262d36c144b65a9b8ce6bf9748cf89c259463dfab65b1f72`).
Resultado esperado: `G:00000000`, `DQM:0f0f0000`, seguido de `!COPY!` e
`!FULL_OK!`; se ainda houver erro, `E:<offset>:<flash>:<ddr>` o fixa sem
executar ou saltar para o Stage-2.

Resultado físico V159: `G:00000000` e `DQM:0f0f0000` passam, mas a cópia
real retorna `E:00000204:0080d825:0080d82d`, o mesmo XOR `0x8` histórico de
DQ19. Isso exclui DQM e os três campos globais de DCDR, bem como a ordem entre
payload e DQM. V160 cobre os dois únicos campos de DQ19 ainda não varridos:
`read-start` (bits 7:0) e `read-end` (bits 23:16). Cada linha `A` ou `B`
testa um tap por `DPHY_CAL_CTRL`, preservando DQM; `.` exige igualdade do
prefixo `0x000..0x204` do payload real.

V160 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v160-dq19-read-bounds-prefix-scan-16m.bin` (SHA-256
`a54ddca1ba96984cfdf6239a844452b3a3e94c58d077ddc351685d820f65fd61`).
O TTL emitirá 32 linhas `A:<read-start>:<DQ19>:[.|x]` e depois 32 linhas
`B:<read-end>:<DQ19>:[.|x]`. Qualquer `.` será promovido imediatamente para
uma cópia integral usando exatamente aquele word DQ19.

Resultado físico V160: há uma janela ampla de pontos `.` nos dois limites.
Em particular, o word GPL base `DQ19=001e0f00` (`A:0`) e o mesmo word no
limite `B:30` preservam o prefixo por `+0x204` quando reaplicados pelo latch
DPHY após treino com payload real e DQM. V161 promove o primeiro desses
resultados, sem alterar o word: reaplica `001e0f00`, executa o mesmo latch e
faz a cópia/verificação integral. Isso valida o efeito de ordem observado em
V160 antes de procurar outra dimensão.

Resultado físico V161: a cópia posterior falhou novamente em
`E:00000204:0080d825:0080d82d`. A conclusão correta não é descartar V160:
seu prefixo de `0x208` bytes passa, mas V161 acrescenta a fase normal até a
cópia e transfere `0x33000` bytes. V162 faz a cópia integral exatamente no
ponto de V160, após o mesmo relatch de DQ19. `F:OK` aponta estado posterior;
`F:<offset>:<flash>:<ddr>` aponta dependência de volume/tempo já nesse ponto.

V162 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v162-dq19-early-full-copy-16m.bin` (SHA-256
`4d03af07e5d40497c0e8ca3920c4b1f03773b5818db6105432fe767ca92ae06f`).
O TTL emitirá `F:OK` ou `F:<offset>:<flash>:<ddr>` e então permanecerá em
loop diagnóstico; não executa Stage-2.

Resultado físico V162: `F:00000204:0080d825:0080d82d`, igual a V161. Logo
o primeiro erro não depende das fases posteriores de Stage-2; V160 só mediu
um intervalo transitório de cópia curta. V163 mantém o mesmo DQ19 e executa
`0x10000` leituras de MCR após o latch DPHY — a estabilização usada pelo SDK
para transições de timing — antes da cópia integral. Ele emitirá `Q:OK` ou o
primeiro `Q:<offset>:<flash>:<ddr>`.

Resultado físico V163: `Q:00000518:1000fffe:1000ffff`. O settle removeu a
falha anterior de DQ19 em `+0x204`; a nova primeira falha é o XOR lógico 1,
isto é, DQ16. V164 deixa DQM e o settle ativos e varre os limites ainda não
cobertos de DQ16: `U` para `read-start` e `V` para `read-end`. Um `.` exige
igualdade até `+0x518` e será promovido a cópia integral.

Resultado físico V164: todos os taps de `read-start` e `read-end` de DQ16
produziram `x`. V165 repete o centro de DQ16 no estado de V163 (payload real,
DQM e settle), combinação ainda não coberta pela varredura V149.

V165 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v165-dq16-settled-center-prefix-scan-16m.bin` (SHA-256
`3094edabe48c002f83057ef87de7bfddd596c9ac26dfb54428f579aa8ebe3817`).

Resultado físico V165: todos os centros de DQ16 foram `x`. V166 repete o
campo de escrita de DQ16 com payload real e DQM GPL, combinação não coberta
por V152.

V166 foi gravada na SPI física em 2026-09-21 às 18:35:41, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A
(`logs/spi-write-20260921-183541.log`: `VERIFIED`). Imagem
`sg1002-mr-v166-dq16-write-payload-dqm-scan-16m.bin` (SHA-256
`357f087158c7309a58e9c731ee227b1f6ea74ad270b3379fcf49613be5c90306`). O
pareamento imagem/log é por horário: imagem gerada às 18:35, gravação às 18:35:41.

Resultado físico V166 (linhas `W:<tap>:<DQ16>:<DQM>:<primeiro erro>:<XOR>` do
log TTL, 32 taps de write de DQ16): `04`, `0a–0d`, `0f–14`, `16–18`, `1a`, `1c`,
`1e` e `1f` terminam no estado de V163 (`+0x518`, XOR 1 = DQ16), sem regressão
de DQ19. Os demais (`00–03`, `05–09`, `0e`, `15`, `19`, `1b`, `1d`) reintroduzem
DQ19 (XOR 8) em `+0x204`, `+0x284`, `+0x304` ou `+0x384`. Nenhum tap de write
de DQ16 remove a falha em `+0x518`; `write=4` é só um dos valores que a
preservam. V167 fixa `write=4` e varre os limites de DQ19.

*Entrada V166 registrada por Claude Sonnet 5 (IA), a partir dos logs
`spi-write-20260921-183541.log` e `serial_ttyUSB0.log`.*

V167 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v167-dq16w4-dq19-bounds-16m.bin` (SHA-256
`bb8351c58cc736f30578c460aaf92b422326b0706fe0886b7c557fcd90de670c`).
Resultado físico: com DQ16-write=4, DQ19 `read-start=2` (`001e0f02`) passa
o prefixo; deve ser promovido à cópia integral com DQM ativo e settle.

Detalhe do resultado físico V167 (log `codex-5.log`; `G:00000000`,
`DQM:0f0f0000`, DQ16 fixo em `041e1e00`). O `.` mede só o prefixo `0x208` e
sem settle, portanto não garante a cópia integral:

- `A` (DQ19 read-start, word `001e0f<tap>`): passam `02`, `05–09`, `0b`, `0c`,
  `0e`, `0f` e `11–1f`; falham `00`, `01`, `03`, `04`, `0a`, `0d` e `10`. Com
  DQ16 em write=4, o word GPL base (`A:00`, `001e0f00`) falha, embora
  passasse em V160.
- `B` (DQ19 read-end, word `00<tap>0f00`): só `02`, `05` e `1e` falham.

V168 promove DQ16 `041e1e00` + DQ19 `001e0f02` (`A:02`) à cópia integral:
escreve os dois words, aplica um único latch por `DPHY_CAL_CTRL` (que preserva
DQM), faz o settle de `0x10000` leituras de MCR de V163 e copia/compara
`0x33000` bytes de `0x050000` para `0xa1c10000`. Não salta ao U-Boot. O TTL
imprime a leitura de conferência `W:<DQ16>:<DQ19>:<DCDQMR>` e depois `FULL_OK`
ou o primeiro `E:<offset>:<flash>:<ddr>`.

V168 foi gravada na SPI física em 2026-09-21 às 19:02:49, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A
(`logs/spi-write-20260921-190249.log`: `VERIFIED`). Imagem
`sg1002-mr-v168-dq16w4-dq19s2-full-copy-16m.bin` (SHA-256
`d3af27bc5255d7cde88fec2eb605e11eac431c179a7ce287c730a5a8a008e117`).
Resultado físico V168 (log TTL): `G:00000000`, `DQM:0f0f0000`,
`W:001e1e00:001e0f00:0f0f0000`, `E:00000204:0080d825:0080d83d`. A cópia
integral falhou em `+0x204` com XOR `00000018` (DQ19 e DQ20); DQM ficou
intacto. Duas observações:

1. A leitura de conferência não devolveu o que foi escrito. Depois do latch e
   do settle, DQ16 voltou `001e1e00` (escrito `041e1e00`) e DQ19 voltou
   `001e0f00` (escrito `001e0f02`). Os dois campos alterados são write
   (bits 31:24) e read-start (bits 7:0); o centro (15:8) já tinha sido
   confirmado por leitura em V147/V148. Todos os readbacks documentados até
   aqui têm 31:24 e 7:0 iguais a zero. Explicações possíveis: (A) esses campos
   não são legíveis, mas a escrita atua; (B) o latch descarta a escrita
   deles. As linhas `W:` de V166/V152 imprimem o word calculado (`s2`), não
   uma leitura, então nenhum ensaio anterior confirmou por leitura que o
   campo write persiste; o efeito de V149–V167 foi só inferido do
   comportamento da cópia.
2. A assinatura `+0x204`/XOR `0x18` é a mesma de V148 (DQ16 centro 28 por
   latch) e difere do estado-base de V163 (`+0x518`/XOR 1), embora o
   readback de DQ19 seja igual ao de V163. V168 difere de V163 pelo hook
   write=4 em `select_taps()` e pelas escritas do bloco, ambos em campos que
   não voltam na leitura. Isso favorece (A), mas não exclui ruído entre
   execuções; V169 mede as duas coisas.

*Resultado V168 analisado e registrado por Claude Sonnet 5 (IA).*

V169 é um ensaio de diagnóstico, não uma tentativa de cópia integral. Roda sobre
o estado calibrado padrão (sem o hook write=4) e usa cópia curta de `0x520`
bytes, que cobre as falhas conhecidas de `+0x204` e `+0x518`. Três partes:

- `R:<DQ16>:<DQ19>:<DCDQMR>`: os registradores como calibrados.
- `N:<n>:<offset>:<xor>` (4 execuções sem latch) e `L:<n>:<offset>:<xor>` (4
  execuções após latch e settle de `0x10000` leituras de MCR): medem se o
  primeiro erro é repetível. XOR `0` significa cópia idêntica. `L` é o análogo
  curto de V163 (`+0x518`, XOR 1).
- `T:<n>:<tap>:<DQ16 pré-latch>:<DQ16 pós-latch>:<DCDQMR>:<offset>:<xor>`:
  alterna o tap de write de DQ16 entre `01` e `0c` (V166 registrou `01`
  falhando em `+0x384` e `0c` chegando a `+0x518`). O readback pré-latch
  separa campo não legível (hipótese A: já volta `001e1e00` antes do latch)
  de campo descartado pelo latch (B: volta o word escrito antes e o perde
  depois).

Como ler: N e L idênticos entre as repetições indicam resultado determinístico,
e as varreduras de tap são interpretáveis; se oscilarem, é ruído e as
varreduras V149–V167 precisam ser reinterpretadas. Em T, offset/XOR diferentes
e repetíveis entre `01` e `0c` indicam que o campo write atua mesmo sem voltar
na leitura.

V169 foi gravada na SPI física em 2026-09-21 às 19:15:09, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A
(`logs/spi-write-20260921-191509.log`: `VERIFIED`). Imagem
`sg1002-mr-v169-dphy-field-readback-repeat-16m.bin` (SHA-256
`06bc1a8a57d3217818bdcd6c64508b84b8b71e78ad891d7296447928094d44ae`).
Resultado físico V169 (log TTL; `G:00000000`, `DQM:0f090000`,
`R:001e1e00:001e0f00:0f090000`):

- `N` (4 cópias sem latch, estado idêntico): primeiro erro em `+0x204/XOR 8`,
  `+0x518/XOR 1`, `+0x284/XOR 8`, `+0x304/XOR 8`. `L` (4 cópias após latch e
  settle): `+0x304`, `+0x284`, `+0x204`, `+0x204`, todas XOR 8.
- `T` (tap de write de DQ16 alternando `01`/`0c`): o readback já volta
  `001e1e00` **antes** do latch e continua igual depois; o primeiro erro é
  `+0x284` nas três tentativas com tap `01` e `+0x304`, `+0x304`, `+0x384`
  com tap `0c`; DCDQMR `0f090000` em todas.

Conclusões:

1. Não é repetível. Com o estado inalterado, o primeiro erro muda a cada
   cópia, e latch/settle não alteram a distribuição. Os resultados de
   V160–V168 que dependem de um único pass/fail por ponto (inclusive "o
   settle remove a falha em `+0x204`", "A:2 passa", "write=4 preserva o
   estado") são ruído.
2. O payload tem o mesmo word `0080d825` em `+0x204`, `+0x284`, `+0x304`,
   `+0x384` e `+0x404` (`build-gpl/u-boot-factory-payload-v22/u-boot.bin`, igual
   em `u-boot-dtb.bin`). Cada ocorrência falha com certa probabilidade e o
   "primeiro erro" é apenas a primeira que falhou. Todas as falhas vistas são
   `0→1` em DQ19 (XOR 8), DQ19+DQ20 (XOR `0x18`) ou DQ16 (XOR 1, `+0x518`).
3. A calibração em si não é determinística: o DQM foi `0f0f0000` em V163–V168
   e `0f090000` em V169, com o mesmo código.
4. O readback de `T` antes do latch descarta a hipótese B de V168 (o latch
   descartar a escrita): o campo write (31:24) simplesmente não volta na
   leitura, seja por ser ilegível mas efetivo, seja por ser ignorado. O
   primeiro erro depende do tap (`01`: `+0x284`; `0c`: `+0x304`/`+0x384`), o
   que sugere efeito, mas com n=3 e o ruído acima não é conclusivo.

Achado (contexto do usuário: dump de fábrica, emulador QEMU e trace em
`/media/dados_2tb/opw/arm-selfmod-lab/qemu-rtl8380-sg1002-mr`, cujo `run.sh`
roda uma imagem de 16 MiB; o emulador não é confiável para timing, pois o DDR é
RAM): **`sg1002_mr_gpl_cali.c` definia DMCR e MCR com os endereços trocados.**

- Em cada commit de delays, `factory-mmio-trace-v3.log` (v1 e v2 idênticos
  nesses pontos) mostra: leitura e reescrita de `0xb800101c`, 10 leituras de
  `0xb8001000` como pausa, poll de `0xb800101c`, toggle de `DACCR`
  (`0x80000010`→`0x80000000`→`0x80000010`) e, depois, um kick em
  `DPHY_CAL_CTRL` (256 no total). `0xb8001000` é escrito uma única vez.
- O SDK do RTL838x (`rtl838x/preloader/include/memctl.h` e
  `_memctl_update_phy_param()` em `plr_dram_gen2.c`) define `MCR=0xB8001000`,
  `DCR=0xB8001004` e `DMCR=0xB800101C`; o assembly deste repositório já usa
  esses valores.
- O `.c` tinha `DMCR=0xb8001000` e `MCR=0xb8001004`. Assim `phy_commit()`
  reescrevia o MCR (strap `0x56c00000`) em vez do DMCR, pausava lendo o DCR e
  esperava o bit 31 do MCR (sempre 0): o commit GPL nunca sincronizava os
  parâmetros com o PHY nem esperava a conclusão. Vale para `set_all`,
  `select_taps` e toda a calibração de DQM. É compatível com o ruído, o DQM
  instável e os centros estranhos (DQ16 com `0x1e`), mas isso é hipótese que
  V170 testa.
- V1xx..V169 usam a versão errada. A correção fica atrás de
  `SG1002_MR_PRELOADER_DDR_GPL_DMCR_101C`, então os scripts antigos continuam
  gerando as imagens já gravadas (V169 reconstruída: SHA idêntico ao gravado).
- Fábrica (trace) versus o nosso init, comparados no emulador: DCR, DTR0..2
  (`0x20320000`, `0x54433830`, `0x0404030f`, `0x0630d000`), DCDR
  (`0x8b540000`) e DACSPCR/DACSPAR (`0x7f`/`0`) coincidem. Diferem: MCR
  (`0x56000000` contra `0x560001e0`), `0xb800101c` final (`0` contra
  `0x00130000`), `0xb8001050` (`0x80000000` contra `0x80800000`),
  `0xb8001094` (`0x8000107c` contra `0x0000107c`) e DACCR (`0x80000010`
  contra `0x80000030`). Sobretudo, a fábrica escreve `DCDQMR` **uma vez** com
  `0x08000000`, e o nosso fluxo termina em `0x0f0f0000`/`0x0f090000`. Os taps
  DPHY finais do trace não servem de referência, porque no emulador o mapa de
  calibração é "tudo passa".

V170 roda a calibração com DMCR/MCR corrigidos e mede taxas de erro em vez
de pass/fail único:

- `R` como em V169. `D:<endereço>:<valor>`: registradores
  `0xb8001000..0x98` e `0xb8001500..0x90` no hardware real, para comparar
  com o trace de fábrica.
- Passe 0 com o DQM calibrado e passe 1 depois da sequência de DQM da fábrica
  (`Q:<DCDQMR>`: escreve `0x08000000`, toggle de DACCR, reescreve
  `0xb8001004=0x20320000`).
- `K:<passe>:<rodada>:<alvo>:<contagem>:<OR>:<bitmap>`: escreve `0x600` bytes
  uma vez e lê 1024 vezes a janela de 9 words em torno de cada word que
  falha (`+0x204..+0x404` e `+0x518`). Contagem é o número de leituras
  divergentes e o bit 4 do bitmap é o word alvo. Erro de escrita fica
  persistente; erro de leitura é esporádico.
- `F:<passe>:<n>:<contagem>:<OR>:<primeiro>:<último>`: uma cópia integral de
  `0x33000` bytes e três verificações.
- `S:<passe>:<qual>:<tap>:<contagem>:<OR>:<DCDQMR>`: varredura do centro de
  leitura de DQ19 (`qual=0`, janela em `+0x204`) e DQ16 (`qual=1`, `+0x518`),
  256 leituras por tap depois de latch, settle e cópia nova.

Validação no emulador (só de fluxo): 1 `R`, 76 `D`, 1 `Q`, 36 `K`, 6 `F` e
128 `S`, sem travar, com `G:00000000` e `Q:08000000`.

V170 foi gravada na SPI física em 2026-09-21 às 19:45:02, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A
(`logs/spi-write-20260921-194502.log`: `VERIFIED`). Imagem
`sg1002-mr-v170-error-rate-probe-16m.bin` (SHA-256
`1b4ff16473142d4ec274be39468ff4c07dca8beb5c9a5d5171404f523b9c40c8`).
Resultado físico V170 (log `codex-v171.log`; o `codex-v170.log` foi uma
captura ruim, descartada pelo usuário, e o firmware é o V170). `G:00000000`,
`DQM:11040000` (antes `0f0f0000`/`0f090000`), `R:001e1308:001e1308:11040000`.

- Com DMCR/MCR corrigidos a calibração passou a achar janelas reais e
  distintas por bit, em vez dos words degenerados `001e0f00`/`001e1e00` de
  V163–V169. DQ0–7: `071e150c`, `071e1308`, `071e150c`, `071e1206`,
  `071e1308`, `051e150c`, `081e1206`, `061e140a`. DQ16–23: `001e1308`,
  `001e140a`, `001e1308`, `001e1308`, `001e150c`, `001e140a`, `001e140a`,
  `001e1104`. DQ8–15 `1e000000`, DQ24–31 `0`. Os campos write (31:24) e
  read-start (7:0) **voltam na leitura** aqui; a ilegibilidade vista em
  V168/V169 acompanhava o commit sem sync.
- `F` (cópia integral, `0x33000` bytes = 52224 words): passe 0 (DQM
  calibrado) 16849/16839/16815 words errados, OR `0xff00ffff`, primeiro em
  `+0x200`, último em `+0x316c4`; passe 1 (DQM de fábrica) 16206/16194/16171,
  OR `0x0000ffff`. As três verificações da mesma cópia concordam em 0,2%: o
  erro é **persistente**, ou seja, o dado é gravado errado, não lido errado.
- `K` (1024 leituras de uma janela de 9 words): o word em `alvo-4`
  (`+0x200`, `+0x280`, `+0x300`, `+0x380`, `+0x400`) falha em 1024 de 1024
  leituras, sempre com o bit 13 (DQ5, OR `0x2000`); em `+0x518` três words
  falham, 2652 a 2926 de 9216 leituras, bits 0 e 11 (OR `0x0801`).
- `S`: nos dois passes o erro de DQ19 é exatamente 256 (um word persistente
  vezes 256 iterações) em **todos os 32 taps**, e DQ16 fica em 629 a 713 sem
  relação com o tap. A curva plana significa que os taps não foram aplicados:
  o `S` de V170 usava só `DPHY_CAL_CTRL`, sem o sync do DMCR e o toggle do
  DACCR. Isso invalida a V170 como varredura de centro e põe em dúvida os
  ensaios "latch" V147–V169, que também só usaram `DPHY_CAL_CTRL`.
- O DQM de fábrica (`0x08000000`) baixa os words errados de 16,8k para 16,2k
  e tira os erros do byte 3, mas não resolve.

Diferenças estruturais achadas comparando o nosso init com o trace de fábrica
(emulador; valores escritos pelo nosso código, não timing):

- `SG1002_MR_PRELOADER_DDR_GPL_SDK_MODE_PROBE` (ligado em V1xx–V170, linhas
  ~1753 do `.S`) grava `DIDER=0x80800000`, `DACCR=0x80000030` e
  `MCR |= 0x1e0`. No SDK isso só ocorre com clock de DRAM acima de 300 MHz
  (`DDR_Calibration()` em `plr_memctl_cali_dram.c`); o trace de fábrica termina
  com `DACCR=0x80000010` e `DIDER=0x80000000` e escreve o MCR uma vez
  (`0x56000000`). Sem o probe, MCR, DCR, DTR0..2, DCDR, DACSPCR/DACSPAR e
  DACCR do nosso init batem com a fábrica.
- `DIDER`: a fábrica grava `0x80000000`; com `DDR3_MRS_BASE` o nosso init
  grava `0`.
- `DMCR` termina em `0x00130000` (MRS de MR3 com valor 0, reexecutado a cada
  commit, benigno); a fábrica o mantém em 0.
- `DDZQPCR` bate: `0x8000107c` no trace é uma escrita (inicia o ZQ) e a
  leitura seguinte é `0x0000107c`.
- O mapeamento de bytes de `record()` é igual ao do SDK (OR dos bits
  corretos); não é bug.

V171 (sem `SDK_MODE_PROBE`, com DMCR/MCR corrigidos) mede, por lane (A =
bytes 3 e 1 = DQ0–7; B = bytes 2 e 0 = DQ16–23), quantos words do payload
ficam errados sob configurações cumulativas no estilo da fábrica:

- cfg0 como calibrado; cfg1 mais DQM de fábrica (`Q:`); cfg2 mais fase de
  escrita estática (lane A 10, lane B 0, como o SDK e o trace; `P:`); cfg3
  mais `DIDER=0x80000000` (`I:`). `F:<cfg>:<n>:<errados>:<lane A>:<lane B>:<OR>`.
- Em cfg1 e cfg3, varredura do campo write (31:24) e do centro de leitura
  (15:8) de cada lane, cada tap aplicado com o commit correto
  (`phy_delay_commit`: DMCR e toggle de DACCR) e o DQM de fábrica reaplicado,
  copiando `0x2000` bytes por tap:
  `S:<cfg>:<lane | campo<<1>:<tap>:<errados>:<A>:<B>:<OR>:<DCDQMR>`.
- `R` e `D` como em V170.

Validação no emulador (só de fluxo e estado estrutural): 1 `R`, 76 `D`, 1
`Q`, 1 `P`, 1 `I`, 8 `F`, 256 `S`. No emulador `P:0a1e0f00:001e0f00` reproduz
os DPHY finais do trace de fábrica, e o estado estrutural (MCR, DCR, DTR0..2,
DCDR, DACSPCR/DACSPAR, DACCR) bate; só o `DIDER` difere antes do passo cfg3.

V171 foi gravada na SPI física em 2026-09-21 às 20:15:52, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A
(`logs/spi-write-20260921-201552.log`: `VERIFIED`). Imagem
`sg1002-mr-v171-factory-cfg-probe-16m.bin` (SHA-256
`ddb14be64788095fae6afcec5f3e62e27ad773d8cb7ef5fac8df4518029adb64`).
Resultado físico V171 (log `codex-v172.log`; o `codex-v171.log` foi uma
captura parcial e o firmware é o V171). Chip informado pelo usuário: Winbond
W631GG8NB-12; o SDK e a nossa tabela estática são do perfil W631GG8KB-15.
`G:00000025` (sem o probe SDK a calibração reprova DQ0, DQ2 e DQ5),
`DQM:0f0f0000`, `R:001e1308:001e140a:0f0f0000`.

- `F` (words errados de 52224; lane A = bytes 3,1; lane B = bytes 2,0):
  cfg0 calibrado 7538/7501 (A 3668, B 3941); cfg1 DQM de fábrica 7515/7507;
  cfg2 mais fase de escrita estática (A 10, B 0) 6545/6557 (A 2985, B 3598);
  cfg3 mais `DIDER=0x80000000` em tempo de execução 38665/38647 (todos os
  bytes). Remover o probe SDK derrubou os erros de 16,8k para 7,5k (14%). O OR
  é `0x0000ffff` em cfg0–cfg2: só a metade baixa da palavra (bytes 1 e 0, a
  segunda beat do burst) erra; bytes 3 e 2 nunca.
- `S` (agora com o commit correto, os taps aplicam: as curvas são suaves;
  2048 words por tap): centro de leitura, lane A, é 2048 (tudo errado) nos
  taps 0–15, 672 no tap 22 e cai monotonicamente até 222 no tap 31; lane B cai
  de 1552 no tap 0 para 145 no tap 31 (só 24 erros da própria lane B). O
  melhor tap é o último possível nas duas lanes, então o olho de leitura fica
  além do alcance de 5 bits e a janela da calibração satura em `end=0x1e`. A
  fase de escrita da lane A fica em ~300 nos taps 0–16 e piora de 17 a 31
  (1050); a da lane B é plana (286–296). O piso de ~14% não depende da escrita.
  `DCDQMR` ficou `08000000` em todas as linhas (o commit correto não derruba o
  DQM quando ele é reaplicado). Em cfg3 tudo é ~1500 errados.

Achados posteriores, sem gravação:

- O kernel do OpenWrt (`target/linux/realtek/files-6.18/drivers/clk/realtek/`:
  `clk-rtl83xx.h`, `clk-rtl83xx.c`, `clk-rtl838x-sram.S`) não calibra o PHY de
  DDR, mas mapeia o PLL: `RTL_SW_CORE_BASE 0xbb000000`, `PLL_GLB_CTRL 0xfc0`,
  `CPU_CTRL0/1 0xfc4/0xfc8`, `LXB_CTRL0/1 0xfd0/0xfd4`, `MEM_CTRL0/1
  0xfdc/0xfe0`, a fórmula do clock e a tabela `rtcl_838x_mem_reg_set`. A
  entrada de 200 MHz é `0x041bc`/`0x14018C80`, e o preloader de fábrica escreve
  exatamente esses valores; a CPU é `0x4748`/`0x0c14530e` (500 MHz). Portanto a
  DRAM da fábrica roda a **200 MHz**, e o nosso init escreve os mesmos valores
  de PLL (mesmo clock). O comentário do kernel diz que o DDR3 tem limite
  inferior de 303 MHz, ou seja, 200 MHz está abaixo do modo DLL ligado. Também
  confirma `DCR` com largura de barramento 8 (`0x20320000`) e DDR3 pelo MCR.
- `plat_memctl_calculate_dqrf_delay()` do SDK empacota o word como o nosso
  `.c` (write 31:24, fim 23:16, centro 15:8, início 7:0); não é bug.
- O emulador roda o nosso firmware com `--mmio-trace` (a saída vai para o
  stderr, então usar `2>&1`) e permite o diff ordenado dos writes de init
  contra o trace de fábrica, ignorando PCs e UART. Antes da primeira palavra de
  DPHY são 27 writes; V171 coincidia em 25 e diferia em: `DIDER` (fábrica
  `0x80000000`, nosso `0`), os **quatro comandos MRS** via DMCR (`0x00101220`,
  `0x00110042`, `0x00120400`, `0x00130000`, do flag `DDR3_MRS_BASE`), que a
  fábrica nunca emite, e um segundo `DACCR=0x80000010` que a fábrica faz no
  lugar deles. A fábrica deixa a init de hardware (`DMCR=0x01000000`) programar
  a DRAM a partir de DCR/DTR. Hipótese: os nossos MR0/MR1/MR2 (CL 6, WR 5, DLL
  ligado, CWL 5) sobrescrevem os do hardware, e uma diferença de CL/CWL de um
  ciclo a 200 MHz (5 ns) desloca o olho de leitura e de escrita por um ciclo
  inteiro, fora do alcance dos taps.

V172 é a V171 com o init idêntico ao da fábrica: `FACTORY_NO_MRS` no lugar de
`DDR3_MRS_BASE` (sem MRS, `DIDER=0x80000000` e `DACCR=0x80000010` no lugar dos
comandos). Mesmo bloco de probe da V171 (`R`, `D`, `Q`, `P`, `I`, `F`, `S`).
No emulador os 27 writes de init são **idênticos** aos da fábrica e o estado
estrutural (MCR, DCR, DTR0..2, DIDER, DCDR, DACCR, DACSPCR/AR, DMCR) bate;
`G:00000000`, 256 `S`, 76 `D`, 8 `F`. A V171 reconstruída mantém o SHA gravado.

V172 foi gravada na SPI física em 2026-09-21 às 20:41:53, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A
(`logs/spi-write-20260921-204153.log`: `VERIFIED`). Imagem
`sg1002-mr-v172-factory-init-probe-16m.bin` (SHA-256
`655bbfc2801a4f50dec243a0507e5a61771f5d1269e9e5783605fbcf4b1c9bdf`).
Resultado físico V172 (log `codex-v173.log`; o `codex-v172.log` dessa rodada
era apenas a reprodução, pelo cliente TTL, do buffer com a saída antiga da V171:
dados idênticos e `DMCR=0x00130000`). O dump `D` do `codex-v173.log` mostra o
init novo (`DMCR=0`, `DIDER=0x80000000`) e a DRAM **morta**: `G:00ff00ff` (os 16
bits reprovam), `DQM:00000000`, DPHY todo zero e **52222 de 52224 words
errados** em todas as configurações. Sem os comandos MRS a memória não funciona,
então a hipótese "os nossos MRS sobrescrevem os do hardware" está refutada.

Por que o trace do emulador não é o caminho do hardware real (achado
posterior, sem gravação):

- O código de init da fábrica (offset `0x1148` do dump) faz
  `DMCR = (DMCR & 0xfeefffff) | 0x01000000`, lê o ID do chip em `0xb80010fc` e
  ramifica: `0x0399xxxx`/`0x8389xxxx` fazem `DACCR |= 0x40000000` e
  `DDZQPCR = arg | 0x40000000`; `0x6275xxxx`/`0x6290xxxx` chamam a função em
  `0x9f0019a0` (é DDR3?) e fazem `0xb8003a00 |= 0xb` (ou `|= 0x3`) antes de
  `DDZQPCR = arg | 0x80000000`; qualquer outro ID só grava o `DDZQPCR`. O SDK
  (`memctlc_ZQ_calibration()` em `plr_dram_gen2.c`) confirma
  (`MEMCTL_SOCPNR_6275 = 0x62750000 /* 8380 Real chip */`), acrescenta a
  **calibração longa de ZQ do DDR3** (`D3ZQCCR = 0xB8001080`, bit 31, espera de
  100 leituras de MCR e poll) e chama `DPCR = 0xB8003A00`. No emulador o ID é 0
  e o tipo de DRAM não é DDR3, então nada disso aparece no trace, e o nosso init
  também não faz. O `MCR |= 0x1e0` (`memctlc_DBFM_enable`) só existe no SDK se o
  clock de memória for maior que 200 MHz; o nosso é 200.
- O SDK `dram_setup()` faz, nesta ordem: `MCR` (RMW), `DIDER`, `DACCR`,
  `DACSPCR/AR`, clock da DRAM, `DCR`, `DTR0..2`, ZQ (refresh desligado, `DPCR`,
  ZQ automático, refresh ligado, ZQCL), DBFM se >200 MHz, calibração (que começa
  com `memctlc_ddr3_dll_reset()`) e o toggle de `DACCR`.
- `memctlc_ddr3_dll_reset()` do SDK: MR1 com DLL desligado, MR1 com DLL ligado,
  **MR0 com reset de DLL**, espera de `0x800`, MR1, MR2, MR3 e o toggle de
  `DACCR`. O nosso `DDR3_MRS_BASE` emite só quatro comandos, sem essa sequência.
- O bloco de parâmetros de DRAM da fábrica (`dram_gen2_info_t` do SDK) está no
  dump em **`0x737c`**: `dcr=0x20320000`, `dtr0=0x54433830`, `dtr1=0x0404030f`,
  `dtr2=0x0630d000`, `mpmr0=0x0f3ff3ff`, `mpmr1=0xff000000`, `dider=0x80000000`,
  `daccr=0x80000010`, `dacspcr=0x7f`, `dacspar=0`, **`DDR3_mr0=0x00101220`,
  `mr1=0x00110040`, `mr2=0x00120400`, `mr3=0x00130000`**, `static_cal_data` de
  DQ0–7 `0x0a1e0f00` (fase de escrita 10) e de DQ8–31 `0x001e0f00`,
  `static_cal_data[32]=0x08000000` (o DQM estático que a fábrica grava),
  `zq_setting=0x107c`, `calibration_type=1` (calibração por software),
  `tx_clk_phs_delay=17`, `clkm_delay=13`, `clkm90_delay=10`,
  `auto_calibration=0`, `drv_strength=0`. Diferenças para o nosso
  `DDR3_MRS_BASE`: `MR1` é `0x0040` (RZQ/6), não `0x0042` (RZQ/7); `MR0`, `MR2`,
  `MR3` coincidem.

V173 segue o SDK e a fábrica com esse bloco de parâmetros, atrás de
`SG1002_MR_PRELOADER_DDR_SDK_DDR3_INIT` (desligado em todos os scripts
existentes; V170, V171 e V172 reconstruídas mantêm os SHA gravados):
`DIDER=0x80000000`; `DPCR |= 0xb` antes do ZQ automático; ZQCL
(`D3ZQCCR |= 0x80000000`, 100 leituras de MCR, poll limitado que só continua em
timeout); e, no lugar dos quatro MRS, o macro `gpl300_ddr3_dll_reset` (MR1 DLL
off, MR1 DLL on, `0x00101320`, espera, `0x00110040`, `0x00120400`,
`0x00130000`, toggle de `DACCR`), cujos valores são exatamente os da fábrica. Sem
`SDK_MODE_PROBE` e sem DBFM. O bloco de probe é o mesmo de V171 mais a linha
`Z:<DPCR>`. No emulador: `G:00000000`, `Z:0000000b`, 256 `S`, 76 `D`, 8 `F`.

V173 foi gravada na SPI física em 2026-09-21 às 21:07:45, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A
(`logs/spi-write-20260921-210745.log`: `VERIFIED`). Imagem
`sg1002-mr-v173-sdk-ddr3-init-probe-16m.bin` (SHA-256
`6c3e3c8002696cbd7cc23fd6eaf31a6d6819071d23cf92891235c2f32404b612`).
Resultado físico V173 (log `codex-v174.log`). `G:00000000`, `DQM:0f0f0000`,
`R:001e0f00:001e0f00:0f0f0000`, `Z:0000000b` (confirma que `DPCR |= 0xB`
aplicou e voltou na leitura). `F` deu **zero palavras erradas nas quatro
configurações, nas duas verificações**: cfg0 (DQM calibrado), cfg1 (DQM de
fábrica), cfg2 (fase de escrita estática) e cfg3 (`DIDER` em runtime). Os
scans `S` (write e centro de leitura, das duas lanes, sob cfg1 e cfg3) deram
**zero erro nos 32 taps inteiros** — a cópia de `0x33000` bytes está correta
em qualquer ponto medido, não só no centro. O init do SDK com o bloco de
parâmetros da fábrica (`DPCR`, ZQCL, `DIDER`, MRS com reset de DLL) resolve o
problema: a hipótese de causa raiz levantada em V172 está confirmada.

Com a DDR provadamente correta, o próximo passo natural é o salto ao U-Boot.
Antes disso, uma auditoria do endereço de entrada achou um erro presente
desde os primeiros testes de cópia (V109 em diante): `GPL_UBOOT_DDR_LOAD`
(`0xa1c10000`) e `GPL_UBOOT_DDR_ENTRY` (`0x81c10000`) eram só um **slot de
teste** arbitrário dentro da DRAM, usado para verificar integridade de cópia,
nunca o endereço real de link do U-Boot GPL. `build-gpl/u-boot-factory-payload-v22/System.map`
mostra `83f01000 T _start`, e `.config` confirma `CONFIG_TEXT_BASE=0x83f01000`
— consistente com o cabeçalho deste documento (linha 12), que desde o início
diz que o Stage-2 GPL "é carregado em `0x83f01000`". O código nunca foi
atualizado para bater com isso. `0x83f01000` é KSEG0 (físico `0x03f01000`,
cacheado); o endereço de carga sem cache correspondente é KSEG1
`0xa3f01000`. A correção fica atrás de `SG1002_MR_PRELOADER_DDR_GPL_UBOOT_REAL_ADDR`,
que redefine `GPL_UBOOT_DDR_LOAD`/`ENTRY` só quando ligado; nenhum script
existente usa esse flag, e V170–V173 reconstruídas continuam com os SHA
gravados.

V174 é a V173 sem o bloco de sondas, com o endereço de entrada corrigido e o
`SG1002_MR_PRELOADER_DDR_V21_HANDOFF_TRACE` (copia os `0x33000` bytes,
verifica ponta a ponta e por palavra, desarma o watchdog, faz o latch por
`DPHY_CAL_CTRL` e salta). Antes de gravar, testada no emulador com o
`u-boot.bin` sobreposto em `0x050000`: **o U-Boot GPL sobe até o prompt**,
`U-Boot 2026.07 ... Model: RTL8380M_INTPHY_2FIB_1G_DEMO ... DRAM: 128 MiB
... RTL838x#`. O `Wrong Image Type for bootm command` que segue é esperado —
não há kernel nenhum ainda no offset de boot padrão do U-Boot; o autoboot
apenas tentou.

**Mudança de escopo, autorizada explicitamente pelo usuário nesta conversa**
(2026-09-21, resposta a uma pergunta direta: "pode fazer o mais hardcore q
vc precisar, temos backup do original"): pela primeira vez nesta sessão, a
gravação cobre também a região Stage-2 (`0x050000..0x082fff`), com o
`u-boot.bin` de `build-gpl/u-boot-factory-payload-v22` (idêntico ao usado no
teste do emulador), preenchendo o restante do slot de `0x33000` bytes com
`0xff`. Antes de gravar, confirmou-se byte a byte que a imagem de 16 MiB só
difere do dump de fábrica dentro de `0x000000..0x0076bb` (Stage-1) e
`0x050000..0x082fff` (Stage-2); nenhum outro byte muda. Isso é consistente
com a regra original deste documento (linha 17: "escrever somente as duas
regiões acima e verificar ambas"), mais restrita que o "somente Stage-1" do
prompt relayado nesta conversa — a restrição do prompt era uma cautela
adicional para a fase de calibração de DDR, já superada pelo resultado
zero-erro da V173.

V174 foi gravada na SPI física em 2026-09-21, em duas chamadas separadas,
cada uma com `--confirm-write`, `--expect-sha256` e verificação pelo
CH341A: Stage-1 às 21:31:02 (`logs/spi-write-20260921-213102.log`:
`VERIFIED`) e Stage-2 às 21:31:29 (`logs/spi-write-20260921-213129.log`:
`VERIFIED`). Imagem `sg1002-mr-v174-sdk-ddr3-boot-with-uboot-16m.bin`
(SHA-256 `f1847bc59ddb7f022391523ee98fa41c31883c9b68ac468e7769d9a005d794b6`),
mesma imagem para as duas gravações. Resultado físico V174 (log TTL, único; não houve captura ruim desta vez):

```
!JUMP!
L
```

O handoff mecânico funcionou de novo (`!COPY!`→`!COPY_OK!`→`!FULL_OK!`→
`!LATCH!`→`!LATCH_OK!`→`!JUMP!` já tinham aparecido antes disso). Depois do
salto, o U-Boot GPL emitiu só `L` (de `lowlevel_init()`,
`arch/mips/mach-rtl-otto/cpu.c:24`) e silêncio total — nem mesmo o marcador
`F` ("entering board_init_f"), que é incondicional e roda duas instruções
depois de `lowlevel_init()` retornar (`arch/mips/cpu/start.S:320`).

Investigação (sem gravação): a hipótese inicial era troca de baudrate por
`mips_sram_init` (`arch/mips/mach-rtl-otto/sram_init.S`, que reprograma a
UART0 para 9600), mas isso foi descartado — `${CROSS_COMPILE}nm
start.o` confirma que esse símbolo nem está referenciado no `.o` compilado,
porque `CONFIG_INTELBRAS_SG1002_MR_FACTORY_PAYLOAD=y` já desliga essa
chamada (`.config` do build `u-boot-factory-payload-v22`, confirmado por
`grep`). O board já tinha, desde antes desta sessão, um flag Kconfig
dedicado a esse cenário exato (chainload por Stage-1 GPL): quando ligado,
`start.S` usa um caminho separado com trace via `sg1002_stage2_trace`
(macro em `arch/mips/cpu/start.S:81`, grava um word cru em `0xb8002000`) e
`common/board_f.c` tem `sg1002_init_trace()`, com um marcador de um
caractere por `INITCALL` de `initcall_run_f()` (linhas 953–1092: `U`, `V`,
`W`, `B`, `S`, `X`, `Y`, `C`, `D`, `E`, depois `0`–`9`,`a`–`e`) e dois em
`board_init_f()` (`P` logo após o prólogo do compilador, `D` depois de `gd`
ficar gravável — linhas 1126–1145). Essa trilha já existia no código, mas
`CONFIG_MIPS_INIT_STACK_IN_SRAM=y` (também já no `.config`) desativa os
únicos traces de `start.S` que rodam *antes* de `board_init_f` (`S`, `G`,
`U`/`u` condicionados a `#ifndef CONFIG_MIPS_INIT_STACK_IN_SRAM`), deixando
um vão sem instrumentação entre `L` e `F`.

Adicionados seis novos marcadores incondicionais em `start.S`, usando a
mesma macro `sg1002_stage2_trace`: `0` (entrada em `reset:`, antes de
qualquer coisa), `1`/`2` (antes/depois de `setup_stack_gd`, caminho SRAM;
`CONFIG_CUSTOM_SYS_INIT_SP_ADDR=0xbf00fe00`, dentro da SRAM interna do
chip), `3`/`4` e `5`/`6` (antes/depois de `lowlevel_init()`, nos dois
caminhos possíveis de `CONFIG_SYS_MIPS_CACHE_INIT_RAM_LOAD`). Não altera
nenhum script do preloader Stage-1 (linguagem C/assembly do U-Boot GPL,
fora de `arch/mips/mach-rtl-otto/preloader/`).

Compilar o U-Boot GPL exigiu `bison`, `flex` e `m4` no host, ausentes neste
ambiente e sem `sudo`. Resolvido com `apt-get download` (sem instalar) e
`dpkg-deb -x` para extrair os três pacotes num diretório local, usados via
`PATH`/`BISON_PKGDATADIR`/`M4` apontando para os binários extraídos — sem
tocar no sistema. `CONFIG_TOOLS_LIBCRYPTO` (e as opções de assinatura FIT
que dependem dela) foi desligada só no `.config` deste diretório de build
(`scripts/config -d ...`), porque faltava `libssl-dev` no host e essas
ferramentas de host não afetam o `u-boot.bin` final. Build:
`configs/intelbras_sg1002_mr_factory_payload_defconfig`, diretório
`build-gpl/u-boot-v23-trace`.

Sequência completa no emulador, depois de `!JUMP!` (com o U-Boot overlaid em
`0x050000`, sem timing real de hardware):

```
0 1 2 5 L 6 F P D U V W B S X Y C D E 0 1 2 3 4 5 6 7 8 9 a b c d e ...
```

(o `B` duplicado no dump bruto é ruído do modelo de UART do QEMU, que não
espera THRE nesses `sg1002_stage2_trace`/`sg1002_init_trace`; a ordem dos
demais caracteres bate exatamente com o código-fonte.) `0`,`1`,`2`,`5`,`6`
aparecem duas vezes cada — antes de `F` são os novos marcadores de
`start.S`; depois de `F` são os marcadores pré-existentes de
`initcall_run_f`, que reaproveitam os mesmos caracteres. A posição relativa
a `F` desambigua.

V175 é a V174 com esse U-Boot GPL recompilado (Stage-1 idêntico byte a
byte ao já gravado; só o binário do slot Stage-2 muda). Antes de gravar,
confirmado que os primeiros 32 KiB da imagem batem exatamente com os já
gravados da V174 (Stage-1 não precisou ser regravado) e que, como sempre,
nenhum byte fora das janelas de Stage-1/Stage-2 muda.

V175 foi gravada na SPI física em 2026-09-21 às 21:52:06, apenas na região
Stage-2 (`0x050000..0x082fff`, o Stage-1 já gravado pela V174 continua
válido), com `--confirm-write`, `--expect-sha256` e verificação pelo
CH341A (`logs/spi-write-20260921-215206.log`: `VERIFIED`). Imagem
`sg1002-mr-v175-uboot-trace-16m.bin` (SHA-256
`8cf1673be1004c481e953794dec6a6beb266ddd886d15a4812b91812cc73a161`).
Resultado físico: pendente, aguardando o log TTL.

**Achado adicional, importante para interpretar o próximo log**: ao preparar
um script reprodutível para a V175, `make intelbras_sg1002_mr_factory_payload_defconfig`
gerou um `.config` **diferente** do que de fato foi usado para compilar
`u-boot-factory-payload-v22` em diante (que eu copiei via `cp -a` para gerar
V174/V175). O `defconfig` versionado tinha `CONFIG_TEXT_BASE=0x81c10000`,
com o comentário: *"Keep the first GPL payload in the low DDR window used
by the observed boot path. The 0x83f... placement fails the Stage-1
full-copy check."* Isso vem do commit `535ba1bc` ("boot: place SG1002 GPL
payload in low DDR", 2026-09-20 14:31), que reverteu tanto o preloader
quanto o defconfig de `0x83f01000`/`0xa3f01000` para
`0x81c10000`/`0xa1c10000` — exatamente o par que eu já tinha corrigido de
volta ao preparar a V174, sem saber desse histórico.

`git log` mostra `535ba1bc` logo antes de `eeae6cd3` ("preloader: verify
full GPL stage2 copy"), que é a V109 já registrada na tabela cronológica
deste documento (linha 33): *"A verificação integral dos `0x33000` bytes
falhou antes do salto; o Stage-2 é corrompido durante a cópia para DDR."*
Ou seja, a falha que motivou a reversão para `0x81c10000` foi observada
numa época em que a calibração de DDR estava **fundamentalmente quebrada**
(o bug de DMCR/MCR trocados, só corrigido em V170, dois dias depois) — a
cópia corrompia em qualquer endereço, não especificamente em `0x83f01000`.
A V173 (2026-09-21) mediu zero erros de cópia em toda a faixa testada, e o
`!COPY_OK!`/`!FULL_OK!` já visto no log físico da V174 confirma que a
cópia para `0x03f01000` bateu byte a byte àquele momento. Isso não é uma
prova de que a região seja tão estável quanto a testada em `0x01c10000`
(nunca fizemos lá a mesma variação de configurações e taps que fizemos no
slot de teste antigo) — só um indício forte de que o motivo original de
`535ba1bc` não se aplica mais.

Corrigido `configs/intelbras_sg1002_mr_factory_payload_defconfig` para bater
com o `.config` real (`CONFIG_TEXT_BASE=0x83f01000`,
`CONFIG_CUSTOM_SYS_INIT_SP_ADDR=0xbf00fe00`, `CPU_MIPS32_R2`,
`MIPS_CACHE_SETUP`/`DISABLE` ligados, `ENV_IS_IN_SPI_FLASH` com
offset/tamanho/setor explícitos), via `make savedefconfig` no diretório de
build que gerou a imagem já gravada, com um comentário novo explicando essa
investigação. `tools/build-sg1002-mr-v175-uboot-trace.sh` reconstruído do
zero, só com o defconfig e o ambiente de host (`HOSTTOOLS_DIR`), reproduz o
binário gravado a menos de 3 bytes (o timestamp de compilação embutido no
banner do U-Boot); confirmado byte a byte contra a imagem realmente gravada.

**Se o próximo log não avançar além de `L`** (ou não chegar nem a `0`, o
primeiro marcador, que roda antes de qualquer instrução C), a hipótese
`535ba1bc` volta à mesa: reverter `GPL_UBOOT_DDR_LOAD`/`ENTRY` e o
`CONFIG_TEXT_BASE` para `0xa1c10000`/`0x81c10000`, mantendo toda a
instrumentação desta V175, e testar de novo no mesmo slot antigo (já
validado pela calibração da V173) antes de suspeitar de qualquer outra
causa relacionada ao endereço.

Resultado físico V175 (log via `serial_ttyUSB0.log`; o `codex-v174.log`
dessa rodada era o buffer reproduzido pelo cliente TTL): **grande avanço**.
A trilha completa, repetida de forma idêntica em pelo menos 7 reinícios
seguidos: `0 1 2 5 L 6 F P D U V W B S X Y C D E 0 1`, e então volta o
banner de reset. Nenhuma mensagem de erro. Decodificado pela legenda:
entrou em reset, montou a pilha, rodou `lowlevel_init`, entrou em
`board_init_f`, `gd` gravável, passou `checkcpu`, `dram_init` retornou sem
erro (`D`→`E`), `setup_dest_addr` (`0`) e `reserve_round_4k` (`1`) também —
e para exatamente ali, antes de `setup_relocaddr_from_bloblist` (`2`).
`hang()` é um loop infinito silencioso, sem watchdog do U-Boot ligado
(`# CONFIG_WATCHDOG is not set`); o reinício automático observado é mais
consistente com uma exceção de CPU real (branch para endereço inválido)
do que com um `hang()` alcançado normalmente.

Hipótese levantada: a pilha inicial (`CONFIG_CUSTOM_SYS_INIT_SP_ADDR=
0xbf00fe00`) fica na SRAM interna do chip, a mesma SRAM onde o código do
Stage-1 preloader continua fisicamente presente (nunca apagado, só
paramos de executar dele: `0x9200` bytes em `SRAM+0x1200..+0x76bc`), e
cujo tamanho real este projeto nunca confirmou. V176 testou mover a pilha
para a DRAM (`CFG_SYS_INIT_SP_ADDR` original do board, `0x80100000`),
mantendo toda a instrumentação — mas essa mudança **regrediu mesmo no
emulador** (DRAM perfeita ali), parando bem mais tarde (depois de todas as
`reserve_*`, `...abcdefghijklm`, sem nem alcançar o primeiro `putc('R')`
de `relocate_code()`). Como o emulador não reproduz o problema físico de
qualquer forma (nunca fez, em nenhuma das versões anteriores) e essa
mudança especificamente quebrou ali, **não foi gravada** na placa.
A pilha foi revertida para `0xbf00fe00`, mantendo os novos marcadores
(`f`..`n`, mais o `R`/`C`/`F`/`J` já existentes dentro de
`arch/mips/lib/reloc.c`); com a pilha revertida mas os traços novos
presentes, o mesmo comportamento (parar em `...abcdefghijklm`) ainda
ocorreu no emulador — ou seja, aquela regressão específica do emulador não
era da pilha, e sim de outra coisa introduzida junto (não investigada:
não importa para o teste físico, que trava bem antes desse ponto de
qualquer forma). **O emulador não deve ser usado para decidir o próximo
passo aqui** — ele nunca reproduziu o ponto de falha físico em nenhuma
versão, e essa investigação mostra que ele pode divergir do hardware real
de formas que levam a conclusões erradas.

V176 (pilha revertida, instrumentação `f`..`n`) foi gravada e confirmada
fisicamente **exatamente no mesmo ponto** que V175 (`...CDE01`, sete
reinícios seguidos, idênticos) — confirma que o ponto de parada é robusto
e que a instrumentação nova (que só atua bem depois desse ponto) não
interfere nele.

V177 decompõe manualmente a chamada única em jogo entre `1` e `2`
(`setup_relocaddr_from_bloblist()`, que só chama
`reserve_video_from_videoblob()` — no-op confirmado, `CONFIG_VIDEO` e
`CONFIG_SPL_VIDEO_HANDOFF` desligados): `p` antes da chamada, `q` logo após
o retorno (antes mesmo de checar o valor de retorno), depois `2` como
sempre. Isola três casos: nunca sai de `1` (não chega a `p`, improvável já
que são as próximas instruções); chega a `p` mas nunca retorna da chamada
(trava dentro ou no `jr`/retorno da função); chega a `q` mas trava depois
(o problema estaria na checagem do retorno ou logo antes de `2`).

V177 foi gravada na SPI física em 2026-09-21 às 22:30:30, apenas na região
Stage-2 (Stage-1 idêntico ao já gravado, confirmado byte a byte, sem
necessidade de regravar), com `--confirm-write`, `--expect-sha256` e
verificação pelo CH341A (`logs/spi-write-20260921-223030.log`: `VERIFIED`).
Imagem `sg1002-mr-v177-uboot-bloblist-boundary-16m.bin` (SHA-256
`dd82a140be6179dfb07ef1bc155e72466c72797e6afeee5468d04ff5b28268bb`).
Resultado físico V177 (log via TTL, 2026-09-22, dois reinícios capturados,
idênticos): `...CDE01` e nada mais — **nem `p`**, que deveria ser a
próxima instrução depois de `1` (só um `IS_ENABLED()` constante em tempo
de compilação, e um `sw` cru na UART). Isso descarta a hipótese de que a
trava estivesse dentro da chamada de `setup_relocaddr_from_bloblist()` ou
no seu retorno — a distância de código entre `1` e `p` é praticamente
nula, então a parada continua na mesma vizinhança de sempre, só que agora
sabemos que nem esse único passo a mais é alcançado (ou, alternativa mais
provável: `p` foi escrito no registrador da UART mas o reset cortou a
transmissão física antes de o byte sair pelo fio — `sg1002_init_trace()`
não espera THRE).

Achado de código (sem gravação): `board_late_init()` em
`board/intelbras/sg1002_mr/sg1002_mr.c` desarma o watchdog de novo
(`WDT_INTR_CLEAR` + `WDT_CTRL_DISABLED`, mesmos endereços e valores que o
Stage-1 já usa antes do salto — `0xb8003154`/`0xb8003158`,
`0xc0000000`/`0x1`, confirmados idênticos). Só que `board_late_init()` só
roda muito depois deste ponto, já em `board_init_r`, pós-relocação. Não
há nenhuma outra escrita a esses dois endereços em lugar algum do
código-fonte entre o `!WDT_OFF!` do Stage-1 e ali — então não há evidência
de que algo *reative* o watchdog explicitamente. Ainda assim, o
reinício automático e determinístico, sem nenhuma mensagem de erro (e
`CONFIG_WATCHDOG` desligado, então `hang()` sozinho — loop infinito
silencioso — não explicaria um reset automático), é mais consistente com
um temporizador de hardware disparando em tempo fixo do que com um erro
de lógica dependente de instrução.

V178 testa exatamente isso: logo depois de `1`, um laço de espera fixo de
5.000.000 de iterações (`volatile`, imune a otimização — confirmado no
desassemblado: `'z'` → monta a constante `0x4C4B40` em duas instruções →
laço → `'Z'`), antes de prosseguir para `p`/a chamada/`q`/`2` como antes.
Resultado esperado e o que cada um significa:

- Reinicia sempre no **mesmo ponto do laço** (a contagem, se fosse
  visível, seria sempre igual): favorece fortemente um temporizador de
  hardware com timeout fixo desde algum marco anterior (o mais provável,
  o próprio boot/reset do SoC ou o handoff do Stage-1).
- **Sobrevive ao laço inteiro** e chega em `Z`: descarta um timeout fixo
  simples; o problema volta a apontar para algo ligado à execução em si
  (código, pilha, ou um evento disparado por uma instrução específica
  logo depois).
- Reinicia **sem sequer chegar em `z`**: o problema está estritamente
  entre `1` e o início do laço, sem relação com o delay.

V178 foi gravada na SPI física em 2026-09-22 às 07:55:41, apenas na região
Stage-2 (Stage-1 idêntico ao já gravado, sem necessidade de regravar), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-075541.log`: `VERIFIED`). Imagem
`sg1002-mr-v178-uboot-delay-probe-16m.bin` (SHA-256
`2c4d5221822d1aefac8fc3233c259a6a2ff803d939dada8a2de8e5e5dbdcf288`).
Resultado físico V178 (log TTL, 2026-09-22, dois reinícios capturados,
idênticos): **passou pelo laço inteiro**. Sequência completa:
`0125L6FPDUVWBSXYCDE01` + `Z` (sem `z` visível — provavelmente sobrescrito
no registrador da UART pela escrita seguinte antes de sair fisicamente
pelo fio; `sg1002_init_trace()` não espera THRE) + `pq23456789abcde`, e só
então reinicia. Isso é decisivo contra um temporizador de tempo fixo desde
o boot: se fosse esse o mecanismo, o laço de 5 milhões de iterações
(que consome bem mais tempo que todo o código anterior) deveria ter
disparado o reset *durante* o laço, não centenas de instruções depois
dele. Em vez disso, o boot avançou por **toda** a sequência de
`reserve_*` (`2` até `e`, incluindo a chamada que V177 isolou) e parou de
novo — no mesmo padrão de antes: logo depois de um marcador (`e`,
`reserve_stacks`), antes do próximo (`f`, `dram_init_banksize`), sem
mensagem de erro.

`dram_init_banksize()` é `__weak`/genérica (nenhuma implementação
específica do board) e só escreve dois campos em `gd->bd->bi_dram[0]`;
`gd->bd` já foi alocado e zerado com sucesso em `reserve_board()` (`8`),
bem antes. Não há nada nessa função que devesse falhar.

V179 generaliza a técnica que funcionou em V178: uma macro
`sg1002_init_delay()` (laço de 2.000.000 de iterações, mesmo padrão
`volatile`) inserida antes de **todo** `INITCALL` restante até
`jump_to_copy()` (antes de `dram_init_banksize`, `show_dram_config`,
`setup_bdinfo`, `display_new_sp`, `reloc_fdt`, `reloc_bootstage`/
`reloc_bloblist`, `setup_reloc`, `clear_bss`, `cyclic_unregister_all` e
`jump_to_copy`). Puramente diagnóstico, não é a correção final: o
objetivo é ver até onde esse padrão ("para logo depois de uma chamada,
delay resolve") se repete, e se eventualmente o boot sobrevive até
`relocate_code()` (marcadores `R`/`C`/`F`/`J`, já existentes em
`arch/mips/lib/reloc.c`) ou até o próprio banner do U-Boot.

V178 foi gravada na SPI física em 2026-09-22 às 07:55:41 (Stage-1
inalterado). V179 foi gravada em seguida, também só na região Stage-2, com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-080226.log`: `VERIFIED`). Imagem
`sg1002-mr-v179-uboot-delay-sweep-16m.bin` (SHA-256
`2662c00f243621c5f15ae076e1416898797d1cd6ef0ab4ca6bf45c3fdf964fa4`).
Resultado físico V179 (log TTL, 2026-09-22, uma tentativa capturada, sem
reinício visível no log — o texto termina no ponto de parada):
`0125L6FPDUVWBSXYCDE01Zpq23456789abcdefgh`, e para. Passou por `f`
(`dram_init_banksize`), `g` (`setup_bdinfo`) e `h` (`display_new_sp`), cada
um agora precedido por `sg1002_init_delay()` — o padrão "delay resolve o
próximo passo" se confirmou de novo três vezes seguidas. Parou logo depois
de `h`, **antes de `i`** (`reloc_fdt`), apesar de também haver um delay
inserido imediatamente antes dessa chamada. Diferente das paradas
anteriores, o delay ali não bastou: a causa provável não é mais "algo
precisa de tempo para se estabilizar", e sim algo específico da própria
chamada.

Achado de código (causa mais concreta até agora, sem gravação):
`reloc_fdt()` (linha ~670) faz
`memcpy(gd->boardf->new_fdt, gd->fdt_blob, fdt_totalsize(gd->fdt_blob))`
se `gd->boardf->new_fdt` for não-nulo. Neste board,
`INITCALL(fdtdec_setup)` — a única rotina que normalmente inicializa
`gd->fdt_blob` — está desligada
(`#if CONFIG_IS_ENABLED(OF_CONTROL) && !CONFIG_IS_ENABLED(INTELBRAS_SG1002_MR_FACTORY_PAYLOAD)`),
então `gd->fdt_blob` fica `NULL` (`gd` inteiro é zerado em
`setup_stack_gd()`). `reserve_fdt()` só reserva `boardf->new_fdt` **se**
`gd->fdt_blob` for válido; como não é, ela pula a reserva e deixa
`new_fdt` como estava. O problema: `struct board_f boardf;` em
`board_init_f()` é uma variável de pilha **nunca zerada** — só
`gd->boardf = &boardf;` é feito antes de usá-la. Então `new_fdt` (e todo
resto de `boardf`) começa com lixo da pilha, que pode por acaso não ser
zero. Se não for, `reloc_fdt()` entra no `if` e chama
`fdt_totalsize(gd->fdt_blob)` com `gd->fdt_blob == NULL` — um dereference
de ponteiro nulo, consistente com o padrão observado (trava sem nenhuma
mensagem, reset automático do núcleo).

Corrigido: `memset(&boardf, 0, sizeof(boardf));` logo após declará-la em
`board_init_f()`, antes de qualquer uso. `memset` já é usada em outro
ponto do mesmo arquivo (`reserve_board()`), então não precisou de novo
include. V180 é a V179 (mantendo toda a instrumentação de trace e os
delays de diagnóstico, que não atrapalham e continuam úteis se houver
mais algum ponto crítico adiante) mais essa correção.

V180 foi gravada na SPI física em 2026-09-22 às 08:10:24, apenas na região
Stage-2 (Stage-1 idêntico ao já gravado), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-081024.log`: `VERIFIED`). Imagem
`sg1002-mr-v180-uboot-boardf-zero-16m.bin` (SHA-256
`47d7089d4bc566abbc95ce296850b8996c6a3604d6343bf5000beb6b20f67325`).
Resultado físico: pendente, aguardando o log TTL.

Resultado físico V180 (log TTL, 2026-09-22, uma tentativa capturada, sem
reinício visível no log): **a correção do `boardf` funcionou**. Sequência:
`0125L6FPDUVWBSXYCDE01Zpq23456789abcdefghijklm`, chegando desta vez até
`i` (`reloc_fdt`, o ponto exato onde V175–V179 sempre paravam) e
seguindo sem interrupção por `j`, `k`, `l`, `m` — toda a sequência de
`reloc_*`/`clear_bss`/`cyclic_unregister_all`. Para exatamente onde **o
emulador também sempre parava** em todas as tentativas anteriores
(v176c/v176d/v178): depois de `m`, dentro de `jump_to_copy()`, antes do
primeiro `putc('R')` que já existe dentro de `relocate_code()`
(`arch/mips/lib/reloc.c`). Pela primeira vez o comportamento físico e o do
emulador convergem no mesmo ponto.

V181 decompõe `jump_to_copy()` com um marcador `J` seguido dos três
argumentos passados a `relocate_code()` em hexadecimal —
`gd->flags`, `gd->start_addr_sp`, `gd->new_gd`, `gd->relocaddr` —, formato
`J:<flags>:<start_addr_sp>:<new_gd>:<relocaddr>`. Se algum desses valores
for inválido (por exemplo, fora da faixa de 128 MiB de DRAM, ou um
endereço óbvio de lixo de pilha), aparece explicitamente no log antes de
qualquer tentativa de pular para `relocate_code()`. Exigiu mover o bloco
de definição de `sg1002_init_trace()`/`sg1002_init_delay()` (antes só
usadas mais adiante no arquivo) para antes de `jump_to_copy()`, que é
definida mais cedo no arquivo — sem isso o compilador rejeita com
"static declaration follows non-static declaration" (a implementação é
idêntica, só a posição no arquivo mudou).

Um log recebido logo depois ainda trazia `...jklm` sem o marcador `J`
esperado — confirmando que era uma nova captura do firmware V180 ainda
gravado (a V181 não tinha sido escrita fisicamente ainda), não um
resultado da V181. Não é um retrocesso; só uma repetição do mesmo teste.

V181 foi gravada na SPI física em 2026-09-22 às 08:24:09, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-082409.log`: `VERIFIED`). Imagem
`sg1002-mr-v181-uboot-relocargs-hex-16m.bin` (SHA-256
`782677d0caa1732774a22ba4451a4fe3e18b535a402e3d87c5b31138e61237d1`).
Resultado físico V181 (log via `serial_ttyUSB0.log`, mais confiável que o
texto colado — os dois terminam exatamente no mesmo ponto, então não foi
corte de cópia): `...jklmJ:00000000:87f90`, e para. `flags=00000000`
confirma que `GD_FLG_SKIP_RELOC` não está setado, então o código realmente
segue para chamar `relocate_code()` — não é um retorno antecipado. Mas a
transmissão para NO MEIO da impressão do primeiro argumento
(`start_addr_sp`: só 5 dos 8 dígitos, `87f90...`), o que é ambíguo:
`sg1002_init_trace()` é um store cru na UART sem esperar
LSR.THRE, e `sg1002_init_trace_hex()` faz 8 dessas escritas seguidas —
exatamente o padrão que já vimos perder o `z` da V178 por sobrescrita no
registrador de transmissão antes de sair fisicamente pelo fio. Os 5
dígitos vistos (`8`,`7`,`f`,`9`,`0`) formam um endereço plausível — perto
do topo dos 128 MiB de DRAM (`0x88000000 - 0x87f9xxxx` ≈ 448 KiB abaixo do
topo, onde as reservas de pilha/malloc/board-info/etc. deveriam
mesmo estar) — mas não dá para saber se o reset aconteceu ali, ou se o
resto da linha (os outros dois argumentos) só não chegou a sair
fisicamente antes de uma parada mais adiante.

V182 torna `sg1002_init_trace()` confiável: espera `LSR.THRE` (bit 29,
`UART0+0x14`, com timeout limitado) antes de cada byte, em vez de
escrever cru. Como esse ponto do boot já está bem depois de `'S'`
(`serial_init`) — e a UART já estava fisicamente configurada pelo Stage-1
antes disso, então mesmo os marcadores anteriores a `'S'` (`U`,`V`,`W`,`B`)
também podem esperar com segurança —, isso deve fazer **cada** caractere
de trace deste arquivo chegar integralmente, eliminando a ambiguidade
"perdido por sobrescrita" vs "o sistema parou ali" para todos os
marcadores daqui em diante, não só o hexadecimal.

V182 foi gravada na SPI física em 2026-09-22 às 08:28:40, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-082840.log`: `VERIFIED`). Imagem
`sg1002-mr-v182-uboot-reliable-trace-16m.bin` (SHA-256
`e9117b0fbce1276c8f2369c9a60b311d4048319cf39a0161b1bcb334db196f54`).
Resultado físico V182 (log via TTL, 2026-09-22): `J:00000000:87f90000:
87f90f00:87fc1000` — desta vez a linha saiu **completa**, confirmando que
a espera de THRE resolve a ambiguidade. Os três argumentos são plausíveis
e coerentes: `relocaddr=0x87fc1000` (mais alto, reservado primeiro por
`reserve_uboot`), `new_gd=0x87f90f00` (reservado por
`reserve_global_data`), `start_addr_sp=0x87f90000` (mais baixo, reservado
por último por `reserve_stacks`), todos dentro dos 128 MiB de DRAM
(`0x80000000..0x88000000`), próximos do topo, com `new_gd - start_addr_sp
= 0xf00` (bem plausível para `sizeof(gd_t)`). **Nenhum argumento é
inválido** — a hipótese "argumento ruim" está descartada.

Achado importante (sem gravação): os pontos `putc('R')`/`'C'`/`'F'`/`'J'`
dentro de `relocate_code()` (`arch/mips/lib/reloc.c`) usam o `putc()` da
biblioteca do U-Boot, não o nosso `sg1002_init_trace` cru. `putc()`, sem
console registrado (`gd->flags` é `0`, sem `GD_FLG_HAVE_CONSOLE`), cai em
`pre_console_putc()` — e com `CONFIG_PRE_CONSOLE_BUFFER` **desligado**
neste `.config`, `pre_console_putc()` é literalmente um stub vazio
(`common/console.c` linha 715). Ou seja, **nunca ver `R`/`C`/`F`/`J` não
prova travamento**: pode significar que `relocate_code()` está rodando e
até completando normalmente, só que muda, porque a única instrumentação
ali nunca produziu saída visível em nenhuma das versões anteriores.

V183 substitui essa instrumentação muda por marcadores crus e confiáveis
(mesma técnica com espera de THRE), replicados localmente em
`arch/mips/lib/reloc.c` (`sg1002_reloc_trace`, `Q`/`K`/`N`/`O` nos mesmos
quatro pontos) e adiciona um marcador (`sg1002_boardr_trace`, `'r'` logo
na entrada e `'!'` logo depois) no primeiro ponto executável de
`board_init_r()` (`common/board_r.c`) — a primeira linha do U-Boot que
roda a partir do código **já relocado**. Se `'r'` aparecer, o salto final
de `relocate_code()` (o bloco `asm volatile` com `jr`) funcionou de
verdade, e o problema estaria mais adiante, dentro de `initcall_run_r()`.
Se `Q`/`K`/`N`/`O` aparecerem mas `'r'` não, o problema está no próprio
salto (`jr`) ou no prólogo da função relocada. Se nem `Q` aparecer, a
trava é anterior à própria entrada em `relocate_code()`, apesar dos
argumentos parecerem válidos.

V183 foi gravada na SPI física em 2026-09-22 às 08:36:26, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-083626.log`: `VERIFIED`). Imagem
`sg1002-mr-v183-uboot-reloc-boardr-trace-16m.bin` (SHA-256
`9e629ca313ef9ec97f1de8e5e39ea73750841e005bebf2956cdad61c557753b0`).

**Resultado físico V183 (log TTL, 2026-09-22): avanço decisivo.** A
captura mostrou a sequência completa `Q`, `K`, `N`, `O` seguida de `r`,
`!` — os quatro marcadores crus de `relocate_code()` **e** os dois
primeiros marcadores crus de `board_init_r()` apareceram, nessa ordem,
em uma única execução física. Isso prova, pela primeira vez em hardware
real, que:

- `relocate_code()` completa integralmente: copia o U-Boot para a DRAM
  (`Q`→`K`), aplica todas as relocações ELF (`K`→`N`), limpa a `.bss`
  (`N`→`O`) e executa o `jr` final;
- esse `jr` realmente pousa dentro da cópia relocada de
  `board_init_r()` (`'r'`), e a função continua rodando após a
  primeira linha (`'!'`), ou seja, a pilha (`start_addr_sp`), o `gd`
  relocado (`new_gd`) e o endereço de destino (`relocaddr`) — cujos
  valores já haviam sido confirmados plausíveis na V182 — estão de
  fato corretos o bastante para um `board_init_r()` funcional.

Nenhuma saída após `'!'` foi capturada nessa sessão — ou seja, a placa
avançou mais do que em qualquer versão anterior, mas ainda para em
algum ponto não identificado dentro de `initcall_run_r()` (ou possivelmente
já em `run_main_loop()`). Essa é a motivação direta da V184 abaixo.

## V184 — contador de passos numerado em `initcall_run_r()`

Com a entrada de `board_init_r()` comprovada (V183), o próximo alvo é
localizar exatamente qual dos `INITCALL(...)` dentro de
`initcall_run_r()` é o último a completar. Em vez de uma letra por
chamada (esgotaria o alfabeto rapidamente — a função tem até ~40
`INITCALL`s em código-fonte, muitos dos quais desativados por este
`.config`), a V184 adiciona um contador numérico:

```c
static unsigned int sg1002_step_n;
static void sg1002_step(void)
{
	unsigned int n = sg1002_step_n++;
	sg1002_boardr_trace('#');
	sg1002_boardr_trace("0123456789abcdef"[(n >> 4) & 0xf]);
	sg1002_boardr_trace("0123456789abcdef"[n & 0xf]);
}
#define SG1002_STEP() sg1002_step()
```

`SG1002_STEP()` foi inserido logo após (quase) todo `INITCALL(...)` /
`INITCALL_EVT(...)` em `initcall_run_r()`, incluindo um marcador líder
antes mesmo do primeiro `INITCALL`. Cada marcador emite três bytes crus
pela UART (`#NN`, dois dígitos hexadecimais), usando a mesma técnica de
espera por `LSR.THRE` já validada nas versões anteriores — não depende
do console do U-Boot.

Como muitos `INITCALL`s deste código-fonte estão desativados neste
`.config` específico (via `#if CONFIG_IS_ENABLED(...)`), o número de
passo N não corresponde à posição no código-fonte, e sim à posição
entre as chamadas que **realmente compilam** para esta placa. A tabela
abaixo foi derivada avaliando deterministicamente cada condição `#if`
contra o `.config` real de build (script Python ad-hoc, não versionado)
e confirmada cruzando com a ordem de chamadas (`jal`) no binário ligado
(`u-boot` ELF) — os nomes de função batem exatamente com o alvo de cada
`jal`, incluindo os casos em que um `INITCALL` de nome genérico (ex.:
`initr_malloc`) chama uma função interna com outro nome (`mem_malloc_init`).
`nm`/`objdump` confirmam exatamente 30 sítios de chamada reais a
`sg1002_step` no binário compilado, batendo com a tabela:

| passo  | após o INITCALL           | passo  | após o INITCALL          |
|--------|----------------------------|--------|---------------------------|
| `#00`  | (marcador líder, antes de qualquer INITCALL) | `#10` | `dm_announce` |
| `#01`  | `initr_trace`               | `#11`  | `arch_initr_trap` |
| `#02`  | `initr_reloc`                | `#12`  | `power_init_board` |
| `#03`  | `event_init`                 | `#13`  | `initr_env` |
| `#04`  | `initr_reloc_global_data`    | `#14`  | `cpu_secondary_init_r` |
| `#05`  | `initr_barrier`              | `#15`  | `INITCALL_EVT(EVT_SETTINGS_R)` |
| `#06`  | `initr_malloc`               | `#16`  | `stdio_add_devices` |
| `#07`  | `log_init`                   | `#17`  | `jumptable_init` |
| `#08`  | `initr_bootstage`            | `#18`  | `console_init_r` |
| `#09`  | `initr_of_live`              | `#19`  | `interrupt_init` |
| `#0a`  | `initr_dm`                   | `#1a`  | `initr_boot_led_blink` |
| `#0b`  | `initr_lmb`                  | `#1b`  | `board_late_init` |
| `#0c`  | `initr_dm_devices`           | `#1c`  | `initr_net` |
| `#0d`  | `stdio_init_tables`          | `#1d`  | `INITCALL_EVT(EVT_LAST_STAGE_INIT)` |
| `#0e`  | `serial_initialize`          | (sem passo) | `initr_boot_led_on`, depois `run_main_loop` (não retorna se tudo correr bem) |
| `#0f`  | `initr_announce`             |        |                           |

**Correção (2026-09-22):** a primeira versão desta tabela (publicada com
a V184) omitiu `initr_announce`, deslocando por um passo toda entrada a
partir dali, e usava rótulos decimais em vez de hexadecimal (o formato
real que `sg1002_step()` transmite). O resultado físico V186 expôs o
erro: o par de marcadores `@`/`$` (inserido logo após
`INITCALL(console_init_r)`) apareceu entre `#18` e `#19` no fio, não
onde a tabela antiga previa. A tabela acima já está corrigida
(recalculada por script determinístico e formatada em hexadecimal de 2
dígitos) e é a versão usada em todo o restante deste documento.

Se o último `#NN` capturado for, por exemplo, `#0c`, o próximo
candidato a travar é `stdio_init_tables` (passo `#0d`); se nenhum `#NN`
aparecer, a trava é entre `'!'` (V183) e o primeiro `INITCALL`
(`initr_trace`) — uma região muito menor para investigar do que toda a
`initcall_run_r()`.

V184 foi gravada na SPI física em 2026-09-22 às 11:42:42, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-114242.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v184-uboot-initcall-r-steps-16m.bin` (SHA-256
`763d6fd99ddc7aa0959653b876da71362b9c6891e52f8f7c379571e42b9ee134`).
Reprodutibilidade do script de build verificada por recompilação
independente em diretório limpo: apenas a string de timestamp embutida
(`Sep 22 2026 - 11:42:07` vs. `11:49:00`) difere entre as duas imagens,
byte a byte idêntico fora disso.

**Resultado físico V184 (log TTL, 2026-09-22): outro avanço decisivo.**
A captura mostrou **todos os 30 marcadores numerados**, `#00` até `#1d`
(29 em decimal), em sequência — ou seja, **todo `INITCALL` que sobrevive
neste `.config` dentro de `initcall_run_r()` completou**, incluindo
`console_init_r` (`#23`→`#24`... a tabela acima lista o nome exato após
cada `#NN`) e `initr_net` (`#28`). Nenhuma saída apareceu depois de
`#1d`: nem um crash marker, nem uma nova reinicialização do Stage-1
(`serial_broker.py` continuou rodando ao vivo, sem novos bytes, por
pelo menos 8s após a captura) — a placa está silenciosamente parada ou
travada em algum ponto entre o fim de `initcall_run_r()` e a primeira
saída esperada de `main_loop()`.

Isso é notável porque **nem a contagem regressiva do
`CONFIG_BOOTDELAY=3`** ("Hit any key to stop autoboot") apareceu —
esse texto vem de `bootdelay_process()`, chamado de dentro de
`main_loop()`, usando o `printf()`/`putc()` normal do U-Boot, que
deveria funcionar a essa altura já que `console_init_r()` (INITCALL
antes de `#23`) reportou sucesso.

**Achado adicional relevante: o ambiente de fábrica na SPI (offset
`0x040000`, `CONFIG_ENV_OFFSET`) está fora das duas janelas autorizadas
de gravação (nunca foi tocado) e contém um ambiente real, no formato
clássico do U-Boot (CRC32 de 4 bytes + pares `chave=valor\0`), com CRC
**válido** (`0x541f01be`, conferido byte a byte contra o payload).
Entradas relevantes:
```
bootdelay=3
stdin=serial
stdout=serial
stderr=serial
bootcmd=bootm 0xb4050000
```
`bootcmd` tentaria rodar `bootm` sobre o endereço mapeado do próprio
Stage-2 (onde este U-Boot GPL está gravado, não um kernel) — deve falhar
com "Bad Magic Number" **se** a saída do console estiver realmente
viva a essa altura. `stdin`/`stdout`/`stderr` apontam para `"serial"`,
um nome de dispositivo stdio plausível, então não há indício de uma
variável de ambiente corrompida direcionando o console para um
dispositivo inexistente — mas a reatribuição feita por
`console_init_r()` a partir dessas variáveis (via `console_assign()`)
só funciona se o dispositivo serial real estiver registrado sob
exatamente esse nome no driver model; isso permanece não verificado.

## V185 — rastreamento de `run_main_loop()` e `main_loop()`

Com todo `initcall_run_r()` provado completo (V184), o próximo alvo é o
trecho ainda não instrumentado: `initr_boot_led_on()` → `run_main_loop()`
(`common/board_r.c`) → `main_loop()` (`common/main.c`). V185 adiciona
marcadores crus (mesma técnica com espera de `LSR.THRE`) em cada ponto
relevante:

- `common/board_r.c`, `run_main_loop()`: `'T'` logo após
  `initr_boot_led_on()`; `'U'` na entrada da função, antes de
  `event_notify_null(EVT_MAIN_LOOP)`; `'u'` se esse evento retornar
  erro (retorno antecipado); `'V'` se tiver sucesso, logo antes do
  primeiro `main_loop()`.
- `common/main.c`, `main_loop()`: `'M'` na entrada; `'a'` após
  `cli_init()`; `'b'` após o bloco `CONFIG_USE_PREBOOT` (desabilitado
  neste `.config`, então o marcador dispara imediatamente); `'c'`/`'x'`/`'d'`
  em torno de `event_notify_null(EVT_POST_PREBOOT)` (`'x'` só se retornar
  erro); `'e'`/`'f'` em torno de `process_button_cmds()`; `'g'` após
  `bootdelay_process()`; `'h'` após `cli_process_fdt()`; `'j'` após
  `autoboot_command()`; `'k'` antes de `cli_loop()`; `'n'` se
  `cli_loop()` chegar a retornar (não deveria, em uso normal).

Como `sg1002_boardr_trace()` só é definida mais adiante no arquivo
(mesmo problema de ordering já visto em `jump_to_copy()`/`SG1002_STEP()`),
desta vez foi usada uma declaração antecipada (`static void
sg1002_boardr_trace(char c);`) em vez de mover o bloco de novo.
`common/main.c` é um arquivo novo para instrumentação nesta sessão;
ganhou sua própria função `sg1002_main_trace()` duplicada localmente,
seguindo o padrão já usado em `arch/mips/lib/reloc.c`.

V185 foi gravada na SPI física em 2026-09-22 às 12:03:53, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-120353.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v185-uboot-main-loop-trace-16m.bin` (SHA-256
`c89b7f0746c9393c6ffa9aee8d14b812d4ee8bb046fe5fda88bef5a6149862f7`).

**Resultado físico V185 (log TTL, 2026-09-22): `T U V M a b c d e f g h j k`
— toda a instrumentação disparou, sem exceção.** Isso prova que:

- `run_main_loop()` roda até o fim de `event_notify_null(EVT_MAIN_LOOP)`
  sem erro (`'u'`, o marcador de falha, não apareceu) e chama
  `main_loop()`;
- `main_loop()` executa `cli_init()`, o evento `EVT_POST_PREBOOT` sem
  erro (`'x'` também não apareceu), `process_button_cmds()`,
  **`bootdelay_process()` até o fim** — ou seja, a contagem regressiva
  inteira de `CONFIG_BOOTDELAY=3` (3 segundos reais) transcorreu
  internamente — e `autoboot_command()`, que roda o `bootcmd` de
  fábrica (`bootm 0xb4050000`);
- o fluxo chega até imediatamente antes de `cli_loop()` (`'k'`).

**Apesar disso, nenhum texto de `printf()`/`puts()` jamais apareceu** —
nem a contagem "Hit any key to stop autoboot: 3... 2... 1...", nem um
eventual erro do `bootm` (que deveria falhar com "Bad Magic Number",
já que o endereço `0xb4050000` é o próprio binário deste U-Boot, não um
kernel). Como os marcadores `T`/`U`/`V`/`M`/.../`k` usam exatamente os
mesmos registros físicos da UART que o driver
`drivers/serial/serial_rtl8380.c` (`base + reg*4`, byte nos bits
31:24, `LSR.THRE` antes de escrever em `THR`) e continuam perfeitamente
legíveis mesmo depois de `console_init_r()` reprogramar a UART
(divisor de baud recalculado a partir de `clock-frequency=200000000`
no devicetree), **isso descarta um problema de baud/registrador no
driver** — o hardware claramente continua funcionando. A quebra deve
estar na camada de console/stdio acima do driver (por exemplo,
`console_assign()`, chamado por `console_init_r()` a partir das
variáveis de ambiente `stdin`/`stdout`/`stderr=serial`, pode não estar
encontrando/associando o dispositivo `"serial"` correto).

## V186 — dois testes diretos de `printf()`

Para isolar driver vs. camada de console, V186 adiciona duas chamadas
diretas ao `printf()` real do U-Boot (não a técnica de marcador cru),
cada uma cercada por marcadores crus `'@'`/`'$'`:

- logo após `INITCALL(console_init_r)` em `initcall_run_r()`
  (`common/board_r.c`): `printf("SG1002TESTPRINTF\n");`
- logo antes de `cli_loop()` em `main_loop()` (`common/main.c`):
  `printf("SG1002TESTPRINTF2\n");`

Se o texto aparecer entre qualquer um dos pares `@`/`$`, o caminho do
console funciona e o próximo alvo é instrumentar dentro de
`cli_loop()`/`cli_simple_loop()`. Se não aparecer em nenhum dos dois,
a suspeita se concentra em `console_init_r()`/`console_assign()` — a
próxima instrumentação seria dentro de `common/console.c`, verificando
se `stdio_get_by_name("serial")` de fato encontra o dispositivo
registrado por `serial_rtl8380.c`.

V186 foi gravada na SPI física em 2026-09-22 às 12:10:18, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-121018.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v186-uboot-printf-test-16m.bin` (SHA-256
`718db7c499b60a2a7fec48ebf2f93d0f722163853a9bfbf3c0df4d884cae5533`).

**Resultado físico V186 (log TTL, 2026-09-22): decisivo, e revelou o
erro de numeração da tabela.** A captura mostrou `#18@$#19...#1d`, ou
seja, o par `@`/`$` apareceu exatamente entre o passo de
`console_init_r` (`#18`) e o de `interrupt_init` (`#19`) — confirmando
tanto a posição real de `console_init_r` na tabela corrigida acima
quanto o resultado do teste em si: **nada apareceu entre `@` e `$`**.
O segundo par `@`/`$` (antes de `cli_loop()`) também apareceu, igualmente
sem texto entre eles (`...jk@$`, fim da captura). `printf("SG1002TESTPRINTF\n")`
e `printf("SG1002TESTPRINTF2\n")` produziram **zero bytes visíveis**,
apesar de os marcadores crus imediatamente antes e depois — na mesma
função, microssegundos de distância — continuarem perfeitamente
legíveis. Isso descarta definitivamente um problema na UART/driver:
`console_init_r()` "tem sucesso" (não retorna erro), mas nenhum
dispositivo `stdio` chega a ser realmente associado como saída.

## V187 — rastreamento de `serial_post_probe()`

Investigando `drivers/serial/serial-uclass.c`, `serial_post_probe()` —
chamada quando um dispositivo `UCLASS_SERIAL` é sondado pelo driver
model — é quem registra o dispositivo na lista `stdio` via
`stdio_register_dev()`, mas contém:
```c
if (!(gd->flags & GD_FLG_RELOC))
	return 0;
```
O driver deste UART (`drivers/serial/serial_rtl8380.c`) é
`DM_FLAG_PRE_RELOC`, ou seja, é sondado uma vez **antes** da
realocação — quando `GD_FLG_RELOC` certamente ainda não está setada,
pulando o registro por design. O U-Boot espera que dispositivos assim
sejam sondados **de novo** depois da realocação, quando o driver model
migra os dispositivos pré-realocação para a nova árvore — e
`GD_FLG_RELOC` já está setada bem cedo (`initr_reloc()`, passo `#02`).
Mas se essa segunda sondagem realmente acontece neste board nunca foi
verificado. V187 adiciona marcadores crus em toda chamada de
`serial_post_probe()`: `'P'` na entrada, `'1'`/`'0'` para o estado de
`GD_FLG_RELOC`, `'.'` se retornar cedo por causa dele, `'R'` antes de
`stdio_register_dev()`, `'K'`/`'E'` para seu resultado, `'-'` se
`CONFIG_DM_STDIO` estivesse desligado (não é o caso aqui).

Como esta função pode ser chamada mais de uma vez (uma pré-realocação,
possivelmente outra pós-realocação), a captura pode mostrar a sequência
duas vezes — a primeira ocorrência, bem no início do log (antes mesmo
de `'0'`/`'1'`/`'2'`/`'5'`/`'L'`/`'6'`/`'F'`), é esperada e não é o
alvo da investigação.

V187 foi gravada na SPI física em 2026-09-22 às 12:19:12, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-121912.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v187-uboot-serial-probe-trace-16m.bin` (SHA-256
`a4aedec6d7048136f58577da6bc2485a22f9775b1c7505e0fb0e9b84c4a5ae7c`).

**Resultado físico V187 (log TTL, 2026-09-22): identifica a causa raiz.**
A captura é byte a byte idêntica à da V186 (mesma sequência completa,
terminando em `...jk@$`) — **nenhum dos novos marcadores (`P`, `1`,
`0`, `.`, `R`, `K`, `E`, `-`) apareceu em lugar nenhum**, nem mesmo uma
única vez pré-realocação. Como o símbolo `serial_post_probe` está
confirmadamente presente no binário (`nm` mostrou
`83f128d8 t serial_post_probe`) e é o hook `.post_probe` registrado em
`UCLASS_DRIVER(serial)`, a única explicação é que **`device_probe()`
nunca é chamado no dispositivo UART em nenhum momento do boot inteiro**
— nem antes, nem depois da realocação.

Investigando `serial_init()` (`drivers/serial/serial-uclass.c`, chamada
pré-realocação) e `serial_initialize()` (chamada pós-realocação, passo
`#0e` — que apenas chama `serial_init()` de novo), encontrou-se a causa
raiz:
```c
int serial_init(void)
{
	/*
	 * ... UART0 is already configured by Stage-1, so defer
	 * selecting a DM console until serial_initialize() after relocation.
	 */
	if (IS_ENABLED(CONFIG_INTELBRAS_SG1002_MR_FACTORY_PAYLOAD))
		return 0;

	serial_find_console_or_panic();  /* nunca alcançado */
	...
```
`IS_ENABLED(CONFIG_INTELBRAS_SG1002_MR_FACTORY_PAYLOAD)` é uma
verificação **em tempo de compilação** — seu valor é o mesmo em
*qualquer* chamada desta função, antes ou depois da realocação. O
comentário descreve a intenção correta ("adiar a seleção do console DM
até `serial_initialize()` pós-realocação"), mas como
`serial_initialize()` apenas chama `serial_init()` de novo, a segunda
chamada bate exatamente na mesma guarda e retorna cedo do mesmo jeito —
o "adiamento" nunca de fato acontece. `gd->cur_serial_dev` nunca é
definido, `serial_post_probe()` nunca é acionado,
`console_init_r()` nunca encontra um dispositivo de saída. **Esta é a
causa raiz de todo o silêncio de console observado da V184 à V186**,
apesar de cada `INITCALL` de `initcall_run_r()` ter tido sucesso.

## V188 — correção de `serial_init()`

A correção troca a guarda de tempo de compilação por uma que também
verifica a flag de runtime `GD_FLG_RELOC` (definida bem cedo, em
`initr_reloc()`, passo `#02`, muito antes de `serial_initialize()`
rodar no passo `#0e`):
```c
if (IS_ENABLED(CONFIG_INTELBRAS_SG1002_MR_FACTORY_PAYLOAD) &&
    !(gd->flags & GD_FLG_RELOC))
	return 0;
```
Pré-realocação, `GD_FLG_RELOC` ainda não está setada → comportamento
inalterado (pula a seleção do console DM, como pretendido, já que o
Stage-1 já configurou a UART0 raw). Pós-realocação, a flag já está
setada → a guarda passa a ser falsa → `serial_find_console_or_panic()`
roda de verdade, define `gd->cur_serial_dev`, e isso deve permitir que
`console_init_r()` encontre e registre o dispositivo de saída
corretamente.

Toda a instrumentação de V184-V187 foi mantida (passos numerados,
marcadores de `run_main_loop()`/`main_loop()`, os dois testes de
`printf()`, o rastreamento de `serial_post_probe()`), então uma única
captura física deve tanto confirmar a correção (os marcadores `P`/`1`/`R`/`K`
de `serial_post_probe()` devem aparecer pós-realocação desta vez)
quanto mostrar, pela primeira vez, saída real do console do U-Boot —
se a correção realmente resolver o problema.

V188 foi gravada na SPI física em 2026-09-22 às 12:27:53, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-122753.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v188-uboot-serial-init-fix-16m.bin` (SHA-256
`1f64d07364e1cf499412dcc43e5b5b33139c52e03e17d151b6d886346c563e33`).

**Resultado físico V188 (log TTL, 2026-09-22): regressão instrutiva.**
A captura parou entre `#0d` (`stdio_init_tables`) e `#0e`
(`serial_initialize`) — mais cedo do que V184-V187, que sempre
alcançavam `#1d`. Isso, à primeira vista, parece um retrocesso, mas na
verdade é a confirmação de que a correção **está funcionando**: como
`serial_post_probe()` nunca havia disparado nenhuma vez antes da V188
(achado da V187), o driver `rtl8380_uart_probe()` — incluindo um
`reset` do FIFO (`FCR=7`) e a reprogramação do divisor de baud a partir
de `clock-frequency=200000000` no devicetree — **nunca havia executado
de verdade em nenhum teste físico desta sessão**. Todo marcador cru
usado até aqui (inclusive na V188) funcionou apenas graças à
configuração que o Stage-1 já havia deixado na UART, nunca validando o
driver do GPL U-Boot em si. Agora que a correção da V188 faz
`serial_find_console_or_panic()` rodar pela primeira vez pós-realocação,
`rtl8380_uart_probe()` executa pela primeira vez também — e se a
suposição de `clock-frequency=200000000` estiver errada,
`rtl8380_uart_setbrg()` reconfiguraria silenciosamente a UART para uma
taxa de baud diferente de 115200, explicando o novo silêncio.

## V189 — rastreamento de `rtl8380_uart_probe()`/`setbrg()`

V189 adiciona marcadores crus diretamente em
`drivers/serial/serial_rtl8380.c`: `'{'`/`'}'` cercando a configuração
de base/clock em `rtl8380_uart_probe()`, e `'='`/dígitos hexadecimais
do divisor calculado/`'~'` cercando a escrita do divisor em
`rtl8380_uart_setbrg()`. Se `'{'` e `'}'` aparecerem mas nada mais, o
problema está nos próprios `rtl8380_uart_write(IER=0)`/`rtl8380_uart_write(FCR=7)`
(entre `'}'` e `'='`). Se o valor do divisor aparecer mas os
marcadores pararem de ser legíveis logo depois, é a reprogramação do
divisor que está quebrando a transmissão — confirmando a suspeita do
`clock-frequency` errado.

V189 foi compilada e verificada (janelas de flash corretas, apenas
Stage-2 difere do dump de fábrica) — o programador CH341A SPI
inicialmente não foi detectado (`lsusb -d 1a86:5512` vazio; apenas o
adaptador TTL em modo serial, `1a86:5523`, e outro dispositivo
`1a86:484a`, estavam presentes), então a gravação ficou pendente até o
usuário reconectar o programador. Uma vez detectado, V189 foi gravada
na SPI física em 2026-09-22 às 12:51:11, apenas na região Stage-2
(Stage-1 idêntico ao já gravado desde a V174), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-125111.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v189-uboot-rtl8380-probe-trace-16m.bin` (SHA-256
`a7663e26bbddb47beacfef0491e7b5495ab08cfc7ec51c1e85c6ab50f14241e9`).

**Resultado físico V189 (log TTL, 2026-09-22): mesmo ponto de parada,
achado negativo importante.** A captura parou exatamente no mesmo
lugar que a V188 (entre `#0d` e `#0e`) — e, crucialmente, **`'{'`
(entrada de `rtl8380_uart_probe()`) nunca apareceu**. Isso significa
que a trava acontece **antes** do próprio `probe()` do driver ser
alcançado — em algum lugar dentro das chamadas de busca do driver model
de `serial_find_console_or_panic()` (`uclass_get_device_by_seq()` /
`uclass_get_device()` / `uclass_first_device_err()`), que é
justamente o que deveria acionar `device_probe()` na UART em primeiro
lugar.

## V190 — rastreamento de `serial_find_console_or_panic()`

V190 adiciona marcadores crus cercando cada ramo e cada chamada
`uclass_*` dentro de `serial_find_console_or_panic()`
(`drivers/serial/serial-uclass.c`): `'<'` na entrada, `'b'`/`'n'` para
se `gd->fdt_blob` está definido, `'3'` ao alcançar a seção de busca de
fallback (usada neste `.config`, já que `blob` é nulo), `'4'`/`'a'` em
torno de `uclass_get_device_by_seq()`, `'5'`/`'B'` em torno de
`uclass_get_device()`, `'6'`/`'c'` em torno de
`uclass_first_device_err()`, `'7'` se as três falharem, `'8'`/`'9'`
antes do `panic_str()` de `CONFIG_REQUIRE_SERIAL_CONSOLE`, `'>'` em
qualquer retorno bem-sucedido. Os marcadores de `probe()`/`setbrg()`
da V189 (`{`/`}`/`=`/`~`) foram mantidos, para o caso de a busca ter
sucesso desta vez.

V190 foi gravada na SPI física em 2026-09-22 às 12:56:19, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-125619.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v190-uboot-find-console-trace-16m.bin` (SHA-256
`373a40fd1d37b1cd82d4032af5d05d6750a90f9807b43a412dd4b312dc8a56e7`).

**Resultado físico V190 (log TTL, 2026-09-22): `<n3456789`.** `gd->fdt_blob`
é nulo (`'n'`, como esperado — confirma o achado da V180 de que ele
nunca é definido nesta placa), a seção de busca de fallback é
alcançada (`'3'`), mas **as três chamadas de busca falham
completamente**: `uclass_get_device_by_seq()` (`'4'`, sem `'a'` de
sucesso), `uclass_get_device()` (`'5'`, sem `'B'`), e
`uclass_first_device_err()` (`'6'`, sem `'c'`) — terminando em `'7'`
(todas falharam), depois `'8'`/`'9'` logo antes do
`panic_str("No serial driver found")`, onde a captura para. Isso é uma
**falha de busca no driver model**, não uma falha de `probe()` — o
marcador `'{'` de `rtl8380_uart_probe()` continua nunca aparecendo,
confirmando que `device_probe()` nunca chega a ser tentado.

## V191 — contagem de dispositivos em `UCLASS_SERIAL`

V191 adiciona uma contagem de quantos dispositivos estão de fato
vinculados (`bind`) a `UCLASS_SERIAL` no momento exato da busca, via
`uclass_get(UCLASS_SERIAL, &uc)` seguido de uma volta em
`uc->dev_head`, transmitida como `'#'` + 2 dígitos hexadecimais. Se o
resultado for `#00`, o próprio passo de *bind* do driver model nunca
anexou o nó `uart0` do devicetree a este uclass pós-realocação — um
bug de vinculação, bem anterior e mais fundamental do que qualquer
problema de `probe()`. Se for maior que zero, o dispositivo está
vinculado mas todo caminho de busca ainda assim falha em selecioná-lo
— apontando para outra causa (talvez a sequência/alias, ou uma falha
de `probe()` que a própria DM trata internamente sem eu conseguir ver,
antes mesmo de chamar o `.probe` do driver).

V191 foi gravada na SPI física em 2026-09-22 às 13:00:48, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-130048.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v191-uboot-uclass-count-trace-16m.bin` (SHA-256
`0c5cd1eebf2a327ac3e3d7e38beadf5a92408f34168cdffe39516c94bca36a98`).

**Resultado físico V191 (log TTL, 2026-09-22): `#00`.** Zero
dispositivos vinculados a `UCLASS_SERIAL` no momento da busca. Isso
confirma que o passo de **vinculação** (`bind`) do driver model nunca
anexou o nó `uart0` do devicetree a nenhum uclass pós-realocação — não
é uma falha de seleção/probe, é como se o devicetree inteiro estivesse
vazio para fins de vinculação de dispositivos.

Investigando a cadeia `initr_dm()` → `dm_init_and_scan(false)` →
`dm_scan()` → `dm_extended_scan()` → `dm_scan_fdt()` →
`dm_scan_fdt_node(gd->dm_root, ofnode_root(), ...)`: como
`CONFIG_OF_LIVE` está desligado, `ofnode_root()` retorna um nó da
"árvore plana" (offset 0) — que só é válido se houver um blob de
devicetree real por trás (`gd->fdt_blob`). Como estabelecido desde a
V180 desta sessão, **`gd->fdt_blob` nunca é definido nesta placa**:
`common/board_f.c` pula `INITCALL(fdtdec_setup)` inteiramente
(`#if CONFIG_IS_ENABLED(OF_CONTROL) && !CONFIG_IS_ENABLED(INTELBRAS_SG1002_MR_FACTORY_PAYLOAD)`),
e nenhuma outra `INITCALL` em `initcall_run_r()` jamais o define. Isso
explica tudo: sem um blob válido, `dm_scan_fdt()` percorre zero nós,
silenciosamente (sem erro), então `initr_dm()`/`initr_dm_devices()`
"têm sucesso" mesmo sem vincular um único dispositivo.

Investigando o histórico (`git log`/`git blame`), o commit que pulou
essa `INITCALL` é `4b6675b4` ("uboot: defer early FDT setup for SG1002
payload", Codex, 2026-09-20), e `fdtdec_setup()` (`lib/fdtdec.c`) já
contém marcadores próprios (`sg1002_fdt_trace()`, `'A'`-`'F'`) de
trabalho anterior do Codex especificamente para esta placa (commits
`33adb183`/`a5b34fd9`/`90bef381`). **É exatamente a mesma classe de bug
da V188 (`serial_init()`):** um comentário descrevendo a intenção de
adiar a configuração real para depois, mas a chamada adiada nunca foi
de fato escrita em lugar nenhum.

## V192 — chamar `fdtdec_setup()` pós-realocação

Com `CONFIG_OF_SEPARATE=y` e `CONFIG_OF_BOARD` desligado, o caminho
real de `fdtdec_setup()` para esta placa é simples:
`gd->fdt_blob = fdt_find_separate();` seguido de
`fdtdec_prepare_fdt()`/`fdtdec_board_setup()` — nenhum código
específico de placa roda (não há `board_fdt_blob_setup()` para este
board). V192 adiciona `INITCALL(fdtdec_setup);` uma única vez em
`initcall_run_r()`, logo antes de `initr_of_live()`/`initr_dm()` (que
precisam de `gd->fdt_blob` para vincular qualquer coisa) — agora que
estamos em segurança pós-realocação, ao contrário do contexto frágil
de `board_init_f()` de onde isso foi originalmente adiado.

Como isso insere um novo passo numerado, **a tabela de passos muda a
partir daqui** — recalculada da mesma forma determinística das
correções anteriores:

| passo  | após o INITCALL           | passo  | após o INITCALL          |
|--------|----------------------------|--------|---------------------------|
| `#00`  | (marcador líder) | `#10` | `initr_announce` |
| `#01`  | `initr_trace`               | `#11`  | `dm_announce` |
| `#02`  | `initr_reloc`                | `#12`  | `arch_initr_trap` |
| `#03`  | `event_init`                 | `#13`  | `power_init_board` |
| `#04`  | `initr_reloc_global_data`    | `#14`  | `initr_env` |
| `#05`  | `initr_barrier`              | `#15`  | `cpu_secondary_init_r` |
| `#06`  | `initr_malloc`               | `#16`  | `INITCALL_EVT(EVT_SETTINGS_R)` |
| `#07`  | `log_init`                   | `#17`  | `stdio_add_devices` |
| `#08`  | `initr_bootstage`            | `#18`  | `jumptable_init` |
| `#09`  | **`fdtdec_setup` (novo na V192)** | `#19`  | `console_init_r` |
| `#0a`  | `initr_of_live`              | `#1a`  | `interrupt_init` |
| `#0b`  | `initr_dm`                   | `#1b`  | `initr_boot_led_blink` |
| `#0c`  | `initr_lmb`                  | `#1c`  | `board_late_init` |
| `#0d`  | `initr_dm_devices`           | `#1d`  | `initr_net` |
| `#0e`  | `stdio_init_tables`          | `#1e`  | `INITCALL_EVT(EVT_LAST_STAGE_INIT)` (último passo) |
| `#0f`  | `serial_initialize`          |        |                           |

Confirmado por `nm`/`objdump` no binário ligado: exatamente 31 sítios
de chamada reais a `sg1002_step` (antes eram 30). Se o log mostrar
`#09` seguido de algo diferente de `#0a`, o problema está dentro do
próprio `fdtdec_setup()` — nesse caso, seus marcadores internos
(`'A'` entrada, `'B'`/`'C'` em torno de `fdt_find_separate()`, `'D'`/`'E'`
em torno de `fdtdec_prepare_fdt()`, `'F'` antes do retorno) devem
aparecer entre `#08` e `#09` e apontar exatamente onde.

V192 foi gravada na SPI física em 2026-09-22 às 13:07:54, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-130754.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v192-uboot-fdtdec-setup-postreloc-16m.bin` (SHA-256
`46d51bd99fbaaa2216576c9e58928b76fd20f5ee69db979130c42c03cc999953`).

**Resultado físico V192 (log TTL, 2026-09-22): `ABCDEF` completo, mas
`#00` continua.** Os seis marcadores internos de `fdtdec_setup()`
(`'A'` entrada, `'B'`/`'C'` em torno de `fdt_find_separate()`,
`'D'`/`'E'` em torno de `fdtdec_prepare_fdt()`, `'F'` antes do
retorno) **todos apareceram** — `fdtdec_setup()` retornou sucesso, e
`gd->fdt_blob` finalmente ficou não-nulo pela primeira vez nesta
sessão (a captura de `serial_find_console_or_panic()` logo depois
mostrou `'b'` em vez de `'n'`). **Mas a contagem de dispositivos em
`UCLASS_SERIAL` continuou `#00`** — exatamente igual a antes de
`fdtdec_setup()` sequer rodar. Definir o blob não foi suficiente.

Investigando `fdtdec_prepare_fdt()` (chamada de dentro de
`fdtdec_setup()`, entre os marcadores `'D'` e `'E'`), encontrada uma
**terceira instância da mesma classe de bug** da V188/V192:
```c
if (IS_ENABLED(CONFIG_INTELBRAS_SG1002_MR_FACTORY_PAYLOAD))
	return 0;
```
Isso pula **toda** a validação real do cabeçalho libfdt para esta
placa — significa que `fdtdec_setup()` "ter sucesso" nunca provou que
`gd->fdt_blob` aponta de fato para um FDT válido. O código de
validação real (`sg1002_fdt_probe_header()` + `fdt_check_header()`,
com seus próprios marcadores `'G'`/`'I'`/`'H'`) já existe logo abaixo
desse desvio, nunca alcançado.

## V193 — verificação direta do magic number do FDT

Uma primeira tentativa condicionou esse desvio também à flag de
runtime `GD_FLG_RELOC` (mesmo padrão da V188/V192), habilitando o
caminho real de `fdt_check_header()`. Isso **compilou**, mas **estourou
o slot apertado de 0x33000 do Stage-2** — o `fdt_check_header()` real
e seu caminho de erro (`printf`/`print_buffer`) trazem consigo 256
bytes extras de `.text`, e só havia ~590 bytes de folga. Revertida.

Em vez disso, V193 adiciona uma transmissão crua e mínima, sem puxar
nenhuma função nova, logo após `fdt_find_separate()` retornar em
`fdtdec_setup()`: lê a primeira palavra de 32 bits do blob (o campo
`magic` do cabeçalho FDT) e transmite em 8 dígitos hexadecimais via
`sg1002_fdt_trace_hex32()`. Se não for `d00dfeed` (o magic number
padrão do FDT), `gd->fdt_blob` aponta para lixo, não para o DTB
anexado da placa — resolvendo a questão de uma vez, independente do
que `fdtdec_prepare_fdt()` decida.

V193 foi compilada e verificada (apenas Stage-2 difere do dump de
fábrica) — o programador CH341A SPI inicialmente não foi detectado, e
a gravação ficou pendente até o usuário reconectá-lo. Uma vez
detectado, V193 foi gravada na SPI física em 2026-09-22 às 13:25:07,
apenas na região Stage-2 (Stage-1 idêntico ao já gravado desde a
V174), com `--confirm-write`, `--expect-sha256` e verificação pelo
CH341A (`logs/spi-write-20260922-132507.log`: `GRAVACAO PARCIAL E
VERIFICACAO OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v193-uboot-fdtdec-validate-16m.bin` (SHA-256
`9dbc402204267624e5aab02d85eb63bf7f92fa30604e4373e2eaeabedff93579`).

**Resultado físico V193 (log TTL, 2026-09-22): `00000000`.** A palavra
mágica transmitida logo após `fdt_find_separate()` foi **zero**, não
`d00dfeed` — confirmando que `gd->fdt_blob` apontava para lixo (memória
zerada), não para o DTB anexado da placa.

Investigando por quê: `_end` é uma referência de símbolo dentro de
código relocável, então `relocate_code()`/`apply_reloc()` desloca seu
valor compilado pelo mesmo offset (`gd->reloc_off`) que qualquer outro
endereço — **mas `relocate_code()` só copia
`__image_copy_end - __text_start` bytes** para a nova localização.
Conferido via `nm`: `__image_copy_end` fica em `0x2ac18` a partir de
`__text_start`, enquanto `_end` — que bate exatamente com
`__text_start` + o tamanho do arquivo `u-boot-nodtb.bin` — fica em
`0x32c18`, uma diferença de `0x8000` (32 KiB, do tamanho da tabela de
relocação `.rel`, também nunca copiada). Ou seja: pós-realocação, o
`_end` "auto-relocado" aponta para memória nunca populada na nova
localização, enquanto o DTB real de fato ainda está no endereço
**original**, pré-realocação.

## V194 — corrigir a localização do FDT com `gd->reloc_off`

A correção desfaz a auto-relocação: `gd->reloc_off` (definido por
`setup_reloc()` pré-realocação — `gd->relocaddr - CONFIG_TEXT_BASE` —
e copiado para o novo `gd` via `memcpy`) é exatamente o offset que
precisa ser subtraído de volta. `orig_blob = gd->fdt_blob -
gd->reloc_off` recupera o endereço original; se a palavra mágica ali
for `d00dfeed`, `gd->fdt_blob` passa a apontar para lá.

Essa correção, combinada com a instrumentação já acumulada, estourou o
slot apertado da V193 (que já tinha só ~10 bytes de folga) por 70
bytes. Para caber, V194 também **remove o teste de `printf()` direto
da V186** (`common/board_r.c`, `common/main.c` — as strings
`"SG1002TESTPRINTF"`/`"SG1002TESTPRINTF2"` e os marcadores `@`/`$` ao
redor delas), já que aquele achado (console sem nenhuma saída visível)
está confirmado e superado há várias versões. **A partir da V194, o
passo `#18` (`console_init_r`) e o final de `main_loop()` não mostram
mais `@`/`$` no log** — isso é esperado, não um retrocesso.

V194 foi gravada na SPI física em 2026-09-22 às 13:31:05, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-133105.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v194-uboot-fdt-reloc-fix-16m.bin` (SHA-256
`a1c3f078e0a6d66a8bd0c3f47c270654c8a5f963164caa8c798aad219aab6ef2`).

**Resultado físico V194 (log TTL, 2026-09-22): o maior avanço desta
sessão.** A sequência capturada foi:
```
...#0e<b#01{=0000006d~P=0000051
```
Decompondo:
- `<b#01` — `serial_find_console_or_panic()` entra, `gd->fdt_blob` não
  é nulo (`'b'`), e pela primeira vez em toda a sessão a contagem de
  dispositivos em `UCLASS_SERIAL` é **`#01`** (não `#00`!) — o nó
  `uart0` finalmente foi vinculado.
- `{` — `rtl8380_uart_probe()` **executa pela primeira vez em qualquer
  teste físico desta sessão inteira**.
- `=0000006d~` — a primeira chamada a `rtl8380_uart_setbrg()` (dentro
  do `.probe()` do driver, usando `current-speed=115200` do
  devicetree) completa com sucesso; divisor calculado `0x6d` (109
  decimal), batendo com `200000000/(16*115200)≈108,5→109`.
- `P` — `serial_post_probe()` (o hook do uclass) começa a rodar —
  **também pela primeira vez nesta sessão inteira**.
- `=0000051` — uma **segunda** chamada a `rtl8380_uart_setbrg()`, desta
  vez de dentro de `serial_post_probe()` (que chama
  `ops->setbrg(dev, gd->baudrate)` de novo, separadamente da chamada
  já feita pelo `.probe()`) — e a captura **para aqui**, no meio da
  transmissão do 7º de 8 dígitos hexadecimais esperados.

O arquivo de log ao vivo (`serial_ttyUSB0.log`) confirmou que não
chegou nenhum byte novo por mais de 60 segundos depois disso — não é
um paste truncado, é uma **trava real**, num ponto muito específico:
dentro ou logo após a segunda chamada de `rtl8380_uart_setbrg()`.

## V195 — isolar a trava dentro de `rtl8380_uart_setbrg()`

Como os marcadores crus desta função usam o mesmo registro `THR`
(offset 0) que `RTL8380_UART_LCR_DLAB` temporariamente reaproveita
como latch do divisor, qualquer marcador colocado **entre** as
escritas de LCR que ligam e desligam o DLAB seria silenciosamente
absorvido como bits do divisor em vez de transmitido — inútil como
diagnóstico ali dentro. V195 adiciona só **um** marcador novo (`'1'`),
logo antes da janela de DLAB começar (depois do divisor já ter sido
calculado e impresso em hexadecimal), para saber se a trava está
dentro da própria impressão dos 8 dígitos ou nas 4 escritas de
registrador que vêm depois.

V195 foi compilada e verificada (apenas Stage-2 difere do dump de
fábrica; crescimento de apenas 8 bytes de `.text`) — o programador
CH341A SPI inicialmente não foi detectado, e a gravação ficou pendente
até o usuário reconectá-lo. Uma vez detectado, V195 foi gravada na SPI
física em 2026-09-22 às 17:26:42, apenas na região Stage-2 (Stage-1
idêntico ao já gravado desde a V174), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-172642.log`: `GRAVACAO PARCIAL E
VERIFICACAO OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v195-uboot-setbrg2-trace-16m.bin` (SHA-256
`7ec0aaa5f0fad1615bbfe3f2424e9b4e6b3ff21ee4d00e144b74b5193164dcc9`).

**Resultado físico V195 (log TTL, 2026-09-22): localiza a trava com
precisão.** Captura: `...{=0000006d1~P=00000516` (fim). Decompondo:

- Primeira chamada de `rtl8380_uart_setbrg()` (dentro do `.probe()` do
  driver, divisor `0x6d`=109, 115200 bps a partir de `current-speed`
  do devicetree): `=` + `0000006d` + **`'1'` (o novo marcador da V195)**
  + as 4 escritas de registrador + `~` — **completa 100%, do início
  ao fim**.
- Segunda chamada (de dentro de `serial_post_probe()`, usando
  `gd->baudrate` — que nesta placa vem do `baudrate=9600` do ambiente
  de fábrica na SPI, carregado por `initr_env()`): `=` + `00000516`
  (divisor 1302, ≈9600 bps) — e então **o mesmíssimo marcador `'1'`,
  na mesmíssima função, nunca aparece**.

O arquivo de log bruto foi conferido byte a byte (`xxd`): **zero bytes
depois de `00000516`**, não bytes corrompidos/ilegíveis. Isso
descarta a hipótese de "só mudou de baud rate e ficou ilegível" — a
própria execução trava, não é uma questão de descompactação de sinal.
Uma segunda captura com troca automática de baud (`--boot-baud 115200
--kernel-baud 9600`, detectando dados corrompidos) **também não
mostrou nenhum byte extra**, confirmando: não é um problema de
brincar com a taxa de transmissão, é uma trava real bem no início da
segunda chamada de `setbrg()`.

## V196 — pular a resincronização redundante de baud

Como a primeira chamada (115200, vinda do devicetree) já fornece um
console perfeitamente utilizável — e é exatamente a taxa que o
adaptador TTL usou a sessão inteira —, V196 simplesmente **pula** a
segunda chamada (`ops->setbrg(dev, gd->baudrate)` dentro de
`serial_post_probe()`) para esta placa, em vez de depurar por que essa
reprogramação específica trava:
```c
if (!IS_ENABLED(CONFIG_INTELBRAS_SG1002_MR_FACTORY_PAYLOAD) && ops->setbrg) {
	ret = ops->setbrg(dev, gd->baudrate);
	...
}
```
Como a condição é conhecida em tempo de compilação para esta placa
(`IS_ENABLED(...)` é verdadeiro, então `!IS_ENABLED(...)` é falso), o
compilador elimina a chamada inteira como código morto — o binário na
verdade **encolheu 48 bytes** em vez de crescer, aliviando um pouco o
slot apertado.

V196 foi gravada na SPI física em 2026-09-22 às 18:11:31, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-181131.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v196-uboot-skip-env-baud-16m.bin` (SHA-256
`fe90c93e69ace35669e8359375a22b1d14f2990739703e060803959106deec5f`).

**Resultado físico V196 (log TTL, 2026-09-22): o melhor resultado até
agora.** Captura: `...{=0000006d1~P1RK>=00000516` (fim). A correção
funcionou exatamente como esperado — **toda a cadeia de descoberta do
console completou pela primeira vez**:

- `'1'` (marcador antigo, dentro de `serial_post_probe()`) — confirma
  `GD_FLG_RELOC` setada.
- `'R'` `'K'` — `stdio_register_dev()` **teve sucesso**! O dispositivo
  `uart0` está registrado na lista `stdio`.
- `'>'` — de volta em `serial_find_console_or_panic()`:
  `serial_check_stdout()` teve sucesso, `gd->cur_serial_dev` foi
  definido, e a função retorna normalmente.

Mas então aparece **`=00000516` de novo** — outra chamada a
`rtl8380_uart_setbrg()` com o mesmo divisor problemático (9600 bps), e
a captura para no mesmo lugar de sempre. Investigando: `serial_init()`
(`drivers/serial/serial-uclass.c`) chama `serial_setbrg()` logo depois
de `serial_find_console_or_panic()` ter sucesso — um **terceiro** ponto
de chamada que a V196 não cobria, já que só pulava a chamada de dentro
de `serial_post_probe()`.

Comparando os dois divisores: `0x6d` (115200, sempre funciona) tem o
byte alto `0x00`; `0x516` (9600, sempre trava) tem o byte alto `0x05`.
Esse byte alto é escrito no registro `IER` como `DLM` (divisor latch
high) enquanto `DLAB` está ativo. Se o isolamento de `IER` por `DLAB`
não for perfeitamente limpo neste hardware, um byte `DLM` residual
poderia vazar como bits reais de habilitação de interrupção assim que
`DLAB` é desligado — e uma interrupção não tratada nesta fase tão
inicial do boot explicaria exatamente uma trava silenciosa e
determinística como a observada.

## V197 — remascarar `IER` depois de cada `setbrg()`

Em vez de continuar corrigindo chamada por chamada, V197 ataca a causa
raiz: força `IER=0` explicitamente logo depois de **qualquer** chamada
a `rtl8380_uart_setbrg()`, independente do divisor usado — garantindo
que nenhum bit residual de `DLM` sobreviva como interrupção
habilitada de verdade. Isso deve cobrir `serial_setbrg()` e qualquer
outro chamador futuro, não só os dois pontos já encontrados.

V197 foi gravada na SPI física em 2026-09-22 às 18:16:49, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-181649.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v197-uboot-ier-remask-fix-16m.bin` (SHA-256
`008a3ed0f2c28f1492d9fc534b7f9b3f8518c00050a43ad92972ea1906439e34`).

**Resultado físico V197 (log TTL, 2026-09-22): sem nenhuma mudança.**
A captura foi **byte a byte idêntica** à da V196 —
`...{=0000006d1~P1RK>=00000516` (fim), exatamente no mesmo ponto. A
hipótese de vazamento de interrupção via `IER`/`DLAB` estava **errada**
(ou pelo menos incompleta) — a correção não teve efeito nenhum.

Reexaminando via disassembly (`objdump -d`): o marcador `'1'` de
`rtl8380_uart_setbrg()` é uma instrução `li a0,49` distinta, logo
depois do laço que imprime os 8 dígitos hexadecimais sair (não é um
dos próprios dígitos). Como a primeira chamada (115200) e a segunda
(9600) executam o **código compilado idêntico** — a função só é
compilada uma vez, chamada por ponteiro de função nas duas ocasiões —,
a única diferença entre elas é o **valor** do `baudrate` recebido. Isso
não parece mais um bug específico de caminho de código.

## V198 — testar se é um efeito de tempo acumulado de boot

Seguindo a mesma técnica que já havia refutado uma hipótese de
watchdog num ponto de parada anterior (V178, bem mais cedo no boot),
V198 insere um `udelay(2000000)` (2 segundos) cercado por marcadores
crus (`'Z'` antes, `'z'` depois) logo antes da chamada de
`serial_setbrg()` em `serial_init()`. Se o atraso for sobrevivido
integralmente e a falha ainda ocorrer no mesmo lugar relativo (não
durante o próprio atraso), isso descarta um temporizador de tempo fixo
desde o boot como causa.

V198 foi gravada na SPI física em 2026-09-22 às 18:33:05, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-183305.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v198-uboot-delay-test-16m.bin` (SHA-256
`9f2c4c38100132e78f385ea0e3a3786e88ffcba15a44b11639d6943f219364b4`).

**Resultado físico V198 (log TTL, 2026-09-22): o atraso foi
sobrevivido, mas o achado real foi outro, feito pelo usuário.**
Primeira captura, a 115200 bps de sempre: `...P1RK>Zz=00000516` —
`'Z'` e `'z'` **ambos apareceram**, confirmando que os 2 segundos de
`udelay()` foram sobrevividos integralmente, e mesmo assim a captura
"parou" exatamente no mesmo ponto relativo de sempre. Isso descarta um
temporizador de tempo fixo desde o boot como causa **para este ponto
de parada específico**.

Só que o usuário então reconectou o adaptador TTL manualmente **a
9600 bps** (`--boot-baud 9600 --kernel-baud 9600`) — e viu, depois de
um trecho inicial ilegível (o início do boot, a 115200 bps real,
decodificado incorretamente a 9600), uma sequência **perfeitamente
legível**: `#0f#10#11#12#13#14#15#16#17#18#19#1a#1b#1c#1d#1eTUVMabcdefghjk`.
**A placa nunca tinha travado.** Ela sempre trocou de verdade para
9600 bps e continuou o boot normalmente — cada `INITCALL` restante,
`run_main_loop()`, e `main_loop()` inteiro até bem antes de
`cli_loop()` (marcador `'k'`). O terminal, ainda ouvindo a 115200 bps,
simplesmente não conseguia mais decodificar nada depois da troca —
por isso parecia silêncio total. **Toda a investigação de V196 a V198
(pulos de chamada, remascaramento de `IER`, teste de atraso) estava
perseguindo um bug que não existia.**

## V199 — forçar 115200 bps de forma incondicional

Como o teste de dois bauds mostrou ser complexo demais na prática
(ferramental de duas velocidades, trecho inicial sempre ilegível de um
lado ou de outro), a decisão foi: em vez de dar suporte à troca de
banda, **eliminá-la**. `rtl8380_uart_setbrg()`
(`drivers/serial/serial_rtl8380.c`) agora ignora o `baudrate` pedido
por qualquer chamador — o `.probe()` do driver, `serial_post_probe()`,
`serial_setbrg()` de `serial_init()`, ou futuramente o callback de
ambiente "baudrate" quando `initr_env()` carregar o `baudrate=9600` de
fábrica — e força **115200 bps sempre**, para esta placa. O teste de
atraso de 2s da V198 também foi removido, por não ser mais necessário.

V199 foi gravada na SPI física em 2026-09-22 às 18:40:45, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-184045.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v199-uboot-force-115200-16m.bin` (SHA-256
`1de7f387f5a709a9d5eab465d95ff22b59c9abf3f4bf0a194ac4f47aa5a7eed7`).

**Resultado físico V199 (duas capturas, uma delas paciente, 2026-09-22):
sucesso total do boot, a 115200 bps o tempo inteiro.** Com o divisor
forçado, a segunda chamada de `rtl8380_uart_setbrg()` (de dentro de
`serial_post_probe()`) agora também usa `0x6d` (115200 bps) em vez de
`0x516` — e completa (`1~`). A partir daí, **todos** os passos
restantes disparam: `#0f` até `#1e`, depois `T U V`
(`run_main_loop()`), depois `M a b c d e f g h j k` (todo o
`main_loop()`, até bem antes de `cli_loop()`). Repetido com uma captura
paciente (sem desconectar logo após `'k'`) — resultado idêntico, sem
nada além do que já havia aparecido.

**Mas nem um byte de saída real de `printf()`/`puts()` apareceu em
lugar nenhum** — nem a contagem regressiva do `bootdelay_process()`,
nem um eventual erro do `bootm`. Isso apesar de `console_init_r()` ter
tido sucesso e toda a cadeia de dispositivo de console estar
plenamente funcional desde a V196. Investigando `common/console.c`:
`GD_FLG_HAVE_CONSOLE` — a flag que **todo** caminho de `putc()`/`printf()`
verifica antes de fazer qualquer coisa — só é definida dentro de
`console_init_f()`. E `common/board_f.c` pula
`INITCALL(console_init_f)` inteiramente para esta placa
(`#if !CONFIG_IS_ENABLED(INTELBRAS_SG1002_MR_FACTORY_PAYLOAD)`), com
um comentário sobre adiar o console para depois da realocação —
**a quarta instância nesta sessão da mesma classe de bug** (`serial_init()`
na V188, `fdtdec_setup()` na V192, `fdtdec_prepare_fdt()` na V193): um
comentário descrevendo a intenção de adiar, mas a chamada adiada nunca
foi de fato escrita em lugar nenhum.

## V200 — chamar `console_init_f()` pós-realocação

V200 adiciona `INITCALL(console_init_f);` uma única vez em
`initcall_run_r()`, logo antes de `console_init_r()` (espelhando a
ordem original pré-realocação), agora que estamos em segurança
pós-realocação. As três coisas que essa chamada faz —
`gd->flags |= GD_FLG_HAVE_CONSOLE;`, `console_update_silent()`, e
`print_pre_console_buffer()` — são seguras de executar aqui.

Como isso insere mais um passo numerado, a tabela muda de novo — agora
com 32 entradas (confirmado por `nm`/`objdump`: 32 sítios reais de
chamada a `sg1002_step` no binário ligado):

| passo  | após o INITCALL           | passo  | após o INITCALL          |
|--------|----------------------------|--------|---------------------------|
| `#00`  | (marcador líder) | `#10` | `initr_announce` |
| `#01`  | `initr_trace`               | `#11`  | `dm_announce` |
| `#02`  | `initr_reloc`                | `#12`  | `arch_initr_trap` |
| `#03`  | `event_init`                 | `#13`  | `power_init_board` |
| `#04`  | `initr_reloc_global_data`    | `#14`  | `initr_env` |
| `#05`  | `initr_barrier`              | `#15`  | `cpu_secondary_init_r` |
| `#06`  | `initr_malloc`               | `#16`  | `INITCALL_EVT(EVT_SETTINGS_R)` |
| `#07`  | `log_init`                   | `#17`  | `stdio_add_devices` |
| `#08`  | `initr_bootstage`            | `#18`  | `jumptable_init` |
| `#09`  | `fdtdec_setup`               | `#19`  | **`console_init_f` (novo na V200)** |
| `#0a`  | `initr_of_live`              | `#1a`  | `console_init_r` |
| `#0b`  | `initr_dm`                   | `#1b`  | `interrupt_init` |
| `#0c`  | `initr_lmb`                  | `#1c`  | `initr_boot_led_blink` |
| `#0d`  | `initr_dm_devices`           | `#1d`  | `board_late_init` |
| `#0e`  | `stdio_init_tables`          | `#1e`  | `initr_net` |
| `#0f`  | `serial_initialize`          | `#1f`  | `INITCALL_EVT(EVT_LAST_STAGE_INIT)` (último passo) |

V200 foi gravada na SPI física em 2026-09-22 às 18:49:11, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-184911.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v200-uboot-console-init-f-16m.bin` (SHA-256
`1c3945b36aa705d43361baa35851387bb46978783639a111122d938b317e6082`).
**Resultado físico V200 (log TTL, 2026-09-22): SUCESSO TOTAL — PRIMEIRO
PROMPT REAL DO U-BOOT DESTA SESSÃO.** Depois do passo `#19`
(`console_init_f`), pela primeira vez em toda a sessão, texto real e
legível do U-Boot aparece no fio:

```
In:    serial@18002000
Out:   serial@18002000
Err:   serial@18002000
```

— a saída de `stdio_print_current_devices()` dentro de
`console_init_r()`, confirmando que os três descritores de E/S estão
de fato associados ao dispositivo `uart0`. Os passos `#1a`-`#1d`
seguem, depois:

```
Net:   eth0: ethernet@1b000000
Hit any key to stop autoboot: 0
Wrong Image Type for bootm command
ERROR -91: can't get kernel image!
```

— `initr_net()` encontra e anuncia a interface Ethernet; a contagem
regressiva real de `bootdelay_process()` roda (capturada já perto do
fim, em `0`); o `bootcmd` de fábrica (`bootm 0xb4050000`) tenta rodar
o **próprio binário deste U-Boot** como se fosse um kernel Linux e
falha exatamente como esperado, com uma mensagem de erro real e
legível — confirmando que o `autoboot_command()` também está
plenamente funcional. Depois de `'j'`/`'k'` (últimos marcadores de
`main_loop()`), aparece:

```
RTL838x#
```

**Um prompt de shell do U-Boot real, interativo, funcionando.** Boot
completo, do zero: Stage-1 GPL (calibração DDR3, cópia, salto) →
Stage-2 GPL U-Boot (realocação, todas as INITCALLs, driver model,
devicetree, console) → prompt de comando — **sem nenhum
preloader/trampolim proprietário em qualquer ponto da cadeia**,
cumprindo o objetivo original desta investigação. Quatro instâncias da
mesma classe de bug (`serial_init()`/V188, `fdtdec_setup()`/V192,
`fdtdec_prepare_fdt()`/V193, `console_init_f()`/V200) encontradas e
corrigidas ao longo da sessão — todas comentários de código dizendo
"adiar para depois da realocação" cuja chamada adiada nunca havia sido
escrita.

V200 foi gravada na SPI física em 2026-09-22 às 18:49:11, apenas na
região Stage-2 (Stage-1 idêntico ao já gravado desde a V174), com
`--confirm-write`, `--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-184911.log`: `GRAVACAO PARCIAL E VERIFICACAO
OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v200-uboot-console-init-f-16m.bin` (SHA-256
`1c3945b36aa705d43361baa35851387bb46978783639a111122d938b317e6082`).

*Resultado V200: **PRIMEIRO PROMPT DE U-BOOT FUNCIONAL DESTA SESSÃO**
(`RTL838x#`), com `In:`/`Out:`/`Err:` reais, anúncio de rede, contagem
regressiva de autoboot, falha esperada e correta do `bootm` de
fábrica, e prompt interativo — marco principal deste esforço de
bring-up GPL — registrado por Claude Sonnet 5 (IA).*

## V201 — banner de boot padrão (versão/CPU/Board/DRAM) pós-realocação

Com o V200 dando um console de verdade, a saída de boot ainda não
lembrava um U-Boot RTL838x típico: sem banner de versão, sem linhas
`Board:`/`CPU:`/`DRAM:`. Pesquisa web confirmou o layout usual desses
boards (banner de versão, `Board:`, `CPU:...MHz`, `DRAM:`, `SPI-F:`,
`Switch Model:`). A causa é a mesma família de bug encontrada quatro
vezes nesta sessão: `display_options()`, `print_cpuinfo()`,
`show_board_info()` e `announce_dram_init()` só são chamados de
`common/board_f.c`, inteiramente pré-realocação, antes de
`console_init_f()` marcar `GD_FLG_HAVE_CONSOLE` — a quinta instância.

`CONFIG_DISPLAY_BOARDINFO_LATE` é a opção de Kconfig upstream feita
exatamente para isso: já existia, sem uso, em `common/board_r.c`
(linhas 909-914 antes desta mudança), envolvendo
`INITCALL(console_announce_r)` + `INITCALL(show_board_info)` — só
precisava ser ligada. `CONFIG_DISPLAY_CPUINFO` foi habilitada junto e
`INITCALL(print_cpuinfo)` inserido no mesmo bloco (ordem do
`board_f.c` original: banner, CPU, board). `checkboard()` é nova em
`board/intelbras/sg1002_mr/sg1002_mr.c`
(`"Board: Intelbras SG1002 MR (RTL8380M)"`), chamada automaticamente
por `show_board_info()` depois de imprimir `Model:` (lido do
devicetree).

Para o `DRAM:`, em vez de reaproveitar `show_dram_config()` (692
bytes — suporta comparação de múltiplos bancos que esta placa, com um
único banco fixo de 128 MiB, não precisa, e a margem de ~34 bytes do
slot Stage-2 não tinha como absorver), `common/board_r.c` ganhou
`sg1002_show_dram()`, um substituto mínimo que reaproveita o
`print_size()` já ligado por outros motivos. A chamada pré-realocação
original de `show_dram_config()` em `board_f.c` foi bloqueada com a
mesma guarda `#if !CONFIG_IS_ENABLED(INTELBRAS_SG1002_MR_FACTORY_PAYLOAD)`
que já cobria `announce_dram_init()` — sem isso, o compilador inlinava
os ~700 bytes de código inalcançável (sem console pré-realocação)
direto em `board_init_f()`, único jeito de a imagem caber no slot.

Orçamento final: **208.894 de 208.896 bytes — 2 bytes de margem**, a
mais apertada da sessão inteira.

Quatro novos `SG1002_STEP()` (após `console_init_r`) mudam a tabela de
32 para 36 passos (confirmado por `nm`/`objdump`: 36 sítios reais de
chamada a `sg1002_step` no binário ligado):

| passo | após o INITCALL | passo | após o INITCALL |
|-------|-------------------|-------|--------------------|
| `#00` | (marcador líder) | `#12` | `arch_initr_trap` |
| `#01` | `initr_trace` | `#13` | `power_init_board` |
| `#02` | `initr_reloc` | `#14` | `initr_env` |
| `#03` | `event_init` | `#15` | `cpu_secondary_init_r` |
| `#04` | `initr_reloc_global_data` | `#16` | `INITCALL_EVT(EVT_SETTINGS_R)` |
| `#05` | `initr_barrier` | `#17` | `stdio_add_devices` |
| `#06` | `initr_malloc` | `#18` | `jumptable_init` |
| `#07` | `log_init` | `#19` | `console_init_f` |
| `#08` | `initr_bootstage` | `#1a` | `console_init_r` |
| `#09` | `fdtdec_setup` | `#1b` | **`console_announce_r` (novo)** |
| `#0a` | `initr_of_live` | `#1c` | **`print_cpuinfo` (novo)** |
| `#0b` | `initr_dm` | `#1d` | **`show_board_info` (novo)** |
| `#0c` | `initr_lmb` | `#1e` | **`sg1002_show_dram` (novo)** |
| `#0d` | `initr_dm_devices` | `#1f` | `interrupt_init` |
| `#0e` | `stdio_init_tables` | `#20` | `initr_boot_led_blink` |
| `#0f` | `serial_initialize` | `#21` | `board_late_init` |
| `#10` | `initr_announce` | `#22` | `initr_net` |
| `#11` | `dm_announce` | `#23` | `INITCALL_EVT(EVT_LAST_STAGE_INIT)` (último passo) |

V201 foi gravada na SPI física em 2026-09-22 às 19:43:24, apenas na
região Stage-2 (Stage-1 idêntico desde a V174), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-194324.log`: `GRAVACAO PARCIAL E
VERIFICACAO OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v201-uboot-boot-banner-16m.bin` (SHA-256
`7c44d98dca42dd496b4b4e0435debcfa2b566cf49369164f7ab03603e88a6b27`).
**Resultado físico V201 (log TTL, 2026-09-22):** depois de `In:`/`Out:`/`Err:`
e dos passos `#1a`-`#1e`, aparecem as novas linhas:

```
Realtek RTL8380 OTTO (MIPS 4KEc)
Model: RTL8380M_INTPHY_2FIB_1G_DEMO
Board: Intelbras SG1002 MR (RTL8380M)
DRAM:  128 MiB
```

— `print_cpuinfo()`, `show_board_info()` (modelo do devicetree +
`checkboard()`) e `sg1002_show_dram()`, todas funcionando. (Na captura
bruta via `watch_ttl.py`, `Model:` aparece com cada caractere separado
por espaço — artefato cosmético do `DiagnosticBurstFormatter`: a
string do devicetree, `RTL8380M_INTPHY_2FIB_1G_DEMO`, tem 29 bytes sem
nenhum espaço interno, acima do limiar de 24 bytes usado para
identificar rajadas de marcadores de diagnóstico; os bytes reais no
fio estão corretos, é só a heurística de formatação que não tinha como
saber que essa palavra específica era texto real e não uma rajada.)
Depois seguem `Net:`, a contagem de autoboot, a falha esperada do
`bootm` de fábrica e o prompt `RTL838x#`, idênticos à V200.

**Lacuna encontrada nesta mesma verificação:** a linha de banner de
versão (`U-Boot 2026.07 (...)`) continua ausente. Causa raiz
identificada: `console_announce_r()` chama
`console_puts_select(stdout, false, buf)` — o `false` é o parâmetro
`serial_only`, então essa chamada só escreve em dispositivos de
console *não seriais* (ex.: vídeo); o único dispositivo de console
desta placa é a UART serial, então a chamada não tem para onde
escrever. É um comportamento deliberado do upstream: normalmente o
banner chega à serial via `display_options()` pré-realocação
(diretamente, ou por `print_pre_console_buffer()` se
`CONFIG_PRE_CONSOLE_BUFFER` estiver ligado), e `console_announce_r()`
só precisa alcançar dispositivos que só existem depois da realocação
(vídeo). Nenhum dos dois caminhos vale para esta placa:
`CONFIG_PRE_CONSOLE_BUFFER` está desligado, e `display_options()` em
si é pulada pré-realocação pela mesma guarda que já bloqueava
`announce_dram_init()`. Correção identificada (chamar `display_options()`
diretamente, pós-realocação — ela usa `printf()` puro, que alcança a
serial normalmente) mas ainda não compilada/gravada/verificada;
fica para uma V202.

*Resultado V201: banner `CPU:`/`Board:`/`DRAM:` confirmados
funcionando na SPI física, orçamento de flash no limite absoluto
(2 bytes de margem), e causa raiz do banner de versão ausente já
identificada para a V202 — registrado por Claude Sonnet 5 (IA).*

## V202 — corrigir o banner de versão ausente e aproximar o layout do padrão RTL838x

Corrige exatamente a causa raiz identificada na V201:
`console_announce_r()` faz `console_puts_select(stdout, false, buf)` —
o `false` é `serial_only`, então essa chamada só escreve em
dispositivos de console *não seriais* (ex.: vídeo). Esta placa só tem
a UART serial como console, então a chamada nunca tinha para onde
escrever. `display_options()` (a função que `board_f.c` chamaria
normalmente pré-realocação) usa `printf()` puro, que alcança a serial
normalmente. Trocado `INITCALL(console_announce_r)` por
`INITCALL(display_options)` em `common/board_r.c`.

Bônus de orçamento: como nada mais nesta placa chama
`console_announce_r()` nem `console_puts_select_stderr()` (só usada
por `drivers/video/vidconsole-uclass.c`, não compilado aqui), as
funções `console_announce_r()` + `console_puts_select()`/`.part.0`
(~268 bytes) saem do binário por não terem mais nenhum chamador — mais
do que compensa o custo de `display_options()`. **A V202 tem MAIS
margem de flash que a V201** (122 bytes antes da troca do model do
devicetree, ~102 depois), revertendo a crise dos 2 bytes de margem da
V201.

`checkboard()` (`board/intelbras/sg1002_mr/sg1002_mr.c`) ganhou uma
linha de clock `CPU:500MHz LXB:200MHz MEM:300MHz` (o plano de clock
público e documentado da família RTL838x — não é uma leitura em tempo
de execução, essa família de SoC não tem registradores de consulta de
árvore de clock; o LXB bate com o `CONFIG_SYS_MIPS_TIMER_FREQ=200000000`
já verificado desta placa) e uma linha `SPI-F: 1x16 MB` (o tamanho
real do chip Winbond W25Q128 desta placa, confirmado pelo próprio
`flashrom` nesta sessão). O `model` do devicetree
(`arch/mips/dts/rtl8380-intelbras-sg1002-mr.dts`) foi trocado do nome
genérico de referência `RTL8380M_INTPHY_2FIB_1G_DEMO` para o nome real
usado no lado Linux do projeto, `Intelbras SG 1002 MR L2+ (RTL8380M
INTPHY 2FIB 1G)` (de `target/linux/realtek/dts/rtl8380_intelbras_sg1002-mr.dts`
no checkout OpenWrt separado `/media/dados_2tb/openwrt-build-tools/openwrt`),
para consistência entre os dois lados do projeto — e, efeito colateral
bem-vindo, essa string tem espaços internos, então não aciona mais o
limiar de "rajada de diagnóstico" do `watch_ttl.py` que espaçava o
`Model:` letra por letra na V201.

**Contexto:** o usuário pediu um layout de boot parecido com U-Boots
RTL838x históricos reais (dois exemplos colados, de 2014/2015), citando
também uma imagem antiga deste mesmo projeto (preloader vendor original
+ um U-Boot modificado, trilha documentada em
`doc/board/intelbras/sg1002-mr.rst`, v14-v28, anterior a este esforço de
preloader GPL) como referência de estética. Buscar e regravar essa
imagem antiga acabou sendo um desvio caro (ver a lição registrada na
memória do projeto) — a decisão final, a pedido explícito do usuário,
foi montar o banner do zero, no estilo dos exemplos, usando só fatos
verdadeiros sobre esta placa, sem tentar re-portar código antigo.
Linhas como config de PHY RTL8218B ou identificação de switch chip
foram deliberadamente **omitidas** por não serem, ainda, coisas que
este U-Boot realmente faz.

Nenhum passo novo foi adicionado à tabela de `initcall_run_r()` (a
troca de `console_announce_r` por `display_options` é uma substituição
1-para-1 no mesmo sítio de `SG1002_STEP()`) — a tabela de 36 passos da
V201 continua válida, só com `#1b` agora correspondendo a
`display_options` em vez de `console_announce_r`.

V202 foi gravada na SPI física em 2026-09-22 às 20:44:49, apenas na
região Stage-2 (Stage-1 idêntico desde a V174), com `--confirm-write`,
`--expect-sha256` e verificação pelo CH341A
(`logs/spi-write-20260922-204449.log`: `GRAVACAO PARCIAL E
VERIFICACAO OK`, região `0x050000-0x082fff`). Imagem
`sg1002-mr-v202-uboot-boot-banner-16m.bin` (SHA-256
`6aa9ea2869efa439dd36247481d95013de514fe12e4673999f9ff537667fe54d`).
**Resultado físico V202 (log TTL, 2026-09-22): SUCESSO — primeiro
banner de versão real desta sessão.** Depois de `In:`/`Out:`/`Err:` e
do passo `#1a`, aparece:

```
U-Boot 2026.07-g0a7264936092-dirty (Sep 22 2026 - 20:39:02 -0300)

Realtek RTL8380 OTTO (MIPS 4KEc)
Model: Intelbras SG 1002 MR L2+ (RTL8380M INTPHY 2FIB 1G)
Board: Intelbras SG1002 MR L2+  CPU:500MHz LXB:200MHz MEM:300MHz
SPI-F: 1x16 MB
DRAM:  128 MiB
```

— todas as linhas planejadas aparecem, e pela primeira vez `Model:`
não é mais espaçado letra por letra (a string agora tem espaços
internos de verdade). A captura bruta via `watch_ttl.py` mostrou a
string de versão espaçada letra por letra (`U-Boot 2026 . 0 7 - g
0a726493 6092 - d i r t y ...`) — o usuário inicialmente leu isso como
"a correção não funcionou", mas os bytes reais no fio estavam
corretos; era o mesmo artefato cosmético do `DiagnosticBurstFormatter`
já visto no `Model:` da V201, agora batendo na string de versão
(`git describe`, longa, sem espaço interno). Corrigido no
`recovery-lab` (commit `b7d0364`, script separado, ver a memória do
projeto): qualquer trecho contendo um ponto (`.`) agora é tratado como
texto real, nunca como rajada de diagnóstico, já que nenhum marcador
deste projeto usa ponto. Resto do boot (`Net:`, contagem de autoboot,
falha esperada do `bootm`, prompt `RTL838x#`) idêntico à V200/V201.

*Resultado V202: banner de versão real confirmado pela primeira vez
nesta sessão, layout de boot muito mais próximo do padrão RTL838x
histórico, MAIS margem de flash que a V201 apesar das adições, e
formatador de TTL corrigido para não confundir a string de versão com
uma rajada de diagnóstico — registrado por Claude Sonnet 5 (IA).*

## V203 — banner de versão antes de `console_init_r()`

Pedido do usuário: o banner de versão deveria ser o primeiro texto
real, antes até da linha `In:`/`Out:`/`Err:` de `console_init_r()`,
igual num U-Boot upstream típico (`display_options()` roda
pré-realocação, bem antes de `console_init_r()`). Movido o bloco
inteiro (`display_options`/`print_cpuinfo`/`show_board_info`/
`sg1002_show_dram`) em `common/board_r.c` pra antes de
`INITCALL(console_init_r)`. Confirmado seguro **lendo** `common/console.c`
antes de mexer, não só testando: `putc()` só passa pela camada DM/stdio
depois que `GD_FLG_DEVINIT` é setado, e essa flag só é setada no
**final** de `console_init_r()` (depois do próprio `In:`/`Out:`/`Err:`)
— até lá, `putc()` cai direto em `serial_putc()` sempre que
`GD_FLG_HAVE_CONSOLE` estiver setada (já true desde o `console_init_f()`
da V200), o mesmo caminho que todo marcador cru desta sessão já usa.
Reordenação pura, nenhum `INITCALL` novo/removido, mesma tabela de 36
passos e mesmo tamanho de binário da V202.

Gravada na SPI física em 2026-09-22 às 20:44:49 (região Stage-2,
`--confirm-write`/`--expect-sha256`, `GRAVACAO PARCIAL E VERIFICACAO
OK`). Imagem `sg1002-mr-v203-uboot-boot-banner-16m.bin` (SHA-256
`c1748454f42cf9b021dfcc236026877e04869f2832eee0bec9a4184cc0621fa8`).
**Resultado físico: confirmado** — banner aparece antes de
`In:`/`Out:`/`Err:` como pedido.

*Resultado V203: banner reordenado com sucesso, sem custo de tamanho —
registrado por Claude Sonnet 5 (IA).*

## V204 — realocação do Stage-2, ethernet automática, `bootcmd` dinâmico, ambiente compilado

Pedido grande do usuário, cinco partes, todas confirmadas fisicamente
num único boot automático (sem nenhum comando manual):

**1. Stage-2 realocado de `0x050000` para `0x008000`.** O "bootrom"
vendor original (uImage VxWorks) sempre ocupou `0x050000-0x14ffff`;
toda build de Stage-2 desde a V174 sobrescrevia o começo dele. Mover o
Stage-2 pro próprio espaço sobrando do loader (`0x008000-0x03ffff`,
entre o Stage-1 de 32 KiB e o ambiente em `0x040000` — antes
contaminado com resíduo de uma fase anterior do projeto, de janela
mais larga) libera `0x050000+` por completo e aumenta a margem do
Stage-2 de ~34 bytes pra ~20 KiB. Mudança em
`arch/mips/mach-rtl-otto/preloader/sg1002_mr_sram.S`:
`GPL_UBOOT_FLASH`/`GPL_UBOOT_COPY_LEN` são macros usadas em todo o
arquivo (inclusive nos ~30 blocos históricos de experimentos), então
essa única mudança cobre todo mundo.

**2. `eth_init()` chamado direto em `board_late_init()`**
(`board/intelbras/sg1002_mr/sg1002_mr.c`), em vez de esperar o primeiro
`ping`/`dhcp`/`tftpboot` disparar `rtl838x_eth_start()` (a lógica de
PHY em `drivers/net/rtl838x_eth.c` já existia e já funcionava — só
nunca rodava sozinha).

**3. `bootcmd` decidido em C, dinamicamente.** Lê o magic de imagem
legada direto no endereço mapeado em SPI `0xb4150000` (onde o vendor
tinha o filesystem proprietário — um futuro ponto de instalação do
OpenWrt) e usa `bootm 0xb4150000` se válido, senão cai pra
`bootm 0xb4050000` (VxWorks, agora intocado no lugar original).
`CONFIG_HUSH_PARSER` está desligado (parser simples), então
`"bootm A || bootm B"` como string estática de `bootcmd` **não
funciona silenciosamente** — o parser simples não tem operadores, e
passa `"||"`/`"bootm"`/`"0xb4050000"` como argumentos extras pro
primeiro `bootm`. Decidir em C evita isso completamente.

**4. Ambiente compilado, com CRC válido, gravado em `0x040000`,**
preservando as 23 variáveis originais do vendor (`bootargs`, as macros
de recuperação `update_*`/`nuke_env`/`rtkon` etc.) — só troca o
`ethaddr` placeholder do vendor (`DE:AD:BE:EF:01:02`) pelo MAC real de
fábrica (`D8:36:5F:90:D7:26`, recuperado de SPI `0x141038`, a partição
MTD "factory" que o lado Linux já usa). `bootcmd` fica deliberadamente
ausente do blob pra lógica do item 3 rodar todo boot;
`setenv bootcmd ...; saveenv` ainda sobrescreve permanentemente se
precisar, igual `ethaddr`/`bootargs` já fazem. Gerador:
`tools/make-sg1002-mr-v204-env-blob.py`.

**5. Checada a região reservada `0xFD0000`** por algo aproveitável
(ex.: um segundo MAC) — está inteiramente apagada (`0xFF`), nada pra
usar ali.

Toda diferença entre a nova imagem de 16 MiB e o dump original
intocado (11/set) foi verificada byte a byte antes de gravar,
confirmando que fica inteiramente dentro de `0x000000-0x04ffff`
(`0x050000` em diante — kernel VxWorks e filesystem vendor — voltou a
ficar idêntico ao dump original). Gravada via CH341A (chip inteiro,
já que isso muda a própria lógica de cópia do Stage-1) em 2026-09-23
às 00:32:57 (`GRAVACAO E VERIFICACAO OK`, SHA-256
`c906ecd5a6421fc518621a24de3ccb35a462899248631b6f4daff131ea4c0375`).

**Janelas de flash autorizadas mudaram — atualização importante:**
Stage-1 continua `0x000000-0x007fff`; Stage-2 agora é
`0x008000-0x03ffff` (antes `0x050000-0x082fff`); ambiente
`0x040000-0x04ffff` agora é ativamente gravado por nós (antes só
preservado). `0x050000` em diante nunca mais deve ser tocado — é o
kernel VxWorks e o filesystem vendor, restaurados de propósito.

Um bug no `bootcmd` (`||` não suportado, resultando em
`Wrong Image Type for bootm command` / `ERROR -91`) foi corrigido
**depois** dessa gravação inicial, só no Stage-2 + ambiente, via **TFTP
+ `sf erase`/`sf write` do próprio U-Boot** — sem CH341A. Estabelece o
fluxo de autoatualização que o usuário pediu: servidor TFTP local
(`dnsmasq`, adaptador USB RTL8153 dedicado pra isso, sem mexer na
infra compartilhada) serve o `u-boot.bin`/blob de ambiente novos, a
board baixa pra RAM com `tftpboot`, grava com `sf erase`+`sf write` e
verifica com `sf read`+`cmp.b` — tudo de dentro do próprio U-Boot
rodando. Certo pra atualizações que não mexem no Stage-1 (que ainda
está executando durante a gravação); mudanças no Stage-1 continuam
exigindo CH341A externo, por segurança.

**Resultado físico final (log TTL, 2026-09-23), boot totalmente
automático, sem nenhum comando manual:**

```
U-Boot 2026.07-g70f894f32107-dirty (Sep 23 2026 - 07:34:10 -0300)

Realtek RTL8380 OTTO (MIPS 4KEc)
Model: Intelbras SG 1002 MR L2+ (RTL8380M INTPHY 2FIB 1G)
Board: Intelbras SG1002 MR L2+  CPU:500MHz LXB:200MHz MEM:300MHz
SPI-F: 1x16 MB
DRAM:  128 MiB
In:    serial@18002000
Out:   serial@18002000
Err:   serial@18002000
rtl8380: PHY 10 ID 001c:ca40 BMSR 79ed
rtl8380: PHY links 00000400 after 3600 ms; MAC links 00000000; TX mask 00000400
Net:   eth0: ethernet@1b000000
Hit any key to stop autoboot: 0
## Booting kernel from Legacy Image at b4050000 ...
   Image Name:   bootrom
   Image Type:   MIPS Linux Kernel Image (gzip compressed)
   Data Size:    667220 Bytes = 651.6 KiB
   Verifying Checksum ... OK
   Uncompressing Kernel Image to 81c00000

Starting kernel ...
```

Ethernet negociou link sozinha (sem `ping`/`tftpboot` manual);
`bootcmd` detectou corretamente que não há OpenWrt em `0x150000` e
caiu pro VxWorks, lido **direto da flash, na posição original**,
checksum válido, descompactação OK. Trava em "Starting kernel..." pelo
mesmo motivo já documentado (estado de exceção da CPU, `Status.BEV`/
`EBase`, nunca restaurado antes do salto pro kernel legado) — não é
regressão desta mudança, é o próximo item pendente se o usuário quiser
o VxWorks realmente bootando.

*Resultado V204: realocação do Stage-2, ethernet automática, `bootcmd`
dinâmico e ambiente compilado confirmados juntos num único boot físico
automático; fluxo de autoatualização via TFTP + `sf` estabelecido e
validado — registrado por Claude Sonnet 5 (IA).*

## V204.1 — 🎯 OpenWrt instalado em `0x150000` e BOOT COMPLETO ATÉ O SHELL

Com o `bootcmd` dinâmico da V204 provado (caiu certo pro VxWorks quando
não havia nada em `0x150000`), o passo óbvio era testar o outro lado:
gravar uma imagem válida ali. Achada pronta em
`fabiano@fabiano-zh01:/media/dados_2tb/openwrt-build-tools/openwrt`
(checkout OpenWrt separado, mesma máquina): `bin/targets/realtek/rtl838x/
openwrt-realtek-rtl838x-intelbras_sg1002-mr-initramfs-kernel.bin`
(12.626.778 bytes, 14/set, branch `feature/intelbras-sg1002-mr`
— a variante mais recente e completa, com o trabalho de LED/porta já
documentado em [[reference-sg1002-mr-rtl8231-led-hardware]]). Já em
formato uImage legado (`27 05 19 56`), cabe fácil nos 14,5 MiB
disponíveis em `0x150000-0xFCFFFF`.

Gravado inteiramente via **TFTP + `sf` do próprio U-Boot** (o fluxo de
autoatualização estabelecido na V204, agora provado numa imagem grande
de verdade): `tftpboot` pra RAM (`0x82000000`), `sf erase 0x150000
0xc0b000`, `sf write 0x82000000 0x150000 0xc0ab5a`, verificado com
`sf read` numa área RAM separada + `cmp.b` (12.626.778 bytes idênticos).
Nenhum CH341A usado.

**Resultado físico: BOOT COMPLETO, DO RESET AO SHELL, TODO AUTOMÁTICO.**
Depois do banner/ethernet automáticos de sempre, `bootcmd` detecta o
magic válido em `0x150000` e escolhe OpenWrt em vez do VxWorks:

```
## Booting kernel from Legacy Image at b4150000 ...
   Image Name:   MIPS OpenWrt Linux-6.18.44
   Image Type:   MIPS Linux Kernel Image (uncompressed)
   Data Size:    12626714 Bytes = 12 MiB
   Verifying Checksum ...
Starting kernel ...

rt-loader
Running on RTL8380M rev C (6275) SoC with 128 MB
[    0.000000] Linux version 6.18.44 ...
[    0.000000] rtl83xx-clk: initialized, CPU 500 MHz, MEM 193 MHz (8 Bit DDR3), LXB 200 MHz
[    1.272176] Creating 1 MTD partitions on "spi0.0":
[    1.272176] 0x000000140000-0x000000150000 : "factory"
[    3.890570] rtl838x_eth ...: Using MAC d8:36:5f:90:d7:26
[    6.430381] rtl83xx-switch ...: Link is Up - 1Gbps/Full - flow control off
[    6.440876] ... lan1..lan8 (uninitialized): PHY [realtek-mdio-0:08..0f] driver [Realtek RTL8218B (internal)]
[    7.247541] Run /init as init process
...
Please press Enter to activate this console.

BusyBox v1.38.0 built-in shell (ash)
 -----------------------------------------------------
 OpenWrt SNAPSHOT, r342+4-3d1645ee26
 -----------------------------------------------------
root@OpenWrt:~#
```

Kernel bate a versão certa (500 MHz confirmado em tempo de execução,
igual ao `checkboard()` documentou), a partição MTD `factory`
(`0x140000-0x150000`, a mesma usada pro MAC real) é reconhecida, os 8
PHYs RTL8218B internos são detectados corretamente como `lan1`-`lan8`,
o MAC real (`D8:36:5F:90:D7:26`) funciona no driver de kernel também
(não só no U-Boot), link Gigabit negociado, `procd` sobe, módulos
carregam (WireGuard, batman-adv, PPP), e o BusyBox ash chega no
prompt. Únicos avisos: RTL8231 (LED expander) não inicializa —
esperado, não portado ainda (ver [[reference-sg1002-mr-rtl8231-led-hardware]]).

**Isso fecha o objetivo original desta investigação inteira**: cadeia
de boot 100% GPL, do reset físico até um shell Linux real e utilizável,
sem nenhum preloader/binário proprietário em qualquer ponto da cadeia
— Stage-1 GPL → Stage-2 GPL U-Boot → kernel GPL → userspace OpenWrt.

*Resultado V204.1: primeiro boot completo do OpenWrt nesta placa, do
reset ao shell, 100% automático — registrado por Claude Sonnet 5 (IA).*

## V205 — corrigir `saveenv` (sétima instância do mesmo padrão de bug)

Usuário reportou: `setenv`+`saveenv` não estava persistindo nada.
Causa: `common/board_f.c` também pula `INITCALL(env_init)` inteiro pra
esta placa (mesma guarda `#if !CONFIG_IS_ENABLED(...)` do
`console_init_f()`), então `env_sf_init()` (`env/sf.c`) nunca roda e
`gd->env_has_init` nunca ganha o bit do backend SPI
(`env_set_inited()` só é chamado de dentro do laço do próprio
`env_init()`). `initr_env()` (incondicional, mais adiante) carrega o
ambiente normalmente por um caminho independente — por isso
`printenv` sempre funcionou — mas `env_save()` verifica
`env_has_inited(drv->location)` antes de salvar, então `saveenv`
sempre falhava com "not initialized", silenciosamente.

Chamado `INITCALL(env_init)` uma vez, pós-realocação, logo depois do
`console_init_f()` da V200. Gravado via TFTP + `sf` do próprio U-Boot
(sem CH341A). **Resultado físico: `saveenv` agora reporta "OK" e
sobrevive a um `reset`.**

## V206 — `bootcmd` reavaliado todo boot, endereços configuráveis por env

Com o `saveenv` corrigido na V205, apareceu um efeito colateral: a
lógica de detecção OpenWrt/VxWorks da V204 só rodava uma vez (guardada
por `!env_get("bootcmd")`), e a decisão CONGELAVA no ambiente salvo
pra sempre — sem nenhuma variável de ambiente pra mudar isso depois
sem editar a string `bootm 0x...` na mão. Antes da V205 isso não dava
pra perceber, porque o `saveenv` quebrado fazia a checagem
recomeçar sozinha todo boot, por acidente.

Agora a checagem roda de novo todo boot por padrão (instalar/remover
um OpenWrt em `$openwrt_addr` já vale no próximo boot, sem precisar de
`setenv`), e os dois endereços checados são configuráveis via
`env_get_hex("openwrt_addr", ...)`/`env_get_hex("vxworks_addr", ...)`.
Uma variável nova `boot_auto` (`env_get_yesno()`, padrão "não
definida" = ligado) permite congelar uma escolha manual: `setenv
boot_auto 0; setenv bootcmd '...'; saveenv` — mesmo padrão de
sobrescrita que `ethaddr`/`bootargs` já usam.

Gravado via TFTP + `sf` (sem CH341A). **Resultado físico: `bootcmd`
confirmado se redecidindo a cada reboot** (`bootm 0xb4150000`, já que
o OpenWrt instalado na V204.1 continua válido). Aproveitado pra também
corrigir o valor salvo de `baudrate` (estava `9600`, herdado do
ambiente vendor original, mas a UART sempre roda forçada em 115200
desde a V199 — `setenv baudrate 115200; saveenv` deixa o valor salvo
condizente com a realidade, sem reabrir a complexidade de dual-baud
que a V199 deliberadamente evitou).

*Resultado V205/V206: `saveenv` funcionando de verdade,
`bootcmd`/`baudrate` finalmente configuráveis e consistentes via
variáveis de ambiente salvas — registrado por Claude Sonnet 5 (IA).*

## V207 — diagnóstico: o ambiente nunca carrega da SPI, só usa o default

Usuário testou fisicamente `setenv baudrate 115200; saveenv`, seguido de
um `reset`, e reportou `baudrate` voltando pra `9600` — contradizendo
diretamente a alegação de "resolvido" da V206. Um `sf read`+`md.b` bruto
no offset `0x040000` confirmou de forma independente que
`baudrate=115200` estava genuinamente gravado na flash — então o
caminho de GRAVAÇÃO estava correto e o bug tinha que estar no caminho
de CARGA.

Adicionado rastreamento bruto pré-console temporário: nos argumentos do
callback `on_baudrate()` (`drivers/serial/serial-uclass.c`) e um dump de
`env_get("baudrate")` logo após `INITCALL(initr_env)`
(`common/board_r.c`). Resultado físico: `on_baudrate()` disparou uma
única vez com `op=env_op_create` e `value="9600"` (o default compilado
de `CONFIG_BAUDRATE`) — nunca com o `"115200"` que estava de fato na
flash. **O ambiente da SPI nunca estava sendo lido; toda inicialização
usava o ambiente default compilado**, mascarado pra quase todas as
outras variáveis: `bootcmd` é recalculado incondicionalmente pelo
`board_late_init()` (V206), `bootargs`/`ethaddr` só quando ausentes.
`baudrate` era a única variável que o código da placa nunca toca,
então foi a única capaz de expor o bug.

Causa raiz: `env_relocate()` (`env/common.c`) só chama o `env_load()`
real quando `gd->env_valid != ENV_INVALID`; caso contrário vai direto
pro `env_set_default()`, sem nunca tentar ler a flash. `gd->env_valid`
começa zerado (`ENV_INVALID`) e normalmente é definido cedo pelo hook
`.init` de cada backend, dentro de `env_init()` — pra
`ENV_IS_IN_SPI_FLASH` isso é `env_sf_init_addr()` (`env/sf.c`), que
faz uma checagem de CRC rápida via `CONFIG_ENV_ADDR` mapeado em
memória. Esta placa deixa `CONFIG_ENV_ADDR=0x0` (não definido) e não
tem `CONFIG_ENV_SPI_EARLY`, então essa checagem sempre retorna
`-ENOENT` sem nunca tocar `gd->env_valid` — e a V205 tinha colocado
`INITCALL(env_init)` **depois** de `INITCALL(initr_env)`, tarde demais
pra importar de qualquer forma.

## V208 — mover `env_init()` para antes de `initr_env()`, correção real

A correção não precisa de `CONFIG_ENV_ADDR`: o próprio `env_init()`
(`env/env.c`) já tem um fallback pra exatamente este caso — se todo
backend retorna `-ENOENT`, ele mesmo força `gd->env_valid = ENV_VALID`
(aponta `env_addr` pro `default_environment[]`, mas `env_relocate()`
ignora esse endereço; `env_sf_load()` faz sua própria leitura
independente via `spi_flash_read()` no `CONFIG_ENV_OFFSET`/
`CONFIG_ENV_SIZE` reais). Bastou mover `INITCALL(env_init)` pra rodar
**antes** de `INITCALL(initr_env)` — `gd->env_valid` vira `ENV_VALID`
de qualquer jeito, e `env_relocate()` passa a tomar o ramo `env_load()`
de verdade, fazendo a leitura real da SPI + checagem de CRC que esta
placa tem desde a V205 sem nunca ter exercitado.

V208 recompila com a mesma correção e remove a instrumentação de
diagnóstico da V207 (binário volta ao tamanho da V206: `209266` bytes).
Gravado via TFTP + `sf` do próprio U-Boot (sem CH341A), verificado
byte a byte antes do `reset`.

**Resultado físico, confirmado em três ciclos de reset separados**:
`baudrate=115200` sobrevive a `setenv`+`saveenv`+`reset` corretamente
(antes revertia pra `9600` todo boot). Testado também com uma variável
arbitrária (`setenv testpersist v208ok; saveenv`) pra confirmar que a
correção é geral — qualquer `setenv`+`saveenv` agora sobrevive a um
reboot, não só `baudrate`.

*Resultado V207/V208: causa raiz do ambiente nunca carregando da SPI
identificada por rastreamento direto no hardware, e corrigida —
`baudrate` e qualquer outra variável salva agora sobrevivem a um
`reset` de verdade — registrado por Claude Sonnet 5 (IA).*

## V209 — partição env gerada pelo compilador, script Python aposentado

Usuário pediu explicitamente: o ambiente gravado tem que vir do
compilador, não de um script Python separado mantido à mão (que já
tinha ficado desatualizado uma vez, exigindo os live-patches de
`baudrate`/`bootcmd` da V206). O próprio U-Boot já tem suporte de
primeira classe pra isso: um arquivo texto
`board/<vendor>/<board>/<CONFIG_SYS_BOARD>.env` é descoberto
automaticamente pelo build (`env/Kconfig`'s `CONFIG_ENV_SOURCE_FILE`,
`doc/usage/environment.rst`) e compilado direto dentro de
`default_environment[]` via `CONFIG_EXTRA_ENV_TEXT` — sem precisar
mexer no defconfig pra isso.

Criado `board/intelbras/sg1002_mr/sg1002_mr.env`, preservando as mesmas
23 variáveis originais de fábrica que o script antigo
(`tools/make-sg1002-mr-v204-env-blob.py`, agora superado) tinha
hardcoded, com o MAC real (`D8:36:5F:90:D7:26`). `baudrate`/`bootdelay`
ficaram de fora do arquivo (`CONFIG_BAUDRATE`/`CONFIG_BOOTDELAY` já
cobrem os dois — evita uma entrada duplicada e desperdiçada no ambiente
compilado) e `bootcmd` continua deliberadamente ausente (calculado todo
boot pelo `board_late_init()`, V206). Corrigido também
`CONFIG_BAUDRATE=9600` → `115200` no defconfig, já que nunca batia com
a taxa real forçada da UART (V199) e era exatamente esse tipo de
entrada duplicada.

`tools/build-sg1002-mr-v209-compiled-env.sh` transforma o MESMO
ambiente default compilado em uma imagem gravável, usando ferramentas
padrão do próprio U-Boot: `make u-boot-initial-env`
(`tools/printinitialenv`) despeja `default_environment[]` de volta como
texto, e `tools/mkenvimage -b -p 0x00 -s CONFIG_ENV_SIZE` codifica isso
como uma imagem de ambiente de verdade. **`-b` (big-endian) é
obrigatório** — a ferramenta por padrão usa CRC little-endian, o que
gera silenciosamente um blob que a checagem de CRC deste alvo MIPS
big-endian rejeita (pego com um build descartável antes de fechar nas
flags certas). `-p 0x00` casa com o padding real confirmado no dump de
fábrica original.

Produz dois artefatos prontos pra gravar por build: uma imagem
"stage2+env" (`u-boot.bin` + `env.bin` emendados, `0x008000-0x04ffff`,
pro workflow já estabelecido de TFTP + `sf`) e uma imagem completa
(Stage-1 + stage2+env, `0x000000-0x04ffff`, pra gravação via CH341A
`--region` se algum dia uma mudança de Stage-1 estiver envolvida) —
ambas cobrem exatamente a região que este projeto tem autorização pra
tocar, sem nunca precisar adivinhar o conteúdo em `0x050000` em diante
(VxWorks + filesystem vendor + o OpenWrt instalado em `0x150000` desde
a V204.1).

Gravado via TFTP + `sf` (stage2+env, sem CH341A), verificado byte a
byte antes do `reset`. **Resultado físico**: `printenv` mostra as 21
variáveis preservadas, o MAC real de fábrica e `baudrate=115200`,
genuinamente lidos da partição compilada (tamanho do ambiente
`1076/65532` bytes, não mais o rodapé pequeno do default sozinho).
`setenv`+`saveenv`+`reset` reconfirmado funcionando em cima desse
ambiente maior.

*Resultado V209: partição env agora gerada pelo compilador a partir de
`sg1002_mr.env`, via `CONFIG_EXTRA_ENV_TEXT` + `u-boot-initial-env` +
`mkenvimage`, substituindo o gerador Python da V204 — registrado por
Claude Sonnet 5 (IA).*

## V210 — `baudrate` passa a ser respeitado de verdade (com ressalva)

Usuário pediu pra reabrir a questão da V195/V199: dava pra fazer
`baudrate` funcionar de verdade, e não só ser decorativo? A V199 tinha
forçado a UART em 115200 incondicionalmente, ignorando qualquer valor
pedido, especificamente pra parar de testar trocas de baud — toda
tentativa da V194-V198 com um divisor genuinamente diferente parecia
travar logo depois de `serial_setbrg()` começar. Mas TODOS esses testes
rodaram antes da V200 (`console_init_f()` nunca chamada), da V205
(`env_init()` nunca chamada) e da V208 (o ambiente nunca era lido da
flash de jeito nenhum) — o subsistema inteiro de console/ambiente
estava quebrado silenciosamente naquela época, então "trava" nunca foi
isolado de verdade dentro do código da UART.

Removida a força da V199 em `rtl8380_uart_setbrg()`
(`drivers/serial/serial_rtl8380.c`) e reaberto o teste físico agora que
o subsistema está corrigido. Achado um segundo fator contribuinte no
processo: um watchdog deixado armado pelo Stage-1/bootstrap de fábrica
só era desligado dentro do `board_late_init()` (passo ~#1b) — bem mais
tarde que o ponto onde a trava acontece (~passo #14, durante a
importação do ambiente). Movida a mesma dupla de escritas de registrador
pra rodar cedo em `common/board_r.c`, logo antes de
`INITCALL(initr_env)`.

**Resultado físico, sete resets com `baudrate=9600` salvo no ambiente**
(gravado via TFTP + `sf`, workflow já estabelecido): **6 completaram o
boot de ponta a ponta; 1 travou exatamente no mesmo ponto que a V195
tinha encontrado originalmente**, recuperável só com desligar/ligar a
placa fisicamente. Uma melhora real sobre o "trava sempre" da V195, mas
não uma correção completa — sobra algum risco intermitente, sensível a
timing, especificamente quando o byte alto do divisor calculado da UART
não é zero.

**Decisão (confirmada pelo usuário): manter a correção, mas os defaults
compilado e salvo continuam em 115200** (`CONFIG_BAUDRATE` no
`factory_payload` defconfig, e `board/intelbras/sg1002_mr/sg1002_mr.env`)
— assim um boot de fábrica ou com ambiente recém-apagado nunca exercita
esse risco residual, porque a checagem já existente de "não está
realmente mudando" em `on_baudrate()` impede uma segunda chamada de
`setbrg()` quando o valor salvo já bate com o default compilado. Só
quem deliberadamente definir um `baudrate` diferente chega no risco de
~1 em 7, e deve esperar precisar de um power-cycle físico se aquele
boot específico cair nele.

Discutido explicitamente e descartado: portar isso também pro Stage-1
(preloader), pra ele respeitar a mesma variável. Tecnicamente possível
(o preloader já tem acesso à SPI), mas descartado porque mexer no
Stage-1 exige CH341A sem rede de segurança de autogravação, e não
resolveria o risco residual do Stage-2 — só adicionaria uma categoria
de risco mais cara em cima de uma correção que já não é 100%. Também
vale notar: como o Stage-1 nunca muda de taxa, um `baudrate` salvo
diferente de 115200 deixa o boot "misto" — o início (saída do Stage-1)
continua precisando de 115200 pra ler, só a partir do Stage-2 é que
muda.

**Verificado fisicamente**: 3 de 3 resets com o default seguro (115200)
completaram limpo e rápido, sem nunca exercitar o caminho de risco (como
esperado, já que a checagem de "não mudou" evita a segunda chamada).

**O kernel NÃO herda a taxa do U-Boot.** Depois da V210, o usuário viu
`earlycon: ns16550a0 at MMIO 0x18002000 (options '115200n8')` no log do
OpenWrt e perguntou se o U-Boot tinha "roubado" a configuração e mudado
pra 9600, ou se era o `conectar_ttl.sh` (do recovery-lab) que trocava de
taxa sozinho do lado do cliente. Testado dos dois jeitos, com
`baudrate=9600` salvo no ambiente:

- Ouvinte **fixo em 115200** (sem nenhuma adaptação) durante um
  `reboot -f` completo: o U-Boot fica **mudo** logo depois da escrita do
  divisor (banner, PHY, contagem de autoboot, "Booting kernel..." — tudo
  transmitido a 9600 e descartado pelo receptor descasado), e volta a ficar
  legível de repente nas mensagens do kernel (`printk: legacy console
  [ttyS0] enabled`), **sem nenhum byte de lixo no meio**. O mesmo padrão
  aparece nas 11 ocorrências do marcador `00000516` (divisor de 9600) no
  log histórico da serial.
- Shell do OpenWrt já no ar: ouvinte fixo em **9600 não recebe resposta
  nenhuma**; ouvinte fixo em **115200 recebe `root@OpenWrt:~#` na hora**.

Conclusão: a troca de taxa do U-Boot é real e vale até o fim da fase do
próprio U-Boot, mas o earlycon do kernel (`ns16550a`, `115200n8` vindo do
DT/cmdline) reprograma o divisor da UART sozinho, bem no início do boot,
seja qual for o valor que o U-Boot deixou. Portanto o `conectar_ttl.sh`
(`--boot-baud`/`--kernel-baud`) está **correto** — acompanha uma
transição física de verdade, não mascara nada. E um `baudrate` diferente
de 115200 **nunca** deixa o console do OpenWrt na velocidade errada: só a
fase do U-Boot precisa de um terminal casado (ou com troca automática).
Os comentários em `rtl8380_uart_setbrg()` e no script de build V210, que
diziam que o Linux herdaria a taxa, foram corrigidos.

*Resultado V210: `baudrate` agora controla de verdade a UART, com um
risco residual conhecido e documentado (~1 em 7) só ao definir um valor
diferente de 115200; o comportamento padrão de fábrica continua com
risco zero; o kernel reprograma a UART pra 115200 por conta própria, sem
herdar a taxa do U-Boot — registrado por Claude Sonnet 5 (IA).*

## V211 — `boot_auto`, `openwrt_addr` e `vxworks_addr` vêm só do env, sem fallback

Pedido do usuário depois de ver como o U-Boot decide entre
`bootm 0xb4150000` (OpenWrt) e `bootm 0xb4050000` (bootrom do vendor):
essas variáveis têm que estar no env e ser respeitadas, sem fallback,
"vai ser o que tá no env". Até a V210 os endereços estavam fixos em C
como valor padrão de `env_get_hex()`, então as variáveis não apareciam
no `printenv` e só quem lia o código sabia que dava para alterá-las.

Mudanças (`board/intelbras/sg1002_mr/`):

- `sg1002_mr.env` passa a listar `boot_auto=1`, `openwrt_addr=0xb4150000`
  e `vxworks_addr=0xb4050000`, com o significado de cada uma no
  cabeçalho. Entram no `default_environment[]` e no `env.bin`.
- `sg1002_mr.c` não tem mais nenhum endereço nem padrão: o que está no
  env é o que vale. Variável ausente, ou `boot_auto` ausente, gera uma
  mensagem no console e o `bootcmd` fica como estava. `boot_auto=0` (ou
  qualquer valor que não seja "sim") continua mantendo o `bootcmd`
  salvo.
- Os endereços são validados antes de lidos: o magic é lido com um
  `lw` de 32 bits, então um endereço desalinhado gera exceção de erro
  de endereço e um fora de KSEG0/KSEG1 pode gerar exceção de TLB. Isso
  roda em `board_late_init()`, antes da contagem do autoboot e do
  prompt; sem a validação, um erro de digitação salvo com `saveenv`
  travaria todos os boots seguintes sem como corrigir por software.
- A decisão agora é impressa, por exemplo `Boot: no uImage at
  openwrt_addr 0xb4150000 (found 46494c45), bootm vxworks_addr
  0xb4050000`.

**Defeito do script de build achado na verificação.** O primeiro
`env.bin` gerado não tinha as três variáveis, embora o `u-boot.bin`
tivesse: `printinitialenv` é uma ferramenta do host que embute o
`default_environment[]` na hora em que é compilada, e um build
incremental não a recompila quando só o `.env` muda, então ela
continuava despejando o ambiente antigo. `tools/build-sg1002-mr.sh` agora
apaga a ferramenta antes de despejar e falha se qualquer `CHAVE=` do
`.env` estiver ausente do despejo. Sem essa checagem eu teria gravado
um U-Boot sem valor de reserva junto com um env sem as variáveis.

Gravado via TFTP + `sf` em `0x008000-0x04ffff` (294.912 bytes), com a
RAM verificada por dois downloads independentes comparados com
`cmp.b` antes de apagar, e leitura de volta comparada depois. O Stage-1
não mudou (SHA-256 `beda62e8…`).

**Resultado físico** (cada caso com `setenv` + `saveenv` + `reset`):

| Caso | Resultado |
|---|---|
| Padrão, env vindo da flash | `Boot: no uImage at openwrt_addr 0xb4150000 (found 46494c45), bootm vxworks_addr 0xb4050000` |
| `vxworks_addr=0xb4150000` | mensagem e `bootcmd=bootm 0xb4150000` seguem o valor novo |
| `openwrt_addr` ausente | `Boot: openwrt_addr is not set in the environment`, `bootcmd` intacto |
| `openwrt_addr=0x1234` | rejeitado (fora de KSEG0/KSEG1), a placa chega ao prompt |
| `openwrt_addr=0xb4150001` | rejeitado (desalinhado), a placa chega ao prompt |
| `openwrt_addr=xyz` | rejeitado (não é hexadecimal), a placa chega ao prompt |
| `boot_auto=0` + `bootcmd=echo manual` | nenhuma linha `Boot:`, o `bootcmd` salvo é mantido |
| `boot_auto` ausente | `Boot: boot_auto is not set in the environment, bootcmd left as is` |

O caso `0x1234` é o que a validação existe para evitar: sem ela o
`lw` em `0x1234` levaria uma exceção dentro de `board_late_init()`. Ao
final, `env default -a` + `saveenv` restauraram o padrão compilado e um
último `reset` confirmou as três variáveis e a decisão padrão.

Compatibilidade: um env salvo antes da V211 não tem as três variáveis,
então a decisão automática é pulada até a partição ser reescrita com a
imagem `stage2+env` deste build (ou as variáveis serem definidas à mão).

### Experimento com o `Switch.bin` em `0x150000`, e uma correção

A pedido do usuário, o `Switch.bin` (4.387.886 bytes, SHA-256
`c0e8bf78…73b4`, idêntico ao pacote oficial `SG 1002 MR
L2+_2.2.0D_119622.bin`, contêiner `BDCO`, não uma uImage) foi gravado em
`0x150000` (`sf erase 0x150000 0x430000` + `sf write`, verificado byte
a byte), no lugar onde o OpenWrt estava. Como o magic não é
`27 05 19 56`, o `bootcmd` caiu para `bootm 0xb4050000` (bootrom do
vendor, checksum OK), imprimiu `Starting kernel ...` e não mostrou mais
nada a 115200 nem a 9600.

A V204 registrou esse ponto como um travamento da CPU. Os fatos de
depois indicam outra coisa: quando voltei ao U-Boot, `0x150000` não
tinha mais `BDCO` e sim `46 49 4c 45 00 e8 00 00 …` ("FILE"), o mesmo
formato que o dump original de fábrica (`sg1002_ch341a_read1.bin`) tem
nesse endereço, com contadores diferentes (`0x65`/`0x01` contra
`0x67`/`0x15`). Não foi escrita minha nem do usuário. A hipótese mais
bem sustentada é que o firmware do vendor está rodando, sem console
visível nas taxas testadas, e recriou o cabeçalho do seu sistema de
arquivos ao não achar um válido. Não vi a escrita acontecer.

Conferido depois: o MAC de fábrica em `0x141038` continua
`d8 36 5f 90 d7 26`, o bootrom em `0x050000` continua com checksum OK,
e amostras a partir de `0x250000` ainda parecem dados do `Switch.bin`.
Não mapeei até onde o vendor escreveu além do cabeçalho.

Consequência prática: enquanto não houver uImage válida em `0x150000`,
cada boot que cai no caminho do vendor pode modificar a flash a partir
de `0x150000`, a região onde o OpenWrt fica. E o "trava em `Starting
kernel ...`" da V204 provavelmente é, na verdade, um firmware vivo e
mudo.

### OpenWrt de volta em `0x150000`, e boot automático

Depois do experimento, a imagem do OpenWrt (`...sg1002-mr-
initramfs-kernel.bin`, 12.626.778 bytes, SHA-256 `74155f5e…158f`) foi
regravada em `0x150000` pelo fluxo da V204.1: dois downloads TFTP
independentes comparados com `cmp.b`, `sf erase 0x150000 0xc0b000`
(147 s), `sf write` (30 s) e leitura de volta com `cmp.b`, tudo
idêntico. O primeiro bloco voltou a ser `27 05 19 56`.

Em seguida um `reset` sem interromper o autoboot. O U-Boot V211 decidiu
sozinho, a partir das variáveis do env, e o boot foi até o shell sem
nenhuma intervenção:

```
Boot: uImage at openwrt_addr 0xb4150000, bootm 0xb4150000
Hit any key to stop autoboot:  3 ... 0
## Booting kernel from Legacy Image at b4150000 ...
   Image Name:   MIPS OpenWrt Linux-6.18.44
   Verifying Checksum ... OK
Starting kernel ...
[    0.000000] Linux version 6.18.44 ...
[    7.243148] Run /init as init process
Please press Enter to activate this console.
```

O kernel reconheceu a partição `factory` (`0x140000-0x150000`), usou o
MAC real `d8:36:5f:90:d7:26`, negociou link de 1 Gbps e o shell
respondeu (`uname -r` = `6.18.44`, 8 portas `lan`). O Stage-1, o
U-Boot e o kernel rodaram todos a 115200. Isso fecha o ciclo da V211: o
mesmo U-Boot que escolhe o vendor quando não há uImage em `0x150000`
(caso do `Switch.bin`) escolhe o OpenWrt assim que a uImage volta,
sem nenhum comando.

*Resultado V211: `boot_auto`, `openwrt_addr` e `vxworks_addr` agora
vivem no env compilado e são a única fonte da decisão de boot, sem
nenhum valor de reserva em C; variável ausente ou endereço inválido é
reportado sem travar o boot; defeito de env desatualizado no script de
build corrigido — registrado por Claude Sonnet 5 (IA).*

## V212 — OpenWrt: `fw_printenv` sem erro, `sysupgrade` de dentro do próprio OpenWrt e boot mais rápido

Três pedidos seguidos do usuário, todos do lado do OpenWrt (repositório
`openwrt`, branch `feature/intelbras-sg1002-mr`, commits
`1518473e38` e `07f015a60f`), resolvidos por declaração no DTS, na receita
da imagem e numa opção de kernel, sem mexer em ferramenta de build.

**1. `Cannot parse config file '/etc/fw_env.config'` / `Failed to find NVMEM
device`.** Duas causas. (a) O DTS só registrava a partição `factory`, e o
script `30_uboot-envtools` do target Realtek gera o `/etc/fw_env.config`
procurando uma MTD chamada `u-boot-env`. (b) Mesmo com o arquivo gerado, a
mensagem continua no boot: o `05_fw_defaults` (também do `uboot-envtools`)
chama `fw_loadenv` no preinit, antes de o `30_uboot-envtools` rodar, então
o arquivo ainda não existe; é a ordem padrão do OpenWrt e aparece em todo boot
de uma imagem initramfs (o rootfs é um tmpfs novo). A saída que a própria
mensagem aponta é o `fw_env` cair no dispositivo NVMEM: o núcleo do kernel
registra um provedor NVMEM para cada MTD (`mtd0`…`mtd5`), e o `fw_env` acha o
certo pelo `compatible = "u-boot,env"` do nó da partição. Isso precisou de
`partition@40000 "u-boot-env"` com esse `compatible` e de
`CONFIG_NVMEM_SYSFS=y` no rtl838x (o rtl930x já tinha): sem ele o arquivo
`/sys/bus/nvmem/devices/mtd0/nvmem` não existe e a mensagem virava `Cannot
open /sys/bus/nvmem/devices/mtd0/nvmem`. Um primeiro palpite meu, ligar
`CONFIG_NVMEM_U_BOOT_ENV`, era desnecessário (só criava um segundo provedor,
`u-boot-env0`) e foi retirado. Verificado: nenhuma das mensagens no log de
boot, `/etc/fw_env.config` = `/dev/mtd0 0x0 0x10000 0x10000`, `fw_printenv`
lê `baudrate`, `boot_auto`, `openwrt_addr` e `vxworks_addr` com `rc=0`, e um
`fw_setenv` de uma variável de teste foi lido de volta pelo **U-Boot** (CRC e
formato válidos, demais variáveis intactas); a variável foi removida depois.

**2. "A imagem tem que ter compressão no kernel."** O kernel já vai
comprimido. `vmlinux` cru: 10.786.400 bytes; kernel empacotado do
`sysupgrade`: 3,18 MB, em LZMA dentro do `rt-loader`. O "MIPS Linux Kernel
Image (uncompressed)" que o U-Boot imprime é o tipo do cabeçalho uImage
(`uImage none`), porque o conteúdo é o carregador que se descomprime sozinho.
O U-Boot só tem `CONFIG_GZIP` (sem LZMA) e é o `rt-loader` quem entra no
Linux com o estado da CPU certo (o mesmo ponto onde o caminho do vendor
falha), então não troquei por `uImage gzip/lzma`. A lentidão era outra: a
imagem gravada era a **initramfs**, com um kernel de 41,6 MB descompactado (o
rootfs vai dentro) e 12,6 MB para o U-Boot conferir a cada boot.

**3. `sysupgrade` de dentro do OpenWrt.** O build da imagem `sysupgrade`
falhava por 128 KiB (14.417.924 > 13952k) e o sistema não tinha partição de
firmware. Mudanças: `partition@150000 "firmware"` (`0x150000`, `0xe80000`,
`denx,uimage`, termina em `0xfcffff`, nada abaixo de `0x150000` é
alcançável), `IMAGE_SIZE := 14848k` e `DEVICE_COMPAT_VERSION := 2.0`. O
`sysupgrade -T` pegou um defeito real antes de qualquer gravação: o
`05_compat-version` já declara este dispositivo como 2.0 no sistema em
execução, mas a receita não herda `Device/zyxel_gs1900` (de onde os outros
dispositivos 2.0 tiram isso), então a imagem saía 1.0 e era recusada
(`image version (2.0->1.0)`).

Camadas da imagem final: `firmware` (`0xe80000`) = `kernel` (`0x310000`) +
`rootfs` squashfs (`0xb70000`) + `rootfs_data` (`0xd0000`); no `/proc/mtd`:
`mtd0 u-boot-env`, `mtd1 factory`, `mtd2 firmware`, `mtd3 kernel`, `mtd4
rootfs`, `mtd5 rootfs_data`. O rootfs tem 11,1 MB, então o overlay jffs2 fica
com apenas 832 KiB (584 KiB livres depois do primeiro boot); mais espaço só
tirando pacotes da configuração.

**Resultado físico.** A primeira gravação da imagem `sysupgrade` foi por
TFTP + `sf` (dois downloads comparados, `sf erase 0x150000 0xdc1000`,
`sf write`, leitura de volta com `cmp.b`). Depois disso, a imagem nova foi
instalada **pelo próprio OpenWrt**: imagem baixada por HTTP (`wget`, SHA-256
conferido), `sysupgrade -T` com `rc=0` e `sysupgrade -n -v`, que fechou os
processos, escreveu a partição `firmware` (cerca de 90 s), reiniciou, e o
U-Boot escolheu o OpenWrt sozinho (`Boot: uImage at openwrt_addr 0xb4150000`).
Tempos do reset ao console: initramfs 88 s; `sysupgrade` 51 a 53 s (banner do
U-Boot em 7,8 s, checagem do kernel de 3 MB em 4,6 s, descompressão do
`rt-loader` em 4,7 s, kernel em 25 s). No primeiro boot depois do
`sysupgrade -n` o `mount_root` usa um overlay em tmpfs por uns 2 minutos
enquanto o jffs2 da `rootfs_data` é formatado; depois `/dev/mtdblock5` fica
montado em `/overlay`.

**Ambiente de build (para a próxima vez).** A árvore `openwrt/` é montada
para o contêiner de `openwrt-build-tools` (`./start.sh docker`), e o host não
tem `gawk`: compilar direto no host falha e mistura ferramentas de outro
caminho no mesmo `build_dir`. Nesta sessão a imagem do builder
(`openwrt-openwrt-25.12-builder`, Ubuntu 22.04) tinha sumido e foi
reconstruída com o `Dockerfile` do projeto, e o `make` rodou com `docker run`
usando os mesmos volumes do `start.sh`. Dois cuidados: o link `dl` aponta para
`/home/developer/dl_cache`, caminho do contêiner, então o build precisa dele;
e o DNS deste host demora 5 s por consulta (corrida entre A e AAAA), mais que
o `--timeout=5` do `wget` do OpenWrt, então o contêiner precisa de
`-e RES_OPTIONS=single-request-reopen`. O `.config` do checkout estava em
`qualcommax/ipq50xx`; para o Realtek foi usada a semente
`rtl838x_intelbras.config` (copiada para `.config` e seguida de `make
defconfig`), e o `.config` original foi restaurado depois, idêntico.

*Resultado V212: `fw_printenv`/`fw_loadenv` funcionam e o boot não tem mais as
duas mensagens; a imagem `sysupgrade` do dispositivo (kernel LZMA de 3 MB no
`rt-loader` + rootfs squashfs) passa a ser gerada, e é gravada a partir do
próprio OpenWrt com `sysupgrade`, com boot de 51 a 53 s em vez de 88 s;
defeito de `compat_version` da receita corrigido — registrado por Claude
Sonnet 5 (IA).*

## V213 — kernel LZMA que o próprio U-Boot descomprime, e `openwrt_addr` no alias cacheado

Depois da V212 o usuário insistiu que faltava "a compressão no kernel": o
U-Boot imprimia `MIPS Linux Kernel Image (uncompressed)`, mesmo com o kernel já
empacotado em LZMA dentro do `rt-loader` (o tipo `none` do cabeçalho é do
carregador). Foi feito o que ele pediu, medindo cada passo.

**gzip nativo (`uImage gzip`).** Testado primeiro: o kernel-só (uImage
`comp=1`, 4.347.391 bytes) foi levado à RAM por TFTP e `bootm`. O U-Boot
mostrou `gzip compressed`, descomprimiu e o Linux 6.18.44 subiu normal (SoC,
MTD, PHYs) em 0,8 s depois do `bootm`; o pânico de `VFS` no fim é o esperado a
partir da RAM, sem rootfs. Mas o kernel gzip é 1,17 MB maior e a imagem
`sysupgrade` chega a 15.728.644 bytes, 512 KiB acima dos 15.204.352 da
partição `firmware`, então ela não é gerada sem tirar pacotes. Ficou provado
que o caminho nativo de entrada no Linux funciona sem o `rt-loader`.

**LZMA nativo.** `CONFIG_LZMA=y` (com `CONFIG_LZMA_SIZE_OPTIMIZATION=y`) no
`configs/intelbras_sg1002_mr_factory_payload_defconfig`: `u-boot.bin` cresce
7.280 bytes, para 218.106, e sobram 11.270 bytes na janela de `0x38000` do
Stage-2. Gravado só o Stage-2 (`0x008000-0x03ffff`, dois downloads comparados,
leitura de volta com `cmp.b`) e o U-Boot novo subiu normal. Lado OpenWrt: o
kernel do `sysupgrade` passa a `lzma | uImage lzma` (3.123.506 bytes, menor
que os 3.180.093 do `rt-loader`); a initramfs continua no `rt-loader`, porque
descompactada tem 41 MB e o `CONFIG_SYS_BOOTM_LEN` é 32 MiB. Da RAM, o
`bootm` de um kernel LZMA leva 2,3 s até o Linux. O U-Boot passa a mostrar
`MIPS Linux Kernel Image (lzma compressed)`, `Data Size: 3123442 Bytes = 3 MiB`.

**O achado que quase estragou o ganho: a janela da flash.** Instalada pelo
`sysupgrade` e bootada da flash em `0xb4150000`, a imagem LZMA levou 23,8 s do
`bootm` ao `Starting kernel` (65 s até o console, pior que os 53 s do
`rt-loader`). A causa: o `bootm` descomprime lendo o kernel direto do
mapeamento da flash, e a janela `0xb4…` é KSEG1, não cacheada, então cada byte
é uma leitura SPI. O mesmo endereço físico pelo alias cacheado KSEG0
(`0x94150000`, o conteúdo é idêntico, conferido com `md.b`) levou 7,6 s até
`Starting kernel`, com a checagem do kernel caindo de 4,6 s para 2,8 s.
`openwrt_addr` no `board/intelbras/sg1002_mr/sg1002_mr.env` passou de
`0xb4150000` para `0x94150000`; `vxworks_addr` continua em KSEG1 (caminho do
bootrom do vendor). Cuidado: como o alias é cacheado, depois de um `sf write`
sobre `0x150000` convém dar `reset` antes de bootar (a primeira linha do
cabeçalho antigo pode ficar em cache). Gravado o `stage2+env` inteiro
(`0x008000-0x04ffff`), com `Boot: uImage at openwrt_addr 0x94150000`.

**Resultado físico** (reset até o console, sem apertar tecla): initramfs 88 s;
`rt-loader` 53 s; LZMA nativo pela janela KSEG1 65 s; **LZMA nativo pelo alias
KSEG0 47 s** (banner do U-Boot em 7,8 s, `bootm` em 13,5 s, checagem em 2,7 s,
kernel em 21,0 s, console em 47,0 s), sem nenhuma mensagem de `fw_env`.

**Dois cabos no switch.** Durante isso o TFTP do U-Boot falhou (`T T T`,
`ping` sem resposta e o host sem receber um pacote) quando havia dois cabos
ligados; com um só voltou ao normal. Causa, lida em `drivers/net/
rtl838x_eth.c`: a espera de link consulta os PHYs a cada 100 ms, até 5 s, e
**para na primeira volta em que qualquer porta tem link** (`if (phy_links)
break;`), e depois só transmite para as portas com link (`tx_port_mask =
phy_links`). Com dois cabos, o que negocia mais rápido (a `lan7`, cerca de 2 s)
vence e a `lan1` do host (cerca de 3,5 s) ainda nem tem link, então todo
quadro sai para a porta errada (o log mostra `PHY links 00004000` em vez de
`00000100`). Contorno: um cabo só. Correção possível, ainda não feita: esperar
mais um pouco depois do primeiro link e juntar as máscaras, ou transmitir para
todas as portas de cobre.

*Resultado V213: o kernel do `sysupgrade` é um uImage LZMA que o U-Boot
descomprime e reconhece como comprimido, cabe na partição com o mesmo overlay,
e o boot da flash chega ao console em 47 s (era 53 s com o `rt-loader` e 88 s
com a initramfs); descoberto e documentado que a janela KSEG1 da flash
triplica o tempo de descompressão, e a causa das falhas de rede com dois
cabos — registrado por Claude Sonnet 5 (IA).*

## V214 — RTL8231: mensagens do LED corrigidas, e o que se sabe do segundo chip

O Codex pediu para corrigir "completamente os dois RTL8231" (a placa tem
fisicamente dois, segundo a inspeção visual registrada no DTS) e as quatro
mensagens do boot:

```
rtl8231-leds: Failed to locate of_node [id: -2]
rtl8231-leds rtl8231-leds.1.auto: scan mode missing or invalid
rtl8231-leds rtl8231-leds.1.auto: probe with driver rtl8231-leds failed with error -22
rtl8231-expander realtek-aux-mdio:01: RTL8231 not present or ready 0x3f != 0x37
```

O usuário acrescentou que os LEDs funcionam, mas às vezes "bugam do nada".

**As três primeiras mensagens: causa e correção.** O MFD do RTL8231 (patch
802 do OpenWrt) registra sempre duas células, `rtl8231-pinctrl` e
`rtl8231-leds` (esta com `of_compatible = "realtek,rtl8231-leds"`). O nó do
expansor não tinha nenhum filho com esse `compatible`, então o `mfd-core`
avisa que não achou o `of_node` (o `-2` é `PLATFORM_DEVID_AUTO`, não um
`-ENOENT`) e cria o dispositivo sem ele, e o driver de LED (patch 804), sem a
propriedade `realtek,led-scan-mode`, falha com `-EINVAL`. Um nó filho
`status = "disabled"` é pulado sem erro. Ele só é a descrição certa se o LED
scan do chip realmente não for usado, e isto foi medido: o estado do chip no
endereço 0, lido depois da inicialização do Linux, é `PIN_MODE0/1 = 0xffff` e
`PIN_HI_CFG` com modo `0x1f` (os 37 pinos em modo GPIO, nenhum em modo LED) e
`GPIO_DIR1 = 0xf7ff` (o único pino de saída é o 27, o LED SYS); `FUNC0 =
0x0003` (`LED_START` ligado), `FUNC1 = 0x0371` (código de pronto `0x37`).
`led-controller { compatible = "realtek,rtl8231-leds"; status = "disabled"; }`
é o que o `dgs-1210` e o segundo expansor da GS1900-48 já fazem no tree.
Verificado na placa: depois de um `sysupgrade`, as três mensagens somem, o chip
0 continua registrando `gpiochip2 [realtek-aux-mdio:00] (37 lines)` e o LED
`sys` existe. Commit `0c74c57757` no repositório `openwrt`.

**A quarta mensagem: não é problema de detecção.** O barramento auxiliar é um
só (`realtek-aux-mdio`, controlador `EXT_GPIO_INDIRECT_ACCESS` em `0xA09C`, com
endereço de 5 bits), e o driver dele não detecta falha de leitura
(`RCMD_FAIL = 0`). Uma varredura só de leitura dos 32 endereços, com o módulo
`tools/linux-rtl8231-aux-scan/` (lê os registradores 0 e 1 de cada endereço e
despeja os 32 do que responder), deu: **só o endereço 0 responde**; os
endereços 1 a 31 leem `0xffff` nos dois registradores, que na máscara
`GENMASK(9, 4)` do driver dá exatamente o `0x3f` do log. Ou seja, no endereço 1
deste barramento não há nada dirigindo o MDIO agora; não é endereço errado
(nenhum outro responde), nem ordem de detecção.

**O que o firmware original diz.** No payload do firmware original (base
`0x80100000`, strings) o driver de GPIO do vendor, `GPIO_RTL8231_DRV`, trata
**um único RTL8231**: `rtl8231_gpio_read ... index:0~36` (37 pinos, sem faixa
para um segundo chip). Há outro expansor, por I2C: `XRA1201 chip info
i2c_mux_index:%d,i2c_addr:%x` e uma tabela de chips (`id:%d,chip:%s,mux_index:
%d,addr:%x`), e os comandos `cpld_dump` (que lê GPIOs do RTL8231) e
`cpld_version`. Não achei no firmware nada que inicialize um segundo RTL8231.

**Em aberto.** O segundo chip não responde em nenhum endereço deste barramento.
Hipóteses, nenhuma confirmada: (a) ele está em reset (o `reset-gpios` da
GS1900-48, por exemplo, liga o reset de um expansor a um GPIO do outro); (b)
está em outros pinos MDC/MDIO ou em modo SMI (I2C-like), fora deste
controlador; (c) não é um RTL8231 (o firmware original usa um XRA1201 por I2C);
(d) está sem alimentação ou não montado. Para decidir preciso da marcação
impressa nesse segundo chip e, se possível, de uma medição (nível do pino
RESET, ou continuidade dos pinos MDC/MDIO até o RTL8380M). Nenhuma escrita
cega em GPIOs do chip 0 foi feita para "procurar" o reset, porque cada pino
pode estar ligado a um reset de PHY ou do SoC. O nó `expander@1` do DTS foi
mantido como está (a mensagem continua) até isso ser esclarecido.

**Sobre os LEDs que "bugam".** As três mensagens corrigidas eram do driver de
LED do chip 0, que falhava antes de tocar em qualquer registrador, então
tirá-las do boot não muda o que o hardware faz. O chip 0 tem `LED_START` ligado
mas nenhum pino em modo LED, então ele não gera matriz de LED. Por isso o
defeito intermitente provavelmente tem outra origem (a disputa entre o motor de
LED do RTL8380 e o script `sg1002-port-leds`, ou o segundo chip em estado
padrão se ele existir e for dono de linhas de LED), e merece uma investigação
separada.

*Resultado V214: mensagens `Failed to locate of_node`, `scan mode missing or
invalid` e `probe … failed with error -22` corrigidas por uma descrição medida
do hardware; a mensagem do segundo RTL8231 explicada (nada responde nos
endereços 1 a 31) e o que falta para resolvê-la documentado — registrado por
Claude Sonnet 5 (IA).*

## V215 — LEDs que "bugam" e o LED SYS que apaga: a causa era o `mesh11sd`

O usuário relatou um defeito intermitente do painel: com o sistema normal, de
repente **todos os LEDs laranja acendem e depois voltam ao normal**; o **LED SYS
apaga do nada e nunca mais acende**; em outra ocasião os verdes predominaram; e
os LEDs de porta, quando acendem, aparecem com brilho muito baixo (cerca de 1%).

**Hipótese inicial (refutada): o RTL8231 #0 resetando sozinho.** O LED SYS é o
GPIO 27 desse chip e o DTS avisa que os pinos 0-19 dele dividem os caminhos dos
LEDs do painel. Um reset espontâneo explicaria os dois sintomas (saídas de LED
no estado padrão, pino 27 voltando a entrada e o driver nunca reconfigurando).
Um módulo só de leitura, `tools/linux-led-diagnostics/sg1002_ledwatch.c`,
passou a vigiar os registradores do motor de LED do RTL8380 (a cada ~15 ms) e os
de configuração do RTL8231 #0 (a cada ~250 ms). Resultado nos eventos
capturados: **nenhum registrador do RTL8231 mudou** (`FUNC0`, `MODE0/1`, `DIR0/1`
ficaram iguais, o pino 27 continuou saída); o que mudou foram os **registradores
do motor de LED**, com todos os canais laranja e verde das dez portas indo para
"aceso" (`amber=AAAAAAAAAA green=GGGGGGGGGG`) e sendo apagados porta a porta, em
sequência, em cerca de um segundo. Mudança de link (cabos ligados e desligados)
não gerou nenhum evento.

**O ritmo.** Os eventos vinham a cada 65-75 s, e cada um caía no mesmo segundo
de `hostapd: Set MLD config` / `wpa_supplicant: Set MLD config`, quatro
segundos depois de `mesh11sd is in startup` (PID novo a cada vez). O
`mesh11sd` estava em laço de respawn (`auto_config is disabled`); às 22:29:54 o
`procd` desistiu (`Instance mesh11sd::instance1 is in a crash loop 11 crashes`)
e os eventos pararam por mais de dois minutos, voltando ao subir o serviço na
mão.

**O mecanismo, provado.** Três observações de espaço de usuário:
1. Um amostrador de três LEDs de prova (`amber:lan-2`, `green:lan-2`, `sys`)
   viu `sys → trigger default-on`, ~1 s depois todos em `1,default-on` e ~1,2 s
   depois todos em `0,none`. Os eventos `change` do hotplug chegam em ordem
   alfabética (`amber:lan-1, amber:lan-10, amber:lan-2 … green:lan-9, sys`), a
   ordem de `ls /sys/class/leds`.
2. Um espião de processos (`procspy.c`) mostrou que, logo antes do evento, o
   filho do `mesh11sd` roda `service network reload` (→ `/etc/init.d/network
   reload` → `ubus call network reload`).
3. O kernel tem `CONFIG_FANOTIFY` mas não tem kprobes nem ftrace, então
   `ledwho.c` marca o `/sys` com fanotify e registra quem escreve nos
   `.../leds/.../trigger`: **todas as escritas, uma por LED, em ordem, são do
   PID de `/bin/sh /usr/sbin/mesh11sd daemon`** (pai: `procd`).
No código do `mesh11sd` 6.2.1 (linhas 4953-4965): `leds=$(ls /sys/class/leds)`,
depois `echo default-on > .../$led/trigger` para todos e em seguida `echo none >
.../$led/trigger` para todos (rotina de LED de "backhaul" do mesh).

**Por que explica os dois sintomas.** Toda partida do `mesh11sd` acende todos os
LEDs (`default-on`) e depois os deixa em `trigger none`, o que o script de
política `sg1002-port-leds` corrige porta por porta no ciclo seguinte (a volta
"ao normal"); o `sys`, que não está nesse script, fica em `none`, brilho 0, para
sempre. A aparente aleatoriedade vinha do laço de respawn e do `procd`
desistindo depois de N quedas.

**A placa não tem Wi-Fi.** Por isso a solução é tirar o stack de Wi-Fi da
imagem, como o usuário pediu. Na semente `rtl838x_intelbras.config` (arquivo não
versionado na raiz do `openwrt`, backup da versão anterior fora do repositório)
foram desligados: `mesh11sd`, `wpad-mesh-mbedtls`, `hostapd-common`, `usteer`,
`wifi-scripts`, `kmod-mac80211`, `kmod-cfg80211`, `iw`, `ucode-mod-nl80211`,
`wireless-regdb` e `ath11k-firmware-ipq5018-qcn6122` (firmware de rádio
Qualcomm, sobra do perfil de outro roteador). Ficaram `libiwinfo`,
`libiwinfo-data` e `rpcd-mod-iwinfo`, que são só bibliotecas exigidas pelo
`luci-mod-status`, e `kmod-batman-adv`, `luci-proto-batman-adv` e `umdns`, que
não são Wi-Fi. Correção posterior: o `kmod-batman-adv` também saiu, junto com
`luci-proto-batman-adv` e `batctl-default`, porque ele depende de
`kmod-cfg80211`, que seleciona `wifi-scripts`, `wireless-regdb` e `iw` (e estes,
`ucode-mod-nl80211`), trazendo o stack de Wi-Fi de volta por dependência;
`umdns` ficou. `ethtool`, padrão neste perfil, é o único padrão que a semente
desliga. A semente resulta em 196 pacotes selecionados, 121 fora do padrão do
dispositivo.

**Build e gravação.** Build no contêiner e instalação pelo `sysupgrade` de dentro
do OpenWrt (`sysupgrade -T`, depois `-n`). A imagem caiu de 14.418.822 para
**9.438.086 bytes**, o manifesto tem 190 pacotes e nenhum de Wi-Fi ou mesh, e o
overlay passou de 832 KiB para **5,5 MiB** (`rootfs_data` em `0x900000-0xe80000`
dentro da partição `firmware`). No primeiro boot: nenhum processo de Wi-Fi/mesh,
zero mensagens do driver de LED do RTL8231, do `fw_env` ou do NVMEM (sobra só a do
segundo RTL8231), LED SYS aceso fixo, `fw_printenv` ok. Amostrando três LEDs de
prova durante 4 minutos (mais de três ciclos do que era o defeito), nenhuma
mudança; o usuário confirmou que os LEDs seguem normais.

**Armadilha do build.** O primeiro `make` falhou em `package/install` com `apk`
dizendo que o pacote `kernel` (`~11c134a5…`) "quebra" `kmod-gpio-button-hotplug`,
`kmod-mtd-rw` e `kmod-ovpn-backports`: esses três módulos de fora da árvore do
kernel estavam compilados contra um hash de kernel anterior (`~096fedb1…`; o hash
muda quando o `.config` do kernel muda, como ao ligar `CONFIG_NVMEM_SYSFS`) e o
`make` não os refez. Solução: `make package/kernel/gpio-button-hotplug/clean
package/feeds/packages/mtd-rw/clean package/feeds/packages/ovpn-dco/clean`
(o `mtd-rw` mora em `feeds/packages/kernel/`, então o alvo é
`package/feeds/packages/mtd-rw`; com o caminho errado o `clean` falha calado) e
recompilar; `apk adbdump` no `.apk` mostra o `kernel=` que cada um exige.

**Observações de brilho.** Com o script parado, o canal laranja da `lan5` no modo
`5` (o que o driver `850-gpio-rtl8380-port-led-test` usa como "aceso fixo") aparece
fraco, na ordem de 1% segundo o usuário; no modo `7` ele "acende e apaga até 1%".
O usuário disse que esse brilho baixo não é problema, então não foi
investigado além disso (os modos `1` a `4` e `6` não foram testados).
`LED_MODE_SEL` foi lido como `0x0007c4a6` em uma sessão e `0x00000000` em outra;
não está explicado e não parece ligado ao defeito.

**O que ficou aberto.** O segundo RTL8231 continua sem resposta em nenhum
endereço do barramento auxiliar (V214). A pergunta de se o LED SYS deveria ficar
em heartbeat (hoje o DTS o deixa aceso fixo depois do boot) aguarda o usuário.

*Resultado V215: causa dos LEDs que acendem sozinhos e do LED SYS que apaga
identificada e provada (o `mesh11sd`, que escreve `default-on` e depois `none`
em todos os LEDs a cada partida e reinicia em laço), sem relação com o RTL8231
nem com o hardware; mitigada na placa e resolvida no próximo build pela remoção
do stack de Wi-Fi da imagem — registrado por Claude Sonnet 5 (IA).*

## V216 — o segundo RTL8231 é o expansor dos cages SFP

O V214 deixou em aberto o que é o segundo RTL8231 (o endereço 1 do barramento
auxiliar, que nunca respondeu). Registrado por Claude Sonnet 5 (IA), a pedido
do usuário.

**A pista.** O usuário observou na placa que as portas SFP têm várias trilhas
que vão para esse RTL8231, e concluiu: "deve ser do SFP então". Ele não tem um
módulo SFP disponível agora, então nada disto foi testado com um módulo, e a
decisão foi apenas documentar a atribuição.

**O que está estabelecido, e o que não está.**

- *Observado pelo usuário (visual, na placa):* várias trilhas dos dois cages SFP
  chegam ao segundo RTL8231. Isso é coerente com o que um cage SFP precisa de um
  expansor de GPIO (`TX_DISABLE`, `RX_LOS`, `MOD_DEF0`/`MOD_ABS`, `TX_FAULT`,
  às vezes `RS0`/`RS1`); o I2C do módulo (`MOD_DEF1`/`MOD_DEF2`) não passa pelo
  RTL8231.
- *Não verificado:* qual pino do chip é qual sinal, se todos os sinais do SFP
  estão nele, e se o chip 0 também leva algum sinal do SFP. Sem módulo não há
  como ver um sinal mudar.
- *Continua sem resposta:* por que o chip não responde. O `0x3f != 0x37` do
  boot é real e continua; `reg = <1>` no DTS é só um marcador de posição para um
  endereço por strap que não se conhece (pinos 37 a 41 do RTL8231).

**Leitura só de leitura feita na placa (2026-09-23), para o chip 0.** No kernel
só existe o `gpiochip2` (`realtek-aux-mdio:00`, 37 linhas); o chip 1 nunca criou
um `gpiochip`. Com `gpioinfo -c gpiochip2` e `gpioget -c gpiochip2 --numeric`
(nada foi escrito): todas as linhas são entrada, exceto a 27 (LED SYS, saída
ativa-baixa); níveis altos nas linhas 10 a 14, 20 a 25, 31 e 34, o resto baixo.
Isso ainda não foi correlacionado com nenhum sinal (seria preciso inserir e
tirar um módulo SFP e comparar as 36 entradas antes e depois: a linha que
mudar é `MOD_DEF0` ou `LOS`).

**Pinos, dos datasheets públicos.** Não existe esquema elétrico público da
SG 1002 MR (só manual e datasheet da Intelbras); os datasheets dos dois chips
existem: RTL8380M-VB (LQFP216,
`wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2208301730_Realtek-Semicon-RTL8380M-VB-CG_C2764165.pdf`)
e RTL8231 (LQFP-48, `file.elecfans.com/web2/M00/86/A6/poYBAGOqTAiALTRuABkZY_QfPI8169.pdf`).

RTL8231, para medir no chip mudo:

| Pino | Função | O que medir |
|---|---|---|
| 36 | `RESET` (ativo-baixo) | 0 V = preso em reset |
| 15 | `LED[0]`/`Dis_SMI` | baixo = modo SMI, alto = modo shift-register |
| 42 | `GPIO20`/`MOD[0]` | alto = interface MDC/MDIO |
| 17 / 18 | `MDIO`/`SDA` / `MDC`/`SCK` | continuidade até o RTL8380M; mesma rede do chip 0? |
| 37 a 41 | `GPIO15..19`/`Addr[0..4]` | endereço de PHY por strap |
| 2 | `RC_Ref` | oscilador RC (249 ohm em paralelo com 1 nF) |

RTL8380M-VB: **correção (V217)** — o barramento auxiliar do RTL8231 não usa os
pinos `MDC` 120 / `MDIO` 121 (esse é o SMI principal, dos PHYs). O devicetree do
Linux liga os **GPIO2 e GPIO3 do SoC como MDC/MDIO** (`mdio_aux_mdx`), que no
datasheet são os pinos **111 (GPIO2) e 110 (GPIO3)**; a continuidade a medir a
partir dos pinos 17/18 do RTL8231 é até esses dois pinos, e a mesma rede do
chip 0 indica o mesmo barramento. Os outros pinos citados: `LED_CK` 122,
`LED_DA` 123, SSPI/I2C 118/119, `RESET#` 114, `GPIO0` 113 (LED do sistema por
padrão).

**Hipóteses para o chip não responder (nenhuma confirmada).** (a) preso em reset
por um GPIO do SoC que ninguém solta; (b) strap em modo SMI-slave (pinos
`SCK`/`SDA`, que o SoC só falaria no modo I2C do controlador auxiliar, com
`aux_mode` = 0, e não no MDIO) ou em modo shift-register
(alimentado por `LED_CK`/`LED_DA`, 122/123), em que o barramento MDC/MDIO nunca
o vê; (c) endereço por strap fora do que foi varrido (foram os 32, 0 a 31);
(d) sem o RC de clock. A medição do pino 36 (reset) é a mais rápida.

**O que foi mudado.** Só comentário no DTS do OpenWrt (commit `17ccf108ff` no
repositório `openwrt`): o cabeçalho, o nó `expander@1` e o comentário das portas
SFP passam a dizer que o RTL8231 do endereço 1 é o expansor dos cages SFP. O
nó e a mensagem do boot foram mantidos de propósito (o chip é real e a falha é
um achado real); o `.dtb` compilado não muda, então não há build nem gravação
novos. Os nós `sff,sfp` (`tx-disable-gpios`, `los-gpios`, `mod-def0-gpios`)
continuam por escrever, porque dependem de saber qual pino é qual; hoje as
portas 24 e 26 seguem só com `managed = "in-band-status"`, sem detecção de
módulo, LOS nem controle de TX.

*Resultado V216: o segundo RTL8231 documentado como o expansor dos cages SFP
(atribuição por observação das trilhas, sem teste com módulo SFP), com o que
falta medir para fazê-lo funcionar — registrado por Claude Sonnet 5 (IA).*

## V217 — teste de LEDs no início do U-Boot (RTL8231 e LEDs portados)

Pedido do usuário: portar para o U-Boot o RTL responsável pelos LEDs e os LEDs,
e, ao iniciar, executar um teste: acender todos os laranjas, depois todos os
verdes, depois apagar todos. Registrado por Claude Sonnet 5 (IA).

**O que "o RTL dos LEDs" é de verdade.** Os 20 LEDs de porta (10 laranjas e 10
verdes) **não** estão em nenhum RTL8231: quem os aciona é o motor de LED do
próprio RTL8380 (dois canais por porta). O RTL8231 do endereço 0 só leva o **LED
SYS** (GPIO27, ativo em nível baixo); o do endereço 1 é o expansor do SFP
(V216). Por isso o porte tem duas partes: o motor de LED do SoC (as 20 portas de
LED) e o acesso mínimo ao RTL8231 (o SYS).

**Onde está o código.** Em `arch/mips/mach-rtl-otto/`: `rtl838x_led.c`
(controle por software do motor de LED, a partir do driver do Linux
`gpio-rtl8380-portled`), `rtl8231.c` (MDIO auxiliar e RTL8231, a partir dos
drivers `mdio-realtek-otto-aux`, `rtl8231` MFD e pinctrl do Linux) e os
cabeçalhos em `include/mach/`, ligados por `RTL838X_PORT_LED` e
`RTL8231_EXPANDER` (o alvo da placa os seleciona). O teste e a fiação medida
(portas de LED 16..24 e 26 para lan1..lan10, SYS = RTL8231 #0 pino 27) estão em
`board/intelbras/sg1002_mr/sg1002_mr.c` e rodam no começo de `board_late_init()`,
depois de parar o watchdog e antes do `eth_init()`.

**Por que sem driver-model (GPIO/LED uclass).** A janela do Stage-2 tem
`0x38000` bytes e o `u-boot.bin` do V213 tinha 218.106, ou seja, 11.270 de
folga; a pilha DM de GPIO + LED + `gpio-leds` custaria mais do que isso. A
versão direta custou 1.816 bytes: `u-boot.bin` de 219.922, sobram 9.454. É
código de registrador, sem código do SDK do vendor.

**Variável de ambiente, como o `boot_auto`.** `led_test=1` está em
`sg1002_mr.env`: 1 executa o teste, 0 pula sem mensagem, e variável ausente é
avisada no console e o teste é pulado. O SPI gravado antes do V217 não tem a
variável, então só a imagem `stage2-plus-env` (que reescreve o ambiente) a traz.
Cada fase dura 700 ms (`SG1002_LED_STEP_MS`). O SYS acende junto com o início do
teste e apaga no fim (leitura minha de "apaga todos": ele conta como um dos LEDs).

**Achado: o barramento auxiliar precisa de dois bits de pinmux.** Lendo o
`EXT_GPIO_INDIRECT_ACCESS` (`0xbb00a09c`) pelo prompt do U-Boot, o comando de
leitura nunca terminava (`EXEC` ficava em 1). No Linux isso é resolvido pelo
pinctrl do devicetree, e o U-Boot ainda não fazia: `0xbb000144` bit 0 (aux em
modo MDIO, não I2C) e `0xbb00a0e0` bit 0 (GPIO2/GPIO3 como MDC/MDIO). Com os
dois em 1, a leitura do endereço 0 devolve `FUNC1 = 0x0371` (código de pronto
`0x37`), igual ao que o Linux vê. O `rtl8231_init()` liga os dois.

**Estado de entrada no U-Boot (lido antes de qualquer escrita).** Motor de LED
em modo automático: `LED_GLB_CTRL = 0x2f39ea3f`, `0xa004 = 0x00838107`,
`LED_P_EN_CTRL = 0x05ffffff`, `LED_SW_CTRL`, `LED0/1_SW_P_EN_CTRL` e todos os
`LED_SW_P_CTRL` em 0, `LED_MODE_SEL = 0`. Ou seja, a máscara que o devicetree do
Linux chama de "medida do estado estável do vendor" é o valor de reset do
motor. `LED_SW_CTRL` só guarda 4 bits (`0xf`) mesmo com `0x0fffffff` escrito,
como no Linux; não investiguei.

**Verificação no hardware (2026-09-23).**

- `tools/build-sg1002-mr.sh build-gpl/sg1002-mr-v217`: Stage-1 idêntico byte a
  byte (`beda62e8…`), sem warnings novos, `env.bin` com `led_test=1`.
- Gravado por TFTP + `sf` em `0x8000..0x4ffff` (dois downloads iguais de
  294.912 bytes, `sf erase`, `sf write`, `sf read` e `cmp.b` idêntico). O
  emulador QEMU não pôde ser usado: a imagem Docker dele está corrompida no
  armazenamento local (blob ausente); de todo modo ele não modela os LEDs.
- Console: `LED test: SYS, amber, green, off` e nenhuma mensagem de erro do SYS.
- Depois do teste, lidos os registradores: motor em modo software (`LED_GLB_CTRL
  = 0x2f39ea1b`), todos os `LED_SW_P_CTRL` em 0, SYS apagado (`DATA1` bit 11 =
  1), pinmux ligados.
- Partida a frio do RTL8231: com `LED_START` zerado à mão (`FUNC0 = 0x0001`) e
  `reset`, o U-Boot fez o soft reset e voltou ao mesmo estado do Linux
  (`FUNC0 = 0x0003`, `FUNC1 = 0x0371`, `PIN_MODE1 = 0xffff`, `DIR1 = 0xf7ff`).
  Não é uma ciclagem real de energia, só o que o U-Boot decide a partir de
  `LED_START`.
- OpenWrt sobe normalmente a partir do U-Boot novo (33 s do `bootm` ao shell,
  só as mensagens já conhecidas).

**Confirmação visual.** O usuário viu o teste na placa e disse que os LEDs
estão perfeitos (2026-09-23): a ordem SYS, laranjas, verdes, tudo apagado e o
brilho do modo `5` estão como esperado, então `RTL838X_LED_SW_ON` fica em `5`.

**Não verificado.** A ciclagem real de energia do RTL8231 (só foi simulada
zerando o `LED_START`) e a via VxWorks: o motor de LED fica em modo software,
tudo apagado, que é o estado que o Linux também deixa; o firmware do vendor
programa o motor sozinho, mas não foi exercitado com este U-Boot.

*Resultado V217: o U-Boot passa a fazer um teste de lâmpadas (SYS, laranjas,
verdes, tudo apagado) no início, controlado por `led_test` no ambiente, com o
acesso ao motor de LED do SoC e ao RTL8231 portados (1.816 bytes); registradores
e console verificados na placa e aspecto visual confirmado pelo usuário —
registrado por Claude Sonnet 5 (IA).*

## V218 — console limpo: os marcadores de depuração do Stage-2 ficam desligados

Pedido do usuário: "tem como não emitir isso no u-boot? desativar esses logs",
com a colagem do início do boot (`123T41Z`, `G:…`, `!PASS!`, `!JUMP!`, depois
`0125L6FPDUVW…`, `#00#01…#1a`, `#1b…#24` e `TUVMabcdefgh`). Registrado por Claude
Sonnet 5 (IA).

**O que era o quê.** O texto colado tem duas origens. (1) O **Stage-1** (o
preloader do treino de DDR): `123T41Z`, `G:`, `DQM:`, `R0:`, `R1:`, `!PASS!`,
`S2:`, `!STAGE2_READY!` … `!JUMP!`. (2) O **Stage-2** (o U-Boot): as letras e
dígitos soltos, o `J:…:…:…:…`, os `#NN` de `initcall_run_r()` e as letras finais
antes de `Hit any key`. São os marcadores do bring-up V170 a V200 (escritas cruas
na UART, que funcionam antes de o console existir) e foram deixados ligados
porque usavam o mesmo símbolo dos ajustes de verdade da placa,
`INTELBRAS_SG1002_MR_FACTORY_PAYLOAD`, então não havia como desligar só eles.

**O que foi feito (só o Stage-2).** Um símbolo próprio,
`SG1002_MR_BOOT_TRACE` (`board/intelbras/sg1002_mr/Kconfig`), **desligado por
padrão**, guarda só os caracteres: `start.S` (macro `sg1002_stage2_trace`),
`board_f.c`, `board_r.c` (os `#NN`), `reloc.c`, `fdtdec.c`, `serial-uclass.c`,
`serial_rtl8380.c`, `main.c`, o `L` de `lowlevel_init()` e o `B` de
`board_early_init_f()`. Ficam como estavam, de propósito: os atrasos de espera
ativa de `board_f.c` (`sg1002_init_delay()`, sobras do V178 que ainda estão no
caminho), todos os ajustes funcionais e as mensagens úteis (`LED test`, `Boot:`,
as linhas `rtl8380: PHY …`). Para voltar a ver os marcadores:
`CONFIG_SG1002_MR_BOOT_TRACE=y`.

**Verificação.**

- Com o símbolo **ligado**, o `u-boot.bin` sai do mesmo tamanho do V217 (219.922
  bytes) e só 15 bytes diferem, todos no hash e na data da string de versão: a
  refatoração não muda nada do que existia.
- Com o símbolo **desligado** (o padrão): `u-boot.bin` de 216.002 bytes (3.920 a
  menos; sobram 13.374 na janela de `0x38000`) e nenhum símbolo de trace no
  binário. Stage-1 idêntico byte a byte (`beda62e8…`), `env.bin` igual.
- Gravado só o Stage-2 (`0x8000..0x3ffff`, o ambiente não foi tocado) por TFTP +
  `sf`, dois downloads iguais, leitura de volta para um buffer recém-preenchido
  com `0xaa` e `cmp.b` de 229.376 bytes idêntico.
- **20 de 20 resets** limpos: versão, `LED test: SYS, amber, green, off`, prompt,
  nenhum caractere de trace e nada entre o `!JUMP!` do Stage-1 e o banner. O
  OpenWrt sobe normalmente a partir dele (33 s do `boot` ao shell).
- Uma primeira rodada de 10 resets ficou inconclusiva (9 de 10, o quarto sem
  resposta): o `socat` que o usuário deixou aberto na mesma serial dividia os
  bytes com o broker, e a saída chegava truncada. Refeita com esse leitor pausado
  (`SIGSTOP`, depois `SIGCONT`), sem falhas.

**O que continua aparecendo.** As ~18 linhas do **Stage-1** (`DCWKQUXV56…`,
`P123T4V1Z`, `G:…` até `!JUMP!`) continuam, porque vêm do preloader do treino de
DDR, que não foi mexido. Ele é um assembly de 8.544 linhas com dezenas de
sondas ligadas por `-D`, emite os caracteres um a um, e trocar o Stage-1 exige
regravar `0x000000..0x007fff`, sem a rede de segurança do TFTP + `sf` (só o
CH341A recupera se algo der errado); além disso, o histórico deste projeto (V178,
V210) mostra comportamento sensível a tempo perto dessa parte do boot. Fica como
decisão do usuário, não foi feito.

**Ressalva sobre o `baudrate`.** Os marcadores de `serial_rtl8380.c` faziam parte
do tempo em volta de uma troca de `baudrate` para um valor diferente de 115200
(o risco de ~1 em 7 do V210). Com 115200, o padrão, essa troca nunca acontece;
se alguém salvar outro `baudrate`, esse risco deve ser medido de novo com o
símbolo desligado.

*Resultado V218: o console do U-Boot deixa de emitir os marcadores do Stage-2
(símbolo `SG1002_MR_BOOT_TRACE`, desligado por padrão); 20/20 resets limpos e
OpenWrt subindo; o bloco do Stage-1 segue igual — registrado por Claude Sonnet 5
(IA).*

## V219 — Stage-1 silencioso, regravado sem apagar nada

Pedido do usuário, depois do V218: tirar também as linhas do Stage-1, com a
condição "desde que nós tenhamos um ponto anterior estável reproduzível pode
gravar, ch341a está sempre disponível". Registrado por Claude Sonnet 5 (IA).

**1. O ponto anterior estável, antes de qualquer gravação.** A flash em
`0x000000..0x04ffff` foi lida de volta para um buffer novo e comparada com
`cmp.b` contra uma imagem reconstruível: **327.680 bytes idênticos**. Detalhes:

- `0x000000..0x0076bb`: Stage-1 V174 (`beda62e8…`, que sai igual do fonte).
- `0x0076bc..0x007fff`: 2.372 bytes de um Stage-1 mais antigo que sobraram na
  flash e nunca são executados. Como a primeira comparação apontou essa
  diferença (a imagem gerada tinha `0xff` ali e o chip tinha código), esses
  bytes foram lidos do chip e postos na imagem, para que ela fique **igual ao
  chip**, não só equivalente.
- `0x008000..0x03ffff`: Stage-2 V218; `0x040000..0x04ffff`: ambiente padrão.
- Guardada em `artifacts/known-good-v218/` (com `SHA256SUMS`, `README.md` e a
  versão de 16 MiB que o `gravar_spi_ch341a.sh` exige) e marcada no git como
  **`sg1002-mr-v218-stable`**. Restauração com o CH341A (placa desligada):
  `gravar_spi_ch341a.sh --image artifacts/known-good-v218/known-good-v218-chip-16m.bin
  --region 0x000000:0x04ffff --confirm-write`.

**2. A mudança.** Em `sg1002_mr_sram.S`, o `sw t1, 0(t0)` que põe cada byte na
UART passou a ser a macro `uart_tx_store`; com `-DSG1002_MR_PRELOADER_QUIET` ela
vira um `nop` do mesmo tamanho. Não muda o layout, a espera de THRE nem o laço
de acomodação fixo depois de cada byte (`0x20000` voltas), então o tempo do boot
inteiro fica o mesmo, com o console mudo. `putc_loud` mantém o marcador de falha
do DDR (`E` em `ddr_error`) audível. O script fixo do V174 só ganhou um gancho
opcional (`SG1002_MR_PRELOADER_EXTRA_FLAGS`); o `tools/build-sg1002-mr.sh`
constrói o Stage-1 silencioso por padrão, e `SG1002_MR_STAGE1_TRACE=1` devolve o
com trace.

**3. Por que dá para gravar sem risco de brick.**

- Sem o flag, o Stage-1 continua saindo `beda62e8…`: a edição do assembly é
  neutra.
- Com o flag, mudam exatamente **153 palavras**, todas `0xad090000` (`sw
  t1,0(t0)`) → `0x00000000` (`nop`), no mesmo tamanho (30.396 bytes,
  `9b1fdd23…`). Confirmado com `objdump`: só restou 1 envio de byte à UART (o
  `E`).
- Todo byte da imagem nova é subconjunto (bit a bit) da antiga. Flash NOR só
  troca bits de 1 para 0 sem apagar, então a imagem nova foi gravada **por cima da
  antiga, sem `sf erase`**. Não existe janela em que o setor está apagado: numa
  queda de energia no meio, cada palavra é a antiga ou a nova, e as duas são
  código válido.

**4. Gravação e verificação (2026-09-23).** `sf write 0x82000000 0x0 0x76bc`
(`Written: OK`), leitura de volta para um buffer com `0xaa` e `cmp.b` dos 30.396
bytes idêntico; depois a região inteira `0x000000..0x04ffff` contra a imagem
esperada (Stage-1 silencioso + resto igual): **327.680 bytes idênticos**. Nada
fora dos primeiros `0x76bc` bytes foi tocado. A nova imagem está em
`artifacts/known-good-v219/`.

- **20 de 20 resets** (que rodam o Stage-1 e o treino de DDR inteiros): o texto
  começa no banner do U-Boot, sem nada antes; depois `LED test`, o prompt e
  nenhum caractere de trace.
- O OpenWrt sobe normalmente (33 s do `boot` ao shell), 128 MiB de RAM
  (`Memory: 117364K/131072K`) e nenhum `Oops`/`BUG`.
- Sem a `socat` do usuário disputando a serial (ver V218), que foi pausada
  com `SIGSTOP` durante os testes.

**Efeitos e ressalvas.**

- Sumiu a visibilidade do progresso do Stage-1 (`G:`, `DQM:`, `R0/R1`,
  `!PASS!`…). Se o treino de DDR falhar, ainda sai o `E`, mas as outras falhas
  passam a ser silêncio. Para depurar, construa com `SG1002_MR_STAGE1_TRACE=1`.
- **Voltar ao Stage-1 com trace não é possível "por cima"**: trocar `nop` por
  `sw` põe bits de 0 para 1, o que exige apagar o setor. Faça com o CH341A (a
  imagem `known-good-v218` acima) ou, com o U-Boot funcionando, `sf erase` +
  `sf write` (aí sim existe uma janela sem Stage-1 válido).
- Não testado: uma **partida a frio de verdade** (tirar e pôr a energia). O
  `reset` refaz o Stage-1 e o DDR, mas não desliga a alimentação do RTL8231 e da
  DRAM. O comportamento esperado é o mesmo, pois nada de timing mudou.

*Resultado V219: o Stage-1 deixa de imprimir (153 `sw`→`nop`, mesmo tamanho e
tempo, `E` de falha mantido), gravado por cima sem apagar depois de provar o
ponto estável anterior (`sg1002-mr-v218-stable`, idêntico à flash); 20/20 resets
com o banner como primeira linha e OpenWrt subindo — registrado por Claude
Sonnet 5 (IA).*

## V220 — refatoração: só o que reproduz o binário gravado

Pedido do usuário: "atualmente tem 182 arquivos, está na hora de refatorar,
remover código inútil, manter só o funcional que consegue reproduzir o binário
completo que temos agora no estado atual". Registrado por Claude Sonnet 5 (IA).

**Critério.** O binário é o do V219, o que está gravado na placa: `preloader.bin`
(`9b1fdd23…`, e o traçado `beda62e8…` com `SG1002_MR_STAGE1_TRACE=1`),
`env.bin` e `u-boot.bin`. Tudo tem de sair bit a bit igual, com uma única exceção:
a string de versão do `u-boot.bin` (hash do git e data da compilação, ~15 bytes).
Cada etapa da refatoração foi seguida de um rebuild e dessa comparação; nenhuma
passou sem estar idêntica. A conferência ficou no repositório:
`tools/check-sg1002-mr-repro.py` compila e compara com a imagem estável
(`artifacts/known-good-v219/`). Ele fixa o sufixo da versão com
`SG1002_MR_LOCALVERSION` (do mesmo tamanho do da referência), para que o
resultado não dependa de a árvore do git estar limpa ou suja: sem isso o
`-dirty` de 6 caracteres deslocaria tudo o que vem depois na string de versão.
Foi rodado também num checkout limpo do que está no índice, sem `.git`, o que
prova que nada de necessário ficou de fora do git.

**Resultado.** 182 → 49 arquivos em relação ao upstream (`ece349ad`), 26.144 →
12.051 linhas adicionadas; o Stage-1 em assembly foi de 8.569 para 1.298 linhas.
Nada foi regravado: a flash continua sendo o V219 e agora o fonte a reproduz.

**O que saiu.**

- `tools/`: os 96 `build-sg1002-mr-vNNN-*.sh` (retratos datados de builds
  antigos), outros 6 scripts de sondas, os `make-*-image.sh`, o blob de
  ambiente antigo, os 3 `.layout`, o `Dockerfile` extra e as ferramentas Linux
  de diagnóstico de LED e de varredura do RTL8231. O build ficou num só script,
  `tools/build-sg1002-mr.sh` (a flag de sondas do V174 e o script do
  preloader foram absorvidos), mais o `check-sg1002-mr-preloader.sh`.
- Fontes: `sg1002_mr_factory_entry.*` e `sg1002_mr_vendor_handoff.*` (só
  usados pelos scripts removidos), `sram_init.S` (só o caminho sem payload de
  fábrica), os defconfigs `intelbras_sg1002_mr_defconfig` e
  `rtl8380m_intphy_2fib_1g_demo_factory_payload_defconfig`.
- Stage-1: o preprocessador foi resolvido com as flags reais do build. Das 537
  diretivas condicionais, só a `SG1002_MR_PRELOADER_QUIET` ficou; foram embora
  as sondas compiladas para fora, 14 macros sem uso e as descrições de
  experimentos que já não existem.
- Stage-2: o símbolo `SG1002_MR_BOOT_TRACE` e todo o código dele (`sg1002_*_trace`,
  `SG1002_STEP`, a macro do `start.S`), o `main.c` e o `start.S` voltaram a ser
  idênticos ao upstream (para isso saiu o `select MIPS_SRAM_INIT` do Kconfig do
  SoC), a checagem de `#if` órfãs, e os comentários de "resultado físico Vnnn"
  foram condensados em duas ou três linhas de motivo.

**O que foi mantido, mesmo sem função, por reproduzir o binário.** Cada item
abaixo está compilado no binário gravado; removê-lo o mudaria e pediria novo teste
no hardware, então ficou como possível limpeza *seguinte*:

- as esperas ocupadas `sg1002_init_delay()` do `board_f.c` e a de 5.000.000
  voltas depois de `reserve_round_4k()`;
- os `putc('R')`, `putc('C')`, `putc('F')`, `putc('J')` do `reloc.c` (mudos, o
  console ainda não existe);
- o laço que conta os dispositivos de `UCLASS_SERIAL` em `serial-uclass.c`
  (o resultado não é usado; 32 bytes);
- `board_early_init_f()` e `board_postclk_init()` vazias (ligadas por Kconfig);
- no Stage-1: cada `putc` é agora um atraso (o `nop` mais o laço de acomodação,
  que é o que preserva o tempo), e há código inalcançável depois de
  `ddr_trace_probe_loop`.

`doc/board/intelbras/*.rst` não foram tocados (os planos
`sg1002-mr-antigravity-execution-plan.rst` e `sg1002-mr-codex-review.rst` são
candidatos óbvios a sair). As seções antigas deste documento citam scripts que
já não existem: estão no histórico, na branch `review/sg1002-mr-boot-chain`
e no snapshot `backup/pre-refactor-v219`.

*Resultado V220: 182 → 49 arquivos, binários bit a bit iguais aos gravados no
V219 (Stage-1, ambiente e Stage-2 fora a string de versão), com o build reduzido
a um script e a conferência de reprodução dentro do repositório — registrado por
Claude Sonnet 5 (IA).*

*Resultado V199, boot completo e limpo a 115200 bps o tempo inteiro
(todos os passos, `run_main_loop()`, `main_loop()` até `'k'`), mas
zero saída de `printf()` real, achado da quarta instância do mesmo
bug (`console_init_f()` pulado, `GD_FLG_HAVE_CONSOLE` nunca definida),
e entrada V200 (chamar `console_init_f()` pós-realocação, nova tabela
de 32 passos) registrados por Claude Sonnet 5 (IA).*

*Resultado V198, atraso de 2s sobrevivido mas achado real feito
manualmente pelo usuário — a placa nunca travava, apenas trocava para
9600 bps e continuava o boot normalmente, provando que V196-V198
perseguiam um bug inexistente — e entrada V199 (forçar 115200 bps
incondicionalmente, eliminando a troca de banda em vez de dar suporte
a ela) registrados por Claude Sonnet 5 (IA).*

*Resultado V197, nenhuma mudança (log idêntico à V196, hipótese do
vazamento de `IER` refutada), e entrada V198 (teste de atraso de 2s
para descartar tempo acumulado de boot) registrados por Claude Sonnet
5 (IA).*

*Resultado V196, toda a cadeia de descoberta do console completa pela
primeira vez (`stdio_register_dev()` com sucesso, `cur_serial_dev`
definido), travando num terceiro ponto de chamada de `setbrg()`
(`serial_init()`→`serial_setbrg()`) não coberto pela V196, e entrada
V197 (remascarar `IER` na raiz, cobrindo qualquer chamador) registrados
por Claude Sonnet 5 (IA).*

*Resultado V195, trava localizada com precisão (a segunda chamada de
`setbrg()`, a 9600 bps vindo do ambiente, trava exatamente onde a
primeira, a 115200 bps, tinha completado sem erro), e entrada V196
(pular a resincronização redundante para esta placa) registrados por
Claude Sonnet 5 (IA).*

*Resultado V194, avanço decisivo — magic do FDT válido, primeiro
dispositivo vinculado, primeira execução de `rtl8380_uart_probe()` e
`serial_post_probe()` em toda a sessão, travando na segunda chamada de
`setbrg()` — e entrada V195 (isolando a trava com um único marcador
extra, mínimo por causa do slot apertado) registrados por Claude
Sonnet 5 (IA).*

*Resultado V193, `00000000` — confirmado que `_end` aponta para
memória vazia pós-realocação (o DTB real está no endereço
pré-realocação), e entrada V194 (correção via `gd->reloc_off`,
remoção do teste de `printf()` da V186 para caber no slot)
registrados por Claude Sonnet 5 (IA).*

*Resultado V192, `ABCDEF` completo (blob do FDT finalmente não-nulo)
mas contagem de dispositivos ainda `#00`, achado de uma terceira
instância do mesmo bug (`fdtdec_prepare_fdt()` pula toda validação), e
entrada V193 (transmissão crua do magic number do FDT, após reverter
uma correção completa que estourou o slot de flash) registrados por
Claude Sonnet 5 (IA).*

*Resultado V191, `#00` — zero dispositivos vinculados a
`UCLASS_SERIAL`, achado de que `gd->fdt_blob` nunca é definido porque
`fdtdec_setup()` é pulado inteiramente nesta placa (mesma classe de
bug da V188), e entrada V192 (chamada real de `fdtdec_setup()`
pós-realocação, nova tabela de passos com 31 entradas) registrados por
Claude Sonnet 5 (IA).*

*Resultado V190, sequência `<n3456789` — todas as três buscas de
dispositivo falham, confirmando falha de busca (não de `probe()`) — e
entrada V191 (contagem de dispositivos vinculados a `UCLASS_SERIAL`)
registrados por Claude Sonnet 5 (IA).*

*Resultado V189, mesmo ponto de parada da V188 e achado de que a trava
é anterior a `rtl8380_uart_probe()` (marcador `'{'` nunca apareceu), e
entrada V190 (rastreamento de `serial_find_console_or_panic()`)
registrados por Claude Sonnet 5 (IA).*

*Resultado V188, regressão instrutiva (para entre `#0d`/`#0e`, prova
que `rtl8380_uart_probe()` finalmente executa pela primeira vez nesta
sessão) e entrada V189 (rastreamento de `probe()`/`setbrg()` em
`serial_rtl8380.c`) registrados por Claude Sonnet 5 (IA).*

*Resultado V187, causa raiz identificada (`serial_init()`'s guarda de
tempo de compilação nunca permite a seleção do console DM
pós-realocação) e entrada V188 (correção real, não apenas mais uma
sonda de diagnóstico) registrados por Claude Sonnet 5 (IA).*

*Resultado V186, ambos os testes diretos de `printf()` sem nenhuma saída
visível (confirmando que a quebra está na camada de console/stdio, não
no driver da UART), correção da tabela de passos V184 (`initr_announce`
estava faltando, rótulos agora em hexadecimal) e entrada V187
(rastreamento de `serial_post_probe()`) registrados por Claude Sonnet 5
(IA).*

*Resultado V185, toda a instrumentação de `run_main_loop()`/`main_loop()`
confirmada (`T`..`k` completos, incluindo a contagem regressiva de
`bootdelay_process()`) sem nenhuma saída de `printf()` visível, e
entrada V186 (dois testes diretos de `printf()` cercados por `@`/`$`)
registrados por Claude Sonnet 5 (IA).*

*Resultado V184, todos os 30 passos numerados completos, achado do
ambiente de fábrica válido em `0x040000` e entrada V185 (rastreamento
de `run_main_loop()`/`main_loop()`) registrados por Claude Sonnet 5
(IA).*

*Resultado V183, avanço decisivo (`Q`/`K`/`N`/`O`/`r`/`!` completos, primeira
entrada comprovada em `board_init_r()`) e entrada V184 (contador de passos
numerado em `initcall_run_r()`) registrados por Claude Sonnet 5 (IA).*

*Resultado V182, achado do `putc()`/`pre_console_putc()` mudo e entrada
V183 (traces crus em `relocate_code`/`board_init_r`) registrados por
Claude Sonnet 5 (IA).*

*Resultado V181, ambiguidade da transmissão UART e entrada V182 (trace
confiável com espera de THRE) registrados por Claude Sonnet 5 (IA).*

*Resultado V180, convergência com o comportamento do emulador e entrada
V181 (dump hexadecimal dos argumentos de `relocate_code`) registrados por
Claude Sonnet 5 (IA).*

*Resultado V179, achado da causa provável (`boardf` não zerada / FDT nulo)
e correção pendente de gravação (V180) registrados por Claude Sonnet 5
(IA).*

*Resultado V178, descarte do temporizador de tempo fixo e entrada V179
(varredura de delay) registrados por Claude Sonnet 5 (IA).*

*Resultado V177, achado do `board_late_init`/watchdog e entrada V178
(teste de temporizador fixo) registrados por Claude Sonnet 5 (IA).*

*Resultado V175/V176, tentativa de pilha em DRAM descartada, achado sobre
a não confiabilidade do emulador para este ponto e entrada V177
registrados por Claude Sonnet 5 (IA).*

*Resultado V174, achado da trilha de trace pré-existente, recompilação do
U-Boot GPL, achado do defconfig desatualizado e entrada V175 registrados
por Claude Sonnet 5 (IA).*

*Resultado V173, correção do endereço de entrada, decisão do usuário e
entrada V174 registrados por Claude Sonnet 5 (IA).*

*Resultado V172, achados do desassembly/SDK/bloco de parâmetros e entrada V173
registrados por Claude Sonnet 5 (IA).*

*Resultado V171, achados do kernel/diff de init e entrada V172 registrados por
Claude Sonnet 5 (IA).*

*Resultado V170, diferenças estruturais e entrada V171 registrados por Claude
Sonnet 5 (IA).*

*Resultado V169, achado DMCR/MCR e entrada V170 registrados por Claude Sonnet 5 (IA).*

*Plano e entrada V169 registrados por Claude Sonnet 5 (IA).*

*Detalhe V167 e entrada V168 registrados por Claude Sonnet 5 (IA).*

V164 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v164-dq16-read-bounds-prefix-scan-16m.bin` (SHA-256
`7167169247b48687be47387bf3f0aef42932bacb7905fa46b451cf2d242978ff`).

V163 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v163-dq19-latch-settle-full-copy-16m.bin` (SHA-256
`3773a84aa6eb155cd40f12dd9f41a64709772a49662f1490d034326b189691ca`).

V161 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v161-dq19-boundary-baseline-full-copy-16m.bin` (SHA-256
`8c2da95f1cbff03559eb3840f2a14dbc6827df581aa966d99db90e3b3e0f5775`).
O TTL deve atingir `!COPY!` e retornar `!FULL_OK!` ou o primeiro
`E:<offset>:<flash>:<ddr>` sem saltar ao U-Boot.

V152 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v152-dq16-write-dphy-full-scan-16m.bin` (SHA-256
`ced822cb57812a27a7456d70ee9a7fa79c731d17b56a8aa66534bd7583670fe8`).

V151 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v151-dq16-phase-dphy-full-scan-16m.bin` (SHA-256
`f5e398c2abbc2e0f43189e463af6a82727fd07f2d4b83c465aed5a7663a625ee`).

V150 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v150-dq16-24-dq19-dphy-full-scan-16m.bin` (SHA-256
`85c4af983cc5d69acc2fb0d64e418d766bb845e7a224444fdfba00cc2d830efd`).

V149 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v149-dq16-dphy-full-scan-16m.bin` (SHA-256
`a91085740287f89f3d91f5a3688ab146a7ba5ea7706a55b5fac175f8586d5c13`).

V148 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v148-dq16-r28-dphy-latch-stage2-copy-16m.bin` (SHA-256
`d82800ca8293602065bd389294f37c24ef1a7ea4e4c2cafbce739b3b20368a77`).

V146 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v146-dq16-r28-pre-dqm-stage2-copy-16m.bin` (SHA-256
`afab2e6cd8ba22342407b34e54b8c0cebf9e21d943ba19a59bd3c7e903c9c3b2`).

V144 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v144-dq16-then-gpl-dqm-stage2-copy-16m.bin` (SHA-256
`d398558bd11cd48eaef2bd19e02c50582e7aa73d8d64b42fbdbed65d4cc057dd`).

V142 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v142-dq16-dqm-recommit-stage2-copy-16m.bin` (SHA-256
`aee9fe27251ea4cdac9dda75d982f9343b08e4c8430c676f376c01dd2b0cf358`).

V141 foi gravada na SPI física em 2026-09-21, limitada a
`0x000000..0x007fff`, com erase/write/verificação confirmados pelo CH341A.
Imagem `sg1002-mr-v141-dq16-read-sweep-full-copy-16m.bin` (SHA-256
`c5392e8b4119a00a81c278228386363d5e6b9f36bcdb150d570a5a387f554e2e`).
