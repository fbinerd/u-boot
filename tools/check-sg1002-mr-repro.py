#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Check that a build reproduces a known-good SG1002 MR boot region.

The reference is a 0x000000-0x04ffff flash image (default:
artifacts/known-good-v238/known-good-v238-composed-0x000000-0x04ffff.bin).  The
build is made with tools/build-sg1002-mr.sh (CROSS_COMPILE must be set) unless
--no-build is given.  Compared:

  Stage-1  preloader.bin against the start of the reference, bit for bit
  Stage-2  u-boot.bin against 0x008000..., bit for bit except the version
           string, which embeds the git hash and the build date.  The build is
           made with a suffix of the same length as the reference's, so a clean
           or dirty git tree (or none) does not change the layout.
  env      env.bin against 0x040000-0x04ffff, bit for bit

This checker substitutes a synthetic version suffix to keep its reference
comparison stable. Its output is not a flash candidate; build directly with
tools/build-sg1002-mr.sh for a versioned image.

The bytes between the end of Stage-1 and 0x008000 hold code of an older
Stage-1 that is never executed, so they are not compared.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

REF_DEFAULT = 'artifacts/known-good-v238/known-good-v238-composed-0x000000-0x04ffff.bin'
STAGE2_OFFSET, ENV_OFFSET, ENV_SIZE = 0x8000, 0x40000, 0x10000
VERSION_KEY = b'U-Boot 2026.07-g'
VERSION_RE = re.compile(
    rb'U-Boot 2026\.07-g[0-9a-f]{12}(?:-dirty)?\+? '
    rb'\([A-Z][a-z]{2} [ 0-9][0-9] [0-9]{4} - '
    rb'[0-9]{2}:[0-9]{2}:[0-9]{2} [+-][0-9]{4}\)\0'
)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--ref', default=REF_DEFAULT)
    ap.add_argument('--out', default='build-gpl/sg1002-mr-repro')
    ap.add_argument('--no-build', action='store_true', help='check an existing build in --out')
    args = ap.parse_args()

    def rd(path):
        with open(path, 'rb') as f:
            return f.read()

    ref = rd(args.ref)
    if not args.no_build:
        build_root = (Path(__file__).resolve().parents[1] / 'build-gpl').resolve()
        raw_out = Path(args.out)
        build_out = raw_out.resolve()
        if raw_out.is_symlink() or build_root not in build_out.parents:
            ap.error('--out must be a non-symlink subdirectory of build-gpl/')
        m = re.search(rb'U-Boot 2026\.07(-g[0-9a-f]{12}(?:-dirty)?\+?)', ref)
        env = dict(os.environ)
        if m:
            suffix = m.group(1)
            env['SG1002_MR_LOCALVERSION'] = (
                '-g' + '0' * 12 +
                ('-dirty' if b'-dirty' in suffix else '') +
                ('+' if suffix.endswith(b'+') else '')
            )
            # setlocalversion otherwise appends '+' when LOCALVERSION is unset,
            # shifting the binary layout for a clean reference.
            env['LOCALVERSION'] = ''
        if build_out.exists():
            shutil.rmtree(build_out)
        subprocess.run(['sh', 'tools/build-sg1002-mr.sh', args.out], check=True, env=env)
    if len(ref) != ENV_OFFSET + ENV_SIZE:
        sys.exit('%s: expected %#x bytes, found %#x' % (args.ref, ENV_OFFSET + ENV_SIZE, len(ref)))
    stage1, stage2, env = (rd(os.path.join(args.out, n)) for n in ('preloader.bin', 'u-boot.bin', 'env.bin'))

    ok = True

    def report(name, good, note=''):
        nonlocal ok
        ok &= good
        print('%-8s %s%s' % (name, 'identical' if good else 'DIFFERENT', note))

    report('Stage-1', ref[:len(stage1)] == stage1)

    ref2 = ref[STAGE2_OFFSET:STAGE2_OFFSET + len(stage2)]
    key = ref2.find(VERSION_KEY)
    diffs = [i for i in range(len(stage2)) if stage2[i] != ref2[i]] if len(ref2) == len(stage2) else None
    # The build can add '+' to the version, moving its NUL by one byte.  Match
    # both complete strings and exclude only their occupied bytes, never the
    # pointers that follow them (which the former 90-byte allowance missed).
    ref_version = VERSION_RE.match(ref2, key) if key >= 0 else None
    new_version = VERSION_RE.match(stage2, key) if key >= 0 else None
    version_end = max(ref_version.end(), new_version.end()) if ref_version and new_version else -1
    good = (diffs is not None and version_end > key and
            all(key <= i < version_end for i in diffs) and
            all(b == 0xff for b in ref[STAGE2_OFFSET + len(stage2):ENV_OFFSET]))
    report('Stage-2', good, ' (except the version string, %d bytes)' % len(diffs) if good else '')

    report('env', ref[ENV_OFFSET:] == env)
    print('RESULT:', 'the build reproduces the known-good image' if ok else 'MISMATCH')
    if b'U-Boot 2026.07-g000000000000' in stage2:
        print('NOT A FLASH IMAGE: this checker used a synthetic version; '
              'run tools/build-sg1002-mr.sh directly for a versioned image')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
