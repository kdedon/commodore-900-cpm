# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# The src/app applications, compiled ON THE MACHINE by DRI's ZCC.Z8K and
# LD8K.Z8K from the .C beside them, under the emulator.  They cannot be
# cross-built on the host: the host libcpm has no stdio, malloc or file
# layer, and DRI's LIBCPM.A is an archive format the cross linker cannot
# read.  So `all' does not build them; `make apps' does, into $(APPDIR),
# and the drive A images carry whatever is there when they are packed.
#
# Each program is built on its own copy of the boot medium made from
# drive A as `all' packed it, with tests/appbuild.sh (one cold boot per
# command; its header says why), then pulled off the partition.  A
# program is rebuilt only when its sources change.
APPDIR	= build/app
APPSDB	= CMD COM CRE ERR IEX INT IO JUNK MTH SCN SDB SEL SRT TBL
APPONE	= SORTFL KILLDU TOHEX FROMHEX
APPBIN	= $(APPDIR)/SDB.Z8K $(APPONE:%=$(APPDIR)/%.Z8K)

.PHONY: apps
apps: $(APPBIN)

# $(call APPBUILD,PROG,NAMES) -- build PROG.Z8K from NAMES' .C files;
# $(APPDIR)/PROG.bin and PROG.d are its scratch disk and the partition
# extracted from it, and PROG.log the build transcript.
define APPBUILD
	@sh tools/deps.sh -n emu '$(EMU)'
	@mkdir -p $(APPDIR)
	$(MKDISK) $(APPDIR)/$(1).bin $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	EMU='$(EMU)' sh tests/appbuild.sh $(APPDIR)/$(1).bin $(APPDIR)/$(1).log \
		$(1).Z8K $(2)
	rm -rf $(APPDIR)/$(1).d; mkdir -p $(APPDIR)/$(1).d
	dd if=$(APPDIR)/$(1).bin of=$(APPDIR)/$(1).d/cpma.img bs=512 \
		skip=$(CPMA_START) count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(APPDIR)/$(1).d/cpma.img $(APPDIR)/$(1).d
	@sh tests/appchk.sh $(APPDIR)/$(1).log $(APPDIR)/$(1).d - $(1).Z8K
	cp $(APPDIR)/$(1).d/$(1).Z8K $@
	rm -rf $(APPDIR)/$(1).d $(APPDIR)/$(1).bin
endef

$(APPDIR)/SDB.Z8K: $(APPSDB:%=src/app/%.C) src/app/SDB.H src/app/SDBIO.H \
		| $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	$(call APPBUILD,SDB,$(APPSDB))

$(APPDIR)/%.Z8K: src/app/%.C | $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	$(call APPBUILD,$*,$*)
