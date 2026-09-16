# CP/M-8000 for the Commodore 900


## Build

    make deps DEP=toolchain
    make
    make clean
    make help

toolchain. Set `C900_TOOLCHAIN` to use a local checkout; otherwise the resolver
searches `deps/` and adjacent checkout directories.

Outputs:

| File | Description |
|---|---|
| `build/cpm.sys` | bootable system image |
| `build/cpma.img` | 10 MB development drive A |
| `build/cpma-rel.img` | release drive A without test programs |
| `build/cpmb.img` | 8 MB drive B |


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
