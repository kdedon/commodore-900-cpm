# CP/M-8000 for the Commodore 900

CP/M-8000 for the Zilog Z8001-based Commodore 900: the machine BIOS, the
BDOS, the CCP and the utilities. The BDOS implements the CP/M 3 function
set -- function 12 answers `0x2031`, and functions 44, 45, 49, 60, 98-105
107-112 and 152 are there, with SFCB date stamps, directory labels, chained
RSX modules, extended error returns, multi-sector I/O and XFCB file
passwords. The CCP loads from `A:CCP.Z8K` on cold and warm boots.

Every command built from `src/cmd` publishes `main`'s status as the
function 108 program return code, so the CCP's `IF ERROR` can branch on it
in a SUBMIT file. The five `src/app` applications -- `SDB`, `SORTFL`,
`KILLDU`, `TOHEX`, `FROMHEX` -- are the exception: they are compiled and
linked on the target against DRI's `STARTUP.O` and `LIBCPM.A` from
`vendor/`, not against `src/cmd/crt0.s`, so they set no return code and
`IF ERROR` reads success after them however they ended. See
`DEVIATIONS.md` §10 in the design notes. The system does
not use GENCPM. The system banner identifies it as version 3.1.

Passwords are enforced on a drive whose directory label has the
password-enable bit set, and only there: a medium without that bit behaves
exactly as it did before they existed. `SET [PROTECT=ON]` sets the bit and
`SET [PASSWORD=]` gives the label its own password. The *file* forms of
`[PASSWORD=]` and `[PROTECT=READ|WRITE|DELETE]` are BDOS function 103, and
`SET [DEFAULT=]` is function 106; both are implemented, and together they
are the round trip -- lock a file, and supply its password once so that
every program on the disk can open it, since no CCP prompts for one.

The system banner carries the date the system was BUILT: `CPMDATE` and
`COPYYEAR` in the Makefile come from `date` on the build host. This is
deliberate -- a build date that is a build date -- so the same source
rebuilt on another day produces a different banner, and `cpm.sys` is not
reproducible byte-for-byte across days.

## Build

    make deps DEP=toolchain
    make
    make clean
    make help

The build requires a host C compiler, Python 3, `cpp`, and the Commodore 900 Z8001
toolchain. Set `C900_TOOLCHAIN` to use a local checkout; otherwise the resolver
searches `deps/` and adjacent checkout directories.

Outputs:

| File | Description |
|---|---|
| `build/cpm.sys` | bootable system image |
| `build/cpma.img` | 10 MB development drive A |
| `build/cpma-rel.img` | release drive A without test programs |
| `build/cpmb.img` | 8 MB drive B |
| `build/cpmonly.bin` | bootable release disk, built when kboot is available |

The filesystem images are sparse files. Set `KBOOT` to a built loader to
produce `cpmonly.bin`; without it, `make` produces the standalone images.

Build rules live in `mk/config.mk` (settings and program lists),
`mk/system.mk` (resident system), `mk/programs.mk` (transient programs),
and `mk/images.mk` (release packaging). Test disk recipes are in
`tests/images.mk`, and runtime checks are in `tests/verify.mk`.

## A local medium with your own programs

The release disk carries only what this project may redistribute. To try
programs of your own -- for instance against the CP/M-80 and CP/M-86
compatibility shims on the development drive -- build a private medium:

    make cpmlocal LOCALDIR=<a directory of extra files> LOCALOUT=<a path outside the checkout>

The result is a bootable disk image: the development drive A, plus every
regular file in `LOCALDIR`, staged under the same 8.3 naming rules as the
rest of the disk (names are upper-cased; a name that does not fit is
reported rather than silently changed). Drive B and the system image are the
ones `make` just built.

The target is opt-in. Nothing in `make all` depends on it, it adds no check
to the ordinary build, and it names none of your files: the directory is the
whole interface.

**The licence boundary.** Files you supply are yours, not this project's,
and may not be redistributable. They are never copied into this repository,
never committed, and never vendored -- and neither is a medium built from
them. `make cpmlocal` therefore refuses an `LOCALOUT` that resolves inside
this checkout or inside the directory holding the sibling checkouts, and
writes nothing when it refuses. An ignored `build/` directory is not an
exception: an ignore rule is the only thing between such a file and a
commit. Give `LOCALOUT` a path somewhere else entirely. If the guarded
parent directory is wrong for your layout, set `LOCALGUARD` to the directory
that must stay clean.

## Verify

Runtime checks are individual `verify-*` targets:

    make verify-boot
    make verify-ccp
    make verify-rsx
    make verify-util

These checks require the Commodore 900 emulator and a built kboot binary. Set
`EMU` and `KBOOT`, or place sibling checkouts where `tools/deps.sh` can find
them. Directory-format checks also use `COHERENT_OS` as an independent reader.

## License

Project-authored code is MIT licensed. Digital Research, Zilog, and other
historical material remains under its original terms. See `LICENSE` and the
copyright and license notices shipped with the source.
