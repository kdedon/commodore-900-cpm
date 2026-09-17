# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# Reserve every fourth directory slot for SFCB timestamps.
# Zero initial stamps are filled by the target clock.
LABEL	= C900A
LABELMODE = create,update
# Stage the src/app programs beside their source and the fixtures.
DISKA = build/diska
$(CPMAIMG): mk/config.mk mk/images.mk src/dist/disk-a src/app $(ZBASE) tools/mkcpmfs.py tools/sparse.py tools/stage-devpack.sh $(wildcard $(ZBASE)/*) \
		$(wildcard src/dist/disk-a/*) $(wildcard src/app/*) \
		$(UAPP) $(UPROGS) $(URSX) $(UCCP) | $(OBJDIR)
	@rm -rf $(DISKA)
	@mkdir -p $(DISKA)
	@for f in src/dist/disk-a/* src/app/* $(UAPP); do b=`basename $$f`; \
		cmp -s $$f $(DISKA)/$$b || cp $$f $(DISKA)/$$b; done
	sh tools/stage-devpack.sh $(ZBASE) $(DISKA)
	@for f in $(UPROGS) $(URSX) $(UCCP); do b=`basename $$f`; \
		cmp -s $$f $(DISKA)/$$b || cp $$f $(DISKA)/$$b; done
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKA)

# Release images exclude exercisers and application source.
# relcheck.sh requires every excluded name to exist in the development image.
ASRC	= SDB.C CMD.C COM.C CRE.C ERR.C IEX.C INT.C IO.C JUNK.C MTH.C \
	  SCN.C SEL.C SRT.C TBL.C SDB.H SDBIO.H \
	  SORTFL.C KILLDU.C TOHEX.C FROMHEX.C
ATEST	= MHELLO.Z8K CRSRDEMO.Z8K CONCOST.Z8K BIOCOST.Z8K CPUTCOST.Z8K MSCOPY.Z8K ERRTEST.Z8K CPM3FN.Z8K \
	  CONC.Z8K CONCB.Z8K CONCP.Z8K CONCQ.Z8K \
	  SCBTEST.Z8K STAMPT.Z8K TRUNCT.Z8K WILDT.Z8K PASST.Z8K ASTAMPT.Z8K LBLNEW.Z8K \
	  TRUNCB.Z8K TRUNCS.Z8K XFCBT.Z8K U0T.Z8K ROT.Z8K BIOSET.Z8K CONBRK.Z8K \
	  V3RET.Z8K V3FREE.Z8K RANEXT.Z8K CPMSYST.Z8K \
	  RSXT.Z8K RSXT2.Z8K Z80.Z8K CPM86.Z8K \
	  PROT.RSX PROTN.RSX UCASEL.RSX UCASE3.RSX UCASEH.RSX \
	  BADP.SUB TEST.SUB BIG.TXT MINI.8KN SDBIN.TXT
# UCASE.RSX remains as a usable example for RSXLDR.
DISKAR	= build/diska-rel
$(CPMARIMG): $(CPMAIMG) mk/images.mk tests/relcheck.sh tools/mkcpmfs.py tools/sparse.py
	@rm -rf $(DISKAR)
	@mkdir -p $(DISKAR)
	@for f in $(DISKA)/*; do b=`basename $$f`; \
		case " $(ATEST) $(ASRC) " in *" $$b "*) continue;; esac; \
		cp $$f $(DISKAR)/$$b; done
	@sh tests/relcheck.sh $(DISKA) $(DISKAR) $(ATEST) $(ASRC)
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $@ $(CPMA_BLOCKS) $(DISKAR)

# Drive B uses the same packer and timestamp label mode as drive A.
LABELB	= C900B
$(CPMBIMG): mk/config.mk mk/images.mk src/dist/disk-b tools/mkcpmfs.py tools/sparse.py $(wildcard src/dist/disk-b/*) | $(OBJDIR)
	python3 tools/mkcpmfs.py --label $(LABELB) --label-mode create,update \
		$@ $(CPMB_BLOCKS) src/dist/disk-b

# Use wildcard so a missing loader gets the resolver diagnostic instead of a make error.
$(CPMDISK): $(CPMSYS) $(CPMARIMG) $(CPMBIMG) $(wildcard $(KBOOT)) tools/mkcpmdisk.py tools/cohfs.py tools/sparse.py
	$(MKDISK) $@ $(CPMSYS) $(CPMARIMG) $(CPMBIMG)

# ---------------------------------------------------------------------------
# Boot-trace medium (OPT-IN; not part of `all').
#
#	make cpmtrace			-> build/cpmtrace.bin
#
# Same sources, compiled with -DBOOT_TRACE so src/bios/boottrace.h's
# BTRACE() markers are emitted on the cold path.  A machine that stops
# between the BIOS line and the BDOS sign-on prints its last marker and
# names the step.  The markers go out through the boot ROM's console
# dispatcher, not through the BIOS console layer or the BDOS, so they do
# not depend on the layer under test.  Legend: docs/run/D3.md.
#
# Everything it touches is named differently from the default build --
# its own object directory, cpm.sys, split module, build log and medium --
# so `make all' still produces byte-identical build/cpm.sys and
# build/cpmonly.bin whether or not this target has ever been run.  The
# recursion follows the verify-hash-ab pattern in tests/verify.mk: a
# PHONY name distinct from the file, so the sub-make sees an ordinary
# $(CPMSYS) rule instead of recursing on itself forever.
TRACEDIR = build/trace
TRACEOBJ = $(TRACEDIR)/obj
TRACESYS = $(TRACEDIR)/cpm.sys
TRACEMOD = $(TRACEDIR)/split.mod
TRACELOG = $(TRACEDIR)/build.log
TRACEBIN = build/cpmtrace.bin

.PHONY: trace-cpmsys cpmtrace
trace-cpmsys:
	@mkdir -p $(TRACEDIR)
	$(MAKE) OBJDIR=$(TRACEOBJ) CPMSYS=$(TRACESYS) SPLITMOD=$(TRACEMOD) \
		LOG=$(TRACELOG) DEFS='-DBOOT_TRACE' $(TRACESYS)

$(TRACEBIN): trace-cpmsys $(CPMARIMG) $(CPMBIMG) $(wildcard $(KBOOT)) \
		tools/mkcpmdisk.py tools/cohfs.py tools/sparse.py
	$(MKDISK) $@ $(TRACESYS) $(CPMARIMG) $(CPMBIMG)

cpmtrace: $(TRACEBIN)
	@echo "boot-trace medium: $(TRACEBIN) (markers: docs/run/D3.md)"

# ---- optional local medium (opt-in; nothing in `all' depends on it) --------
# make cpmlocal LOCALDIR=<a directory of extra files> LOCALOUT=<a path outside
# the checkout> writes a bootable medium that is the DEVELOPMENT drive A:
# (which carries the compatibility shims) plus every file in LOCALDIR.
#
# The extra files are the operator's own and are not this project's to
# redistribute, so neither they nor the medium may enter a checkout.  Their
# names appear nowhere here: LOCALDIR is the whole interface.  tools/
# mklocal.sh refuses an output path inside the checkout or its parent; that
# refusal is the licence boundary, not a build gate.  See README.md.
LOCALDIR ?=
LOCALOUT ?=
.PHONY: cpmlocal
cpmlocal: $(CPMSYS) $(CPMAIMG) $(CPMBIMG) tools/mklocal.sh tools/mkcpmfs.py \
		tools/mkcpmdisk.py tools/cohfs.py tools/sparse.py
	sh tools/mklocal.sh '$(LOCALDIR)' '$(LOCALOUT)' $(DISKA) $(CPMA_BLOCKS) \
		$(LABEL) $(LABELMODE) $(CPMSYS) $(CPMBIMG) '$(KBOOT)'
