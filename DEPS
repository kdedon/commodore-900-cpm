# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dir]
#
# Read by `make deps' (tools/deps-fetch.sh).  The build resolves dependencies
# through tools/deps.sh; a named variable wins over anything here.
#
# kind git      cloned beside this repository, floating on <ref>
# kind release  a published archive, pinned to <ref>
#
#   toolchain  compiler + assembler + linker for the Z8001 -- the one
#              input `make all' needs.  A RELEASE, not a checkout: `make
#              all' produces the bytes that ship, so what compiles them has
#              to be a PIN, not whatever `main' happens to be the day the
#              build runs.  Bumped by hand, same as kernel3's own edge onto
#              this same repository.
#
#   kboot      the loader, and its include/bootinfo.h.  The BIOS COMPILES that
#              header -- it is the layout of the block the loader writes, and a
#              copy of it here would drift against the loader that fills it in
#              -- so `make all' needs the checkout, the same way kernel3 does.
#              The built loader is what the bootable CP/M medium is made with.
#
# Verify only.  `make all' needs neither; `make verify' needs both.
#   emu        the c900 emulator binary, to run the guest tests
#              format, built on the host as an independent oracle

toolchain  release  https://github.com/kdedon/commodore-900-toolchain  v0.1.7  c900-toolchain-@REF@-@HOST@
kboot      git      https://github.com/kdedon/commodore-900-kboot      main
userland   git      https://github.com/kdedon/commodore-900-coh-userland  main
emu        release  https://github.com/kdedon/commodore-900-emulator   v0.2  c900-@REF@-@HOST@
