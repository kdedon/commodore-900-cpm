# Build CP/M-8000 system and disk images for the Commodore 900.

SHELL = /bin/sh
HOSTCC ?= cc

# Deletes a target left by a failed recipe.
.DELETE_ON_ERROR:

# Objects reached only through a rule chain (src/cmd/%.c -> build/user/%.o ->
# %.lout -> %.Z8K) are INTERMEDIATE, so make deletes them at the end of every
# build.  They are not recompiled on the next build -- a missing intermediate
# is not out of date while the .lout above it is current -- but they must all
# be recompiled as soon as any *link* input changes (crt0.o, the ULIB objects,
# lout2cpm), because relinking demands an object that is no longer there.
# .SECONDARY with no prerequisites keeps every such file; nothing else about
# how make decides to rebuild changes.
.SECONDARY:

# Resolve external inputs from explicit variables or tools/deps.sh.
C900_TOOLCHAIN := $(if $(C900_TOOLCHAIN),$(C900_TOOLCHAIN),$(shell sh tools/deps.sh toolchain))
TC = $(C900_TOOLCHAIN)/host/build

# Allow cleanup, dependency fetching and host checks without the cross toolchain.
TCFREE = clean unpublish dirfmt-check help \
TCNEED = $(filter-out $(TCFREE),$(or $(MAKECMDGOALS),all))
ifneq ($(strip $(TCNEED)),)
# Refusal text is deps.sh's own, on stderr; $(error) only stops the read.
ifneq ($(shell sh tools/deps.sh -n toolchain '$(C900_TOOLCHAIN)' 2>/dev/null || echo no),)
$(shell sh tools/deps.sh -n toolchain '$(C900_TOOLCHAIN)')
$(error no Z8001 toolchain -- see above)
endif
endif

CC0 = $(TC)/z8001/cc0-z8001
CC1 = $(TC)/z8001/cc1-z8001
CC2 = $(TC)/z8001/cc2-z8001
AS  = $(TC)/as-z8001
LD  = $(TC)/ld-z8001

VAR ?= 800000020800

# Header search order selects the BIOS or BDOS copy of biosdef.h/stdio.h.
# $(KBOOTINC) comes last: it supplies only <bootinfo.h>, which no tree here has.
SRCINC = -Isrc/bios -Isrc/bdos -I$(KBOOTINC)
SYSINC = -Isrc/bdos -Isrc/ccp -Isrc/bios -I$(OBJDIR)
DEFS =

# Release banner; generated into build/obj/cpmver.h.
CPMVER	= 3.1
# Build-host date in the target banner's MM/DD/YY format.
CPMDATE	:= $(shell date +%m/%d/%y)
COPYYEAR := $(shell date +%Y)

OBJDIR = build/obj
LOG    = build/build.log
# Compiler identity and binary digest; changes invalidate target objects.
TCSTAMP = build/toolchain.txt
CPMSYS = build/cpm.sys
CPMAIMG = build/cpma.img
# RELEASE = dev image without verify harness guest halves.
CPMARIMG = build/cpma-rel.img
# Blocks (512B); must match the cpma partition in hd42-cpm.media.
CPMA_BLOCKS = 20480
# Drive B occupies the disk tail; offsets are in 512-byte blocks.
CPMBIMG = build/cpmb.img
CPMB_BLOCKS = 16384
CPMB_BASEBLK = 59136

# The handoff block's layout is kboot's header, compiled here from the kboot
# checkout.  A copy of a struct layout drifts against the loader that fills it
# in, so the BIOS reads the original: $(KBOOTINC) is on its -I path and the
# header is a prerequisite of every object built from it.
KBOOTSRC := $(if $(KBOOTSRC),$(KBOOTSRC),$(shell sh tools/deps.sh kbootsrc))
KBOOTINC := $(KBOOTSRC)/include
BOOTINFOH := $(KBOOTINC)/bootinfo.h
# Refused at parse time, like the toolchain above and for the same reason: it
# is a COMPILE input, so a recipe-time check comes after the compile that needs
# it and the only symptom is a missing include buried in build/build.log.
ifneq ($(strip $(TCNEED)),)
ifneq ($(shell sh tools/deps.sh -n kbootsrc '$(KBOOTSRC)' 2>/dev/null || echo no),)
$(shell sh tools/deps.sh -n kbootsrc '$(KBOOTSRC)')
$(error no kboot checkout -- see above)
endif
endif

# Optional boot medium: kboot plus the system and release drive images.
KBOOT	:= $(if $(KBOOT),$(KBOOT),$(shell sh tools/deps.sh kboot))
CPMDISK	= build/cpmonly.bin
# Report missing kboot before invoking the disk builder.
KBOOTCHK = sh tools/deps.sh -n kboot '$(KBOOT)' || exit 1;
# Without kboot, all builds standalone images; boot tests require it.
CPMDISKALL = $(if $(wildcard $(KBOOT)),$(CPMDISK))
# Stock DRI utilities, staged verbatim.
ZBASE = vendor/z8001mb/cpm8k/packages/base

# Transient programs use TPA segment 0x32 for frame/auto references.
# Link crt0 first at offset zero, then wrap the l.out as a 0xEE01 image.
UVAR	= 001000000004
UBASE	= 0x32000000
UOBJDIR	= build/user
LOUT2CPMSRC = $(C900_TOOLCHAIN)/tools/lout2cpm/lout2cpm.c
LOUT2CPM = build/lout2cpm

ULIB	= $(UOBJDIR)/bdossc.o $(UOBJDIR)/biossc.o $(UOBJDIR)/cstart.o \
	  $(UOBJDIR)/libcpm.o
UPROGS	= $(UOBJDIR)/MHELLO.Z8K $(UOBJDIR)/FCOPY.Z8K $(UOBJDIR)/BEEP.Z8K \
	  $(UOBJDIR)/CRSRDEMO.Z8K $(UOBJDIR)/CONCOST.Z8K $(UOBJDIR)/BIOCOST.Z8K \
	  $(UOBJDIR)/CPUTCOST.Z8K $(UCONC) \
	  $(UPROGS3) $(UPROGSU) $(UPROGSR) $(UPIP) $(USTAT) $(UZ80) $(UI86) \
UKERMIT	= $(UOBJDIR)/KERMIT.Z8K
# Compatibility shims are built but excluded from the release disk.
UZ80	= $(UOBJDIR)/Z80.Z8K
UI86	= $(UOBJDIR)/CPM86.Z8K
# Concurrency exercisers, excluded from the release disk.
UCONC	= $(UOBJDIR)/CONC.Z8K $(UOBJDIR)/CONCB.Z8K \
	  $(UOBJDIR)/CONCP.Z8K $(UOBJDIR)/CONCQ.Z8K

# Additional concurrency programs are staged on separate test images.
UCONCFS	= $(UOBJDIR)/CONCD.Z8K $(UOBJDIR)/CONCE.Z8K \
	  $(UOBJDIR)/CONCF.Z8K $(UOBJDIR)/CONCG.Z8K \
	  $(UOBJDIR)/CONCH.Z8K $(UOBJDIR)/CONCI.Z8K \
	  $(UOBJDIR)/CONCX.Z8K $(UOBJDIR)/CONCY.Z8K

# XDOS exercisers.
UXDOS	= $(UOBJDIR)/XDOSM.Z8K $(UOBJDIR)/XDOSD.Z8K $(UOBJDIR)/XDOSE.Z8K

# Idle-console cost test; CONCY supplies the competing workload.
UCONCZ	= $(UOBJDIR)/CONCZ.Z8K $(UOBJDIR)/CONCY.Z8K

# Device-poll test and competing workload.
UXDOSPOL = $(UOBJDIR)/XDOSPOL.Z8K $(UOBJDIR)/CONCY.Z8K
# System-mode scheduling test and competing workload.
UCONCS	= $(UOBJDIR)/CONCS.Z8K $(UOBJDIR)/CONCY.Z8K
# HELP.HLP is staged from src/dist/disk-a beside HELP.Z8K.
UV5	= $(UOBJDIR)/HELP.Z8K $(UOBJDIR)/PAGET.Z8K
# GET and PUT load their .RSX modules at runtime.
UGP	= $(UOBJDIR)/GET.Z8K $(UOBJDIR)/PUT.Z8K \
	  $(UOBJDIR)/GET.RSX $(UOBJDIR)/PUT.RSX
# Scheduler quantum measurement.
UCONCV	= $(UOBJDIR)/CONCV.Z8K $(UOBJDIR)/CONCY.Z8K

# Console routing, sessions and ownership exercisers.
	  $(UOBJDIR)/RXOV.Z8K $(UOBJDIR)/CATT.Z8K
UPIP	= $(UOBJDIR)/PIP.Z8K
# PIP and STAT are rebuilt from src/cmd, not staged vendor binaries.
USTAT	= $(UOBJDIR)/STAT.Z8K
# CP/M 3 function exercisers and DATE.
UPROGS3	= $(UOBJDIR)/MSCOPY.Z8K $(UOBJDIR)/ERRTEST.Z8K \
	  $(UOBJDIR)/CPM3FN.Z8K $(UOBJDIR)/SCBTEST.Z8K $(UOBJDIR)/DATE.Z8K \
	  $(UOBJDIR)/STAMPT.Z8K $(UOBJDIR)/TRUNCT.Z8K \
	  $(UOBJDIR)/WILDT.Z8K $(UOBJDIR)/PASST.Z8K \
	  $(UOBJDIR)/ASTAMPT.Z8K $(UOBJDIR)/LBLNEW.Z8K \
	  $(UOBJDIR)/TRUNCB.Z8K $(UOBJDIR)/TRUNCS.Z8K $(UOBJDIR)/XFCBT.Z8K \
	  $(UOBJDIR)/V3RET.Z8K $(UOBJDIR)/V3FREE.Z8K
# CP/M 3 transient utilities: ordinary user programs.
UPROGSU	= $(UOBJDIR)/SDIR.Z8K $(UOBJDIR)/SHOW.Z8K $(UOBJDIR)/SUBMIT.Z8K \
	  $(UOBJDIR)/SET.Z8K $(UOBJDIR)/INITDIR.Z8K
# RSX modules use fixed TPA offsets; mkrsx.py checks the prefix against the link address.
UPROGSR	= $(UOBJDIR)/RSXLDR.Z8K $(UOBJDIR)/RSXT.Z8K $(UOBJDIR)/RSXT2.Z8K \
	  $(UOBJDIR)/GENCOM.Z8K
RSXORG	= 0xF000
RSXLINK	= $(shell printf '0x%x' $$(( $(UBASE) + $(RSXORG) )))
# Each attached module must fit below the existing RSX chain.
PROTORG	= 0xE800
PROTLINK = $(shell printf '0x%x' $$(( $(UBASE) + $(PROTORG) )))
UCLORG	= 0xE000
UCLLINK	= $(shell printf '0x%x' $$(( $(UBASE) + $(UCLORG) )))
# UCASE variants exercise chain order and available TPA space.
UCHORG	= 0xF700
UCHLINK	= $(shell printf '0x%x' $$(( $(UBASE) + $(UCHORG) )))
URSX	= $(UOBJDIR)/UCASE.RSX $(UOBJDIR)/PROT.RSX $(UOBJDIR)/UCASEL.RSX \
	  $(UOBJDIR)/UCASE3.RSX $(UOBJDIR)/PROTN.RSX $(UOBJDIR)/UCASEH.RSX
# PUT must attach before GET: fixed-address modules attach below the existing chain.
GETORG	= 0xE400
GETLINK	= $(shell printf '0x%x' $$(( $(UBASE) + $(GETORG) )))
PUTORG	= 0xF200
PUTLINK	= $(shell printf '0x%x' $$(( $(UBASE) + $(PUTORG) )))

SRCHDRS = $(wildcard src/bios/*.h) $(wildcard src/bdos/*.h) $(wildcard $(BOOTINFOH))
SYSHDRS = $(wildcard src/bdos/*.h) $(wildcard src/ccp/*.h) src/bios/c900cfg.h
# crt.o must link first: its 0x600 pad puts the WD command block/DMA
# buffer (phys 0x080000/0x080400) at text offset 0.
SRCOBJ = $(OBJDIR)/crt.o \
       $(patsubst src/bios/%.c,$(OBJDIR)/%.o,$(wildcard src/bios/*.c)) \
       $(filter-out $(OBJDIR)/crt.o,$(patsubst src/bios/%.s,$(OBJDIR)/%.o,$(wildcard src/bios/*.s)))
# The split-I/D slow path is linked separately and embedded as splitimg.o.
MODSRC = splitent splitscan splitsc zsplit
MODOBJ = $(patsubst %,$(OBJDIR)/%.o,$(MODSRC))
SPLITMOD = build/split.mod

# The CCP loads from A:CCP.Z8K into the TPA on cold and warm boots.
CCPSRC = ccp ccpext ccpgo
CCPOBJ = $(UOBJDIR)/ccpcrt.o $(patsubst %,$(UOBJDIR)/%.o,$(CCPSRC))
CCPXCL = $(OBJDIR)/ccpcrt.o $(patsubst %,$(OBJDIR)/%.o,$(CCPSRC))
UCCP   = $(UOBJDIR)/CCP.Z8K

# Exclude the transient CCP and split module from the resident link.
# Sort each object group to keep link order independent of source directory.
SYSOBJ = $(filter-out $(MODOBJ) $(CCPXCL), \
       $(sort $(patsubst src/bdos/%.c,$(OBJDIR)/%.o,$(wildcard src/bdos/*.c)) \
              $(patsubst src/ccp/%.c,$(OBJDIR)/%.o,$(wildcard src/ccp/*.c))) \
       $(sort $(patsubst src/bdos/%.s,$(OBJDIR)/%.o,$(wildcard src/bdos/*.s)) \
              $(patsubst src/ccp/%.s,$(OBJDIR)/%.o,$(wildcard src/ccp/*.s)))) \
       $(OBJDIR)/splitimg.o
OBJ  = $(SRCOBJ) $(SYSOBJ)
