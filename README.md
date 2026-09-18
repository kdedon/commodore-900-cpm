# CP/M-8000 for the Commodore 900

CP/M-8000 for the Zilog Z8001-based Commodore 900: the machine BIOS, the
BDOS, the CCP and the utilities. The BDOS implements the CP/M 3 function
set -- function 12 answers `0x2031` -- with SFCB date stamps, directory
labels, chained RSX modules, extended error returns, multi-sector I/O and
XFCB file passwords, including the drive-wide password-enable bit that
`SET [PROTECT=ON]` turns on. The CCP loads from `A:CCP.Z8K` on cold and
warm boots, and the banner identifies the system as version 3.1. GENCPM
is not used.

The banner also carries the date of the build: `CPMDATE` and `COPYYEAR`
come from `date` on the build host, so the same source rebuilt on another
day gives a different banner and `cpm.sys` is deliberately not
reproducible byte-for-byte across days.

## Build

    make deps DEP=toolchain
    make deps DEP=kboot
    make
    make help

`make help` lists the remaining targets. The build needs a host C
compiler, Python 3, `cpp`, and the Z8001 toolchain and kboot named in
`DEPS` -- the toolchain taken at its latest release, kboot pinned at a
tag, both fetched by `make deps`. Set `C900_TOOLCHAIN`, `KBOOT` or
`KBOOTSRC` to use a local copy instead; otherwise the resolver searches
`deps/` and adjacent checkouts.

`make` builds everything on the host, the five `src/app` programs
(`SDB`, `SORTFL`, `KILLDU`, `TOHEX`, `FROMHEX`) included, and stages it
on the drive images:

| File | Description |
|---|---|
| `build/cpm.sys` | bootable system image |
| `build/cpma.img` | 10 MB development drive A |
| `build/cpma-rel.img` | release drive A without the test programs |
| `build/cpmb.img` | 8 MB drive B |
| `build/cpmonly.bin` | bootable release disk, built when kboot is available |

The filesystem images are sparse files. A `v*` tag publishes
`cpmonly.bin.gz`, `cpm.sys`, `cpma-rel.img.gz`, `cpmb.img.gz` and
`SHA256SUMS` as release assets (`.github/workflows/release.yml`).

Build rules live in `mk/config.mk`, `mk/system.mk`, `mk/programs.mk` and
`mk/images.mk`; test media and runtime checks in `tests/images.mk` and
`tests/verify.mk`.

`make cpmlocal` builds a private boot medium carrying the development
drive plus files of your own -- to try programs against the CP/M-80 and
CP/M-86 shims, say. It refuses a `LOCALOUT` inside this checkout or
beside it: what you supply is yours, and must not land where a commit
could pick it up.

## Verify

    make verify-all       every verification target, 97 of them, ~38 min
    make verify-<name>    one of them
    make verify-zcc       opt-in, outside verify-all; see make help

The checks run the built medium under the Commodore 900 emulator
(`make deps DEP=emu`, or set `EMU`) and need a kboot binary.
`dirfmt-check` and `verify-stamped` additionally read and write the
images with [cpmtools](http://www.moria.de/~michael/cpmtools/) as an
independent oracle: `apt install cpmtools`, or set `CPMTOOLS` to the
directory holding `cpmls`. [`tests/README.md`](tests/README.md)
describes the suite.

## License

Project-authored code is MIT licensed. Digital Research, Zilog, and other
historical material remains under its original terms. See `LICENSE` and the
copyright and license notices shipped with the source.
