# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# lout2cpm wraps a linked l.out as a 0xEE01 segmented CP/M x.out.  Compiled
# from the toolchain checkout, not copied here.
$(LOUT2CPM): $(LOUT2CPMSRC) | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -o $@ $(LOUT2CPMSRC)

define UCOMPILE
$(CC0) $(VAR) $< $(UOBJDIR)/$*.z0 -I$(<D) -Isrc/lib >> $(LOG) 2>&1
$(CC1) $(VAR) $(UOBJDIR)/$*.z0 $(UOBJDIR)/$*.z1 >> $(LOG) 2>&1
$(CC2) $(UVAR) $(UOBJDIR)/$*.z1 $@ $(UOBJDIR)/$*.scr 0 >> $(LOG) 2>&1
endef

$(UOBJDIR)/%.o: src/cmd/%.c src/lib/cpm.h $(TCSTAMP) | $(UOBJDIR)
	$(UCOMPILE)

$(UOBJDIR)/%.o: src/lib/%.c src/lib/cpm.h $(TCSTAMP) | $(UOBJDIR)
	$(UCOMPILE)

$(UOBJDIR)/%.o: src/shim/%.c src/lib/cpm.h $(TCSTAMP) | $(UOBJDIR)
	$(UCOMPILE)

$(UOBJDIR)/%.o: src/tests/%.c src/lib/cpm.h $(TCSTAMP) | $(UOBJDIR)
	$(UCOMPILE)

# DRI headers need 8.3 filename expansion, CP/M EOF removal and an extern for _base.
PIPINC = $(UOBJDIR)/pipinc
$(PIPINC)/basepage.h $(PIPINC)/bdos.h $(PIPINC)/portab.h $(PIPINC)/setjmp.h: \
		tools/stage-pipinc.sh $(ZBASE)/basepa.h $(ZBASE)/bdos.h \
		$(ZBASE)/portab.h $(ZBASE)/setjmp.h | $(UOBJDIR)
	sh tools/stage-pipinc.sh $(ZBASE) $(PIPINC)

$(UOBJDIR)/pip.o: src/cmd/pip.c $(PIPINC)/basepage.h $(PIPINC)/bdos.h \
		$(PIPINC)/portab.h $(PIPINC)/setjmp.h $(TCSTAMP) | $(UOBJDIR)
	$(CC0) $(VAR) $< $(UOBJDIR)/pip.z0 -I$(PIPINC) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(UOBJDIR)/pip.z0 $(UOBJDIR)/pip.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(UOBJDIR)/pip.z1 $@ $(UOBJDIR)/pip.scr 0 >> $(LOG) 2>&1

# PIP/STAT runtime shims are linked only into those programs.
PIPLIB = $(UOBJDIR)/pipmain.o $(UOBJDIR)/pipjmp.o

$(UOBJDIR)/pip.lout: $(UOBJDIR)/pip.o $(UOBJDIR)/crt0.o $(ULIB) $(PIPLIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o $(UOBJDIR)/pip.o \
		$(ULIB) $(PIPLIB)

# STAT shares PIP headers and runtime, plus copyrt.lit and the toolchain
# library's BSD-licensed qsort.
$(UOBJDIR)/stat.o: src/cmd/stat.c src/cmd/copyrt.lit $(PIPINC)/basepage.h \
		$(PIPINC)/bdos.h $(PIPINC)/portab.h $(PIPINC)/setjmp.h $(TCSTAMP) | $(UOBJDIR)
	$(CC0) $(VAR) $< $(UOBJDIR)/stat.z0 -I$(PIPINC) -Isrc/cmd >> $(LOG) 2>&1
	$(CC1) $(VAR) $(UOBJDIR)/stat.z0 $(UOBJDIR)/stat.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(UOBJDIR)/stat.z1 $@ $(UOBJDIR)/stat.scr 0 >> $(LOG) 2>&1

$(UOBJDIR)/stat.lout: $(UOBJDIR)/stat.o $(UOBJDIR)/crt0.o $(ULIB) $(PIPLIB) $(LIBCZ)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o $(UOBJDIR)/stat.o \
		$(ULIB) $(PIPLIB) $(LIBCZ)

$(UOBJDIR)/STAT.Z8K: $(UOBJDIR)/stat.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# ---- the src/app programs ----
# Written for DRI's CP/M C library, they link the toolchain's COHERENT
# stdio, string and malloc from its libc-z8001.a over src/lib/cpmsys.c,
# which puts open/read/write/lseek/sbrk/_exit on the BDOS and is also their
# startup (so no cstart.o).  Linked ahead of the archive, cpmsys.o's
# definitions keep the archive's system-call trap stubs out.
TCINC	= $(firstword $(wildcard $(C900_TOOLCHAIN)/src/include $(C900_TOOLCHAIN)/usr/include))
APPOBJ	= $(UOBJDIR)/app
APPINC	= -I$(APPOBJ) -I$(TCINC) -I$(TCINC)/sys

APPSDB	= CMD COM CRE ERR IEX INT IO JUNK MTH SCN SDB SEL SRT TBL

# SDB's sources include "sdbio.h"; the file is SDBIO.H.
$(APPOBJ)/sdbio.h: src/app/SDBIO.H | $(APPOBJ)
	cp $< $@

$(APPOBJ)/%.o: src/app/%.C $(APPOBJ)/sdbio.h $(TCSTAMP) | $(APPOBJ)
	$(CC0) $(VAR) $< $(APPOBJ)/$*.z0 $(APPINC) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(APPOBJ)/$*.z0 $(APPOBJ)/$*.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(APPOBJ)/$*.z1 $@ $(APPOBJ)/$*.scr 0 >> $(LOG) 2>&1

$(APPOBJ)/cpmsys.o: src/lib/cpmsys.c src/lib/cpm.h $(TCSTAMP) | $(APPOBJ)
	$(CC0) $(VAR) $< $(APPOBJ)/cpmsys.z0 -Isrc/lib -I$(TCINC) -I$(TCINC)/sys >> $(LOG) 2>&1
	$(CC1) $(VAR) $(APPOBJ)/cpmsys.z0 $(APPOBJ)/cpmsys.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(APPOBJ)/cpmsys.z1 $@ $(APPOBJ)/cpmsys.scr 0 >> $(LOG) 2>&1

# $(call APPLINK,PROG,OBJECTS)
define APPLINK
$(APPOBJ)/$(1).lout: $(2) $(APPOBJ)/cpmsys.o $(UOBJDIR)/crt0.o \
		$(UOBJDIR)/bdossc.o $(UOBJDIR)/libcpm.o $(LIBCZ)
	$$(LD) -e start -R $$(UBASE) -o $$@ $(UOBJDIR)/crt0.o \
		$$(filter-out %/crt0.o,$$^)

$(UOBJDIR)/$(1).Z8K: $(APPOBJ)/$(1).lout $$(LOUT2CPM)
	$$(LOUT2CPM) $$< $$@
endef
$(eval $(call APPLINK,SDB,$(APPSDB:%=$(APPOBJ)/%.o)))
$(foreach p,SORTFL KILLDU TOHEX FROMHEX,$(eval $(call APPLINK,$(p),$(APPOBJ)/$(p).o)))

# CPMSYST exercises cpmsys.c itself, linked as the programs above are.
$(APPOBJ)/cpmsyst.o: src/tests/cpmsyst.c $(TCSTAMP) | $(APPOBJ)
	$(CC0) $(VAR) $< $(APPOBJ)/cpmsyst.z0 -I$(TCINC) -I$(TCINC)/sys >> $(LOG) 2>&1
	$(CC1) $(VAR) $(APPOBJ)/cpmsyst.z0 $(APPOBJ)/cpmsyst.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(APPOBJ)/cpmsyst.z1 $@ $(APPOBJ)/cpmsyst.scr 0 >> $(LOG) 2>&1
$(eval $(call APPLINK,CPMSYST,$(APPOBJ)/cpmsyst.o))

$(APPOBJ):
	mkdir -p $(APPOBJ)

$(UOBJDIR)/%.o: src/cmd/%.s $(TCSTAMP) | $(UOBJDIR)
	cpp -traditional-cpp -P $< > $(UOBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(UOBJDIR)/$*.i >> $(LOG) 2>&1

$(UOBJDIR)/%.o: src/lib/%.s $(TCSTAMP) | $(UOBJDIR)
	cpp -traditional-cpp -P $< > $(UOBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(UOBJDIR)/$*.i >> $(LOG) 2>&1

$(UOBJDIR)/%.o: src/shim/%.s $(TCSTAMP) | $(UOBJDIR)
	cpp -traditional-cpp -P $< > $(UOBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(UOBJDIR)/$*.i >> $(LOG) 2>&1

# The UCASE variants include the shipped module's source.
$(UOBJDIR)/%.o: src/tests/%.s $(TCSTAMP) | $(UOBJDIR)
	cpp -traditional-cpp -P -Isrc/cmd $< > $(UOBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(UOBJDIR)/$*.i >> $(LOG) 2>&1

$(UOBJDIR)/%.lout: $(UOBJDIR)/%.o $(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o $< $(ULIB)

# Transient CCP: ccpcrt.o supplies entry and BDOS gate, so no crt0 or ULIB.
$(UOBJDIR)/%.o: src/ccp/%.c $(SYSHDRS) $(TCSTAMP) | $(UOBJDIR)
	$(CC0) $(VAR) $< $(UOBJDIR)/$*.z0 $(SYSINC) -DCCPTRANSIENT >> $(LOG) 2>&1
	$(CC1) $(VAR) $(UOBJDIR)/$*.z0 $(UOBJDIR)/$*.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(UOBJDIR)/$*.z1 $@ $(UOBJDIR)/$*.scr 0 >> $(LOG) 2>&1

$(UOBJDIR)/%.o: src/ccp/%.s $(TCSTAMP) | $(UOBJDIR)
	cpp -traditional-cpp -P $< > $(UOBJDIR)/$*.i 2>> $(LOG)
	$(AS) -g -o $@ $(UOBJDIR)/$*.i >> $(LOG) 2>&1

$(UOBJDIR)/ccp.lout: $(CCPOBJ)
	$(LD) -e start -R $(UBASE) -o $@ $(CCPOBJ)

$(UCCP): $(UOBJDIR)/ccp.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/MHELLO.Z8K: $(UOBJDIR)/mhello.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/FCOPY.Z8K: $(UOBJDIR)/fcopy.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/BEEP.Z8K: $(UOBJDIR)/beep.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CRSRDEMO.Z8K: $(UOBJDIR)/crsrdemo.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCOST.Z8K: $(UOBJDIR)/concost.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/BIOCOST.Z8K: $(UOBJDIR)/biocost.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CPUTCOST.Z8K: $(UOBJDIR)/cputcost.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONC.Z8K: $(UOBJDIR)/conc.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCB.Z8K: $(UOBJDIR)/concb.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCP.Z8K: $(UOBJDIR)/concp.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCQ.Z8K: $(UOBJDIR)/concq.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/XDOSM.Z8K: $(UOBJDIR)/xdosm.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/XDOSD.Z8K: $(UOBJDIR)/xdosd.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/XDOSE.Z8K: $(UOBJDIR)/xdose.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CON1.Z8K: $(UOBJDIR)/con1.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONN.Z8K: $(UOBJDIR)/conn.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CATT.Z8K: $(UOBJDIR)/catt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/SESSION.Z8K: $(UOBJDIR)/session.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/RXOV.Z8K: $(UOBJDIR)/rxov.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# Short packets limit buffer use; disable file-type scans and debug logs.
# Keep CRC support for block-check negotiation.
KFLAGS	= -DNO_LP -DNO_SCAN -DNODEBUG

$(UOBJDIR)/kermit.o: src/cmd/kermit.c src/cmd/kermit.h src/cmd/cdefs.h \
		src/cmd/debug.h $(TCSTAMP) | $(UOBJDIR)
	$(CC0) $(VAR) $< $(UOBJDIR)/kermit.z0 -Isrc/cmd $(KFLAGS) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(UOBJDIR)/kermit.z0 $(UOBJDIR)/kermit.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(UOBJDIR)/kermit.z1 $@ $(UOBJDIR)/kermit.scr 0 >> $(LOG) 2>&1

$(UOBJDIR)/cpmio.o: src/cmd/cpmio.c src/lib/cpm.h src/cmd/kermit.h \
		src/cmd/cdefs.h src/cmd/debug.h $(TCSTAMP) | $(UOBJDIR)
	$(CC0) $(VAR) $< $(UOBJDIR)/cpmio.z0 -Isrc/cmd -Isrc/lib $(KFLAGS) >> $(LOG) 2>&1
	$(CC1) $(VAR) $(UOBJDIR)/cpmio.z0 $(UOBJDIR)/cpmio.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(UOBJDIR)/cpmio.z1 $@ $(UOBJDIR)/cpmio.scr 0 >> $(LOG) 2>&1

$(UOBJDIR)/kermit.lout: $(UOBJDIR)/cpmio.o $(UOBJDIR)/kermit.o \
		$(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o \
		$(UOBJDIR)/cpmio.o $(UOBJDIR)/kermit.o $(ULIB)

$(UOBJDIR)/KERMIT.Z8K: $(UOBJDIR)/kermit.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCD.Z8K: $(UOBJDIR)/concd.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCE.Z8K: $(UOBJDIR)/conce.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCF.Z8K: $(UOBJDIR)/concf.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCG.Z8K: $(UOBJDIR)/concg.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCH.Z8K: $(UOBJDIR)/conch.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCI.Z8K: $(UOBJDIR)/conci.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCX.Z8K: $(UOBJDIR)/concx.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCY.Z8K: $(UOBJDIR)/concy.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCZ.Z8K: $(UOBJDIR)/concz.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/XDMA.Z8K: $(UOBJDIR)/xdma.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# F4: the second-split-I/D-program exerciser (verify-split).
$(UOBJDIR)/SPLITB.Z8K: $(UOBJDIR)/splitb.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# F4: the two creators that contend for one descriptor (verify-concr).
$(UOBJDIR)/CONCM.Z8K: $(UOBJDIR)/concm.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCO.Z8K: $(UOBJDIR)/conco.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# F14: the lock holder that is NOT also a creator, and the two creators
# plus ballast it arranges around it (verify-concr2).
$(UOBJDIR)/CONCL.Z8K: $(UOBJDIR)/concl.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCR.Z8K: $(UOBJDIR)/concr.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/XDOSPOL.Z8K: $(UOBJDIR)/xdospol.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# CONCS needs the assembly System-mode loop as well as its C driver.
$(UOBJDIR)/concs.lout: $(UOBJDIR)/concs.o $(UOBJDIR)/sysmode.o \
		       $(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o \
		$(UOBJDIR)/concs.o $(UOBJDIR)/sysmode.o $(ULIB)

$(UOBJDIR)/CONCS.Z8K: $(UOBJDIR)/concs.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCV.Z8K: $(UOBJDIR)/concv.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# The warm-boot segment-ownership trio (F5); src/tests/concw.c explains them.
$(UOBJDIR)/CONCW.Z8K: $(UOBJDIR)/concw.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCWB.Z8K: $(UOBJDIR)/concwb.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONCWC.Z8K: $(UOBJDIR)/concwc.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# The F1 directory-guard programs; src/tests/dgena.c explains them.
$(UOBJDIR)/DGENA.Z8K: $(UOBJDIR)/dgena.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/DGENB.Z8K: $(UOBJDIR)/dgenb.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/DERR.Z8K: $(UOBJDIR)/derr.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/DBOUND.Z8K: $(UOBJDIR)/dbound.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/MSCOPY.Z8K: $(UOBJDIR)/mscopy.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/ERRTEST.Z8K: $(UOBJDIR)/errtest.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CPM3FN.Z8K: $(UOBJDIR)/cpm3fn.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/DATE.Z8K: $(UOBJDIR)/date.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/SCBTEST.Z8K: $(UOBJDIR)/scbtest.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/V3FREE.Z8K: $(UOBJDIR)/v3free.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/RANEXT.Z8K: $(UOBJDIR)/ranext.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/STAMPT.Z8K: $(UOBJDIR)/stampt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/TRUNCT.Z8K: $(UOBJDIR)/trunct.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/WILDT.Z8K: $(UOBJDIR)/wildt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/PASST.Z8K: $(UOBJDIR)/passt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/ASTAMPT.Z8K: $(UOBJDIR)/astampt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/LBLNEW.Z8K: $(UOBJDIR)/lblnew.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/TRUNCB.Z8K: $(UOBJDIR)/truncb.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/TRUNCS.Z8K: $(UOBJDIR)/truncs.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/XFCBT.Z8K: $(UOBJDIR)/xfcbt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/U0T.Z8K: $(UOBJDIR)/u0t.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/ROT.Z8K: $(UOBJDIR)/rot.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/V3RET.Z8K: $(UOBJDIR)/v3ret.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/SDIR.Z8K: $(UOBJDIR)/sdir.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/SHOW.Z8K: $(UOBJDIR)/show.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/HELP.Z8K: $(UOBJDIR)/help.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/PAGET.Z8K: $(UOBJDIR)/paget.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/GET.Z8K: $(UOBJDIR)/get.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/PUT.Z8K: $(UOBJDIR)/put.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/RSXLDR.Z8K: $(UOBJDIR)/rsxldr.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/RSXT.Z8K: $(UOBJDIR)/rsxt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/RSXT2.Z8K: $(UOBJDIR)/rsxt2.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# CP/M-80 shim: host tests and target builds use the same engine sources,
# except the decoder -- the machine runs the assembly one and z80dec.c is
# the reference the host suite compiles.  ZDECT compares them, and is the
# only target program that links the C decoder: Z80.Z8K does not, which
# is why the mnemonic and condition helpers the executor calls are in
# z80mnem.c and not beside the decoder they came from.
Z80OBJ	= $(UOBJDIR)/z80.o $(UOBJDIR)/z80deca.o $(UOBJDIR)/z80mnem.o \
	  $(UOBJDIR)/z80exec.o \
	  $(UOBJDIR)/z80load.o $(UOBJDIR)/z80bdos.o $(UOBJDIR)/gdpb.o
$(Z80OBJ): src/shim/z80.h src/shim/gdpb.h src/shim/conmode.h

$(UOBJDIR)/z80.lout: $(Z80OBJ) $(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o $(Z80OBJ) $(ULIB)

$(UZ80): $(UOBJDIR)/z80.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# ZDECT links both decoders, so z80dec.c is compiled a second time under
# the name the comparison calls it by.
$(UOBJDIR)/z80decc.o: src/shim/z80dec.c src/shim/z80.h $(TCSTAMP) | $(UOBJDIR)
	$(CC0) $(VAR) $< $(UOBJDIR)/z80decc.z0 -Isrc/shim -Isrc/lib \
		-Dz80dec=z80decc >> $(LOG) 2>&1
	$(CC1) $(VAR) $(UOBJDIR)/z80decc.z0 $(UOBJDIR)/z80decc.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(UOBJDIR)/z80decc.z1 $@ $(UOBJDIR)/z80decc.scr 0 >> $(LOG) 2>&1

$(UOBJDIR)/zdect.o: src/tests/zdect.c src/shim/z80.h src/lib/cpm.h \
		$(TCSTAMP) | $(UOBJDIR)
	$(CC0) $(VAR) $< $(UOBJDIR)/zdect.z0 -Isrc/shim -Isrc/lib >> $(LOG) 2>&1
	$(CC1) $(VAR) $(UOBJDIR)/zdect.z0 $(UOBJDIR)/zdect.z1 >> $(LOG) 2>&1
	$(CC2) $(UVAR) $(UOBJDIR)/zdect.z1 $@ $(UOBJDIR)/zdect.scr 0 >> $(LOG) 2>&1

ZDECOBJ	= $(UOBJDIR)/zdect.o $(UOBJDIR)/z80deca.o $(UOBJDIR)/z80decc.o

$(UOBJDIR)/zdect.lout: $(ZDECOBJ) $(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o $(ZDECOBJ) $(ULIB)

$(UZDEC): $(UOBJDIR)/zdect.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# CP/M-86 shim: host tests and target builds use the same engine sources.
I86OBJ	= $(UOBJDIR)/i86.o $(UOBJDIR)/i86dec.o $(UOBJDIR)/i86exec.o \
	  $(UOBJDIR)/i86load.o $(UOBJDIR)/i86bdos.o $(UOBJDIR)/gdpb.o
$(I86OBJ): src/shim/i86.h src/shim/gdpb.h src/shim/conmode.h

$(UOBJDIR)/i86.lout: $(I86OBJ) $(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o $(I86OBJ) $(ULIB)

$(UI86): $(UOBJDIR)/i86.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# No crt0, no libc, no relocation, one section.  mkrsx.py fails the build
# if the link address and the `org' word in the module's prefix disagree.
$(UOBJDIR)/ucrsx.lout: $(UOBJDIR)/ucrsx.o
	$(LD) -L -e rsxbase -R $(RSXLINK) -o $@ $<

$(UOBJDIR)/UCASE.RSX: $(UOBJDIR)/ucrsx.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(RSXORG)

$(UOBJDIR)/ucrsxl.o: src/cmd/ucrsx.s

$(UOBJDIR)/ucrsxl.lout: $(UOBJDIR)/ucrsxl.o
	$(LD) -L -e rsxbase -R $(UCLLINK) -o $@ $<

$(UOBJDIR)/UCASEL.RSX: $(UOBJDIR)/ucrsxl.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(UCLORG)

$(UOBJDIR)/prsx.lout: $(UOBJDIR)/prsx.o
	$(LD) -L -e protbase -R $(PROTLINK) -o $@ $<

$(UOBJDIR)/PROT.RSX: $(UOBJDIR)/prsx.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(PROTORG)

$(UOBJDIR)/ucrsx3.o: src/cmd/ucrsx.s

$(UOBJDIR)/ucrsx3.lout: $(UOBJDIR)/ucrsx3.o
	$(LD) -L -e rsxbase -R $(UCLLINK) -o $@ $<

$(UOBJDIR)/UCASE3.RSX: $(UOBJDIR)/ucrsx3.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(UCLORG)

$(UOBJDIR)/ucrsxh.o: src/cmd/ucrsx.s

$(UOBJDIR)/ucrsxh.lout: $(UOBJDIR)/ucrsxh.o
	$(LD) -L -e rsxbase -R $(UCHLINK) -o $@ $<

$(UOBJDIR)/UCASEH.RSX: $(UOBJDIR)/ucrsxh.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(UCHORG)

$(UOBJDIR)/prsxn.o: src/tests/prsx.s

$(UOBJDIR)/prsxn.lout: $(UOBJDIR)/prsxn.o
	$(LD) -L -e protbase -R $(PROTLINK) -o $@ $<

$(UOBJDIR)/PROTN.RSX: $(UOBJDIR)/prsxn.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(PROTORG)

# Shipped RSX modules use the same flat-image link as the test modules.
$(UOBJDIR)/getrsx.lout: $(UOBJDIR)/getrsx.o
	$(LD) -L -e rsxbase -R $(GETLINK) -o $@ $<

$(UOBJDIR)/GET.RSX: $(UOBJDIR)/getrsx.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(GETORG)

$(UOBJDIR)/putrsx.lout: $(UOBJDIR)/putrsx.o
	$(LD) -L -e putbase -R $(PUTLINK) -o $@ $<

$(UOBJDIR)/PUT.RSX: $(UOBJDIR)/putrsx.lout tools/mkrsx.py
	python3 tools/mkrsx.py $< $@ $(PUTORG)

$(UOBJDIR)/GENCOM.Z8K: $(UOBJDIR)/gencom.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/SUBMIT.Z8K: $(UOBJDIR)/submit.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/SET.Z8K: $(UOBJDIR)/set.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/INITDIR.Z8K: $(UOBJDIR)/initdir.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/BIOSET.Z8K: $(UOBJDIR)/bioset.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# SCZERO (verify-ddtbrk): the C driver and its one-instruction SC #0.
$(UOBJDIR)/sczero.lout: $(UOBJDIR)/sczero.o $(UOBJDIR)/sczerosc.o \
			$(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o \
		$(UOBJDIR)/sczero.o $(UOBJDIR)/sczerosc.o $(ULIB)

$(UOBJDIR)/SCZERO.Z8K: $(UOBJDIR)/sczero.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

# SSTKT (verify-sstk): the C driver and its SC #1 copy.
$(UOBJDIR)/sstkt.lout: $(UOBJDIR)/sstkt.o $(UOBJDIR)/sstktsc.o \
			$(UOBJDIR)/crt0.o $(ULIB)
	$(LD) -e start -R $(UBASE) -o $@ $(UOBJDIR)/crt0.o \
		$(UOBJDIR)/sstkt.o $(UOBJDIR)/sstktsc.o $(ULIB)

$(UOBJDIR)/SSTKT.Z8K: $(UOBJDIR)/sstkt.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/CONBRK.Z8K: $(UOBJDIR)/conbrk.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR)/PIP.Z8K: $(UOBJDIR)/pip.lout $(LOUT2CPM)
	$(LOUT2CPM) $< $@

$(UOBJDIR):
	mkdir -p $(UOBJDIR)
