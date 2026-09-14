# Emulator verification harness; paths are relative to the repository root.
# make verify-<name> runs one check; make verify-all runs the suite.
# verify-util asserts the development disk contents; verify-setb copies them.

# Runtime tests consume built emulator and kboot artifacts resolved by tools/deps.sh.
EMU	:= $(if $(EMU),$(EMU),$(shell sh tools/deps.sh emu))
# kboot and boot-medium settings are defined in mk/config.mk.
TESTIMG	= build/emutest.bin
EMUMAX	?= 600000000
# Stop on console idle or the CCP's unknown-command echo of ENDWORD.
# The trailing '?' distinguishes that response from the typed command echo.
ENDWORD	?= ZZEND
ENDMARK	?= $(ENDWORD)?
EMUIDLE	?= --stop-on=idle --stop-mark='$(ENDMARK)'
# Appended to a scripted --input to end the run at that point.  Used only
# where the script leaves the guest back at the CCP prompt: anywhere else
# the word would be swallowed by whatever is reading the console, the mark
# would not print, and the run would end on idle as before.
ENDIN	= $(ENDWORD)\r

# A CP/M-only disk has one boot entry, so scripted input needs no menu selection.
OSSEL	=
# POSIX sh reports tee's status for a pipeline. Save the emulator status
# inside the group, then check it with EMUOK after tee drains the transcript.
# EMUCD validates the dependency before changing directories.
EMUCD = sh tools/deps.sh -n emu '$(EMU)' && cd $(EMU)/bin
EMUSTATUS = build/emu.status
EMUSTAT = echo $$? > $(abspath $(EMUSTATUS))
EMUOK = test "`cat $(EMUSTATUS)`" = 0 \
	|| { echo "*** the emulator exited `cat $(EMUSTATUS)`: what is above is not a session"; exit 1; }

# verify-boot: does this medium still boot CP/M?  Nothing else in the suite
# asks that question directly -- every other target boots in order to test
# something else, so a loader or boot-partition regression surfaced as
# whichever of them ran first, failing on a transcript with no CP/M in it.
# tests/bootgate.sh walks ROM -> kboot -> cpm.sys -> BDOS -> CCP and names the
# first stage that did not happen, saying plainly when the LOADER is the
# suspect.  The session itself is trivial on purpose (one DIR): what is under
# test is the boot chain, not the command.
BOOTLOG	= build/verify-boot.log
BOOTIN	= $(OSSEL)DIR *.TXT\r$(ENDIN)
.PHONY: verify-boot verify-bootgate
verify-boot: all $(CPMDISK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CPMDISK)) \
		--input="$(BOOTIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(BOOTLOG))
	@$(EMUOK)
	@sh tests/bootgate.sh $(BOOTLOG) \
		|| { echo "verify-boot: FAIL -- $(CPMDISK) no longer boots CP/M."; \
		     echo "             The reason is above; if it names the loader,"; \
		     echo "             suspect kboot or the boot partition BEFORE you"; \
		     echo "             suspect whatever else is failing today."; exit 1; }
	@echo "verify-boot: PASS -- $(CPMDISK) boots ROM -> kboot -> cpm.sys -> A>"

# Prove verify-boot can fail: three deliberately broken media, each rejected
# and shown to fail; a mutation that changes nothing looks like a check that cannot fail.
# It depends on verify-boot so both directions are shown in one run: the good
# medium passes, all three broken ones are caught.
#
# These runs REDIRECT rather than pipe (so make sees the status directly, and
# tests/pipecheck.sh has nothing to wrap) and their status is deliberately
# ignored: a medium that never boots leaves the guest halted with a live timer
# interrupt, which does not advance the instruction counter, so --max is never
# reached and only a wall-clock timeout ends the run.  Nothing is inferred
# from the status -- the verdict comes from the transcript, and an emulator
# that never started leaves an empty one, which the gate's first check
# rejects.
MUTTIMEOUT ?= 60
verify-bootgate: verify-boot
	for m in cfg kernel base; do \
		$(MKDISK) --mutate=$$m build/mut-$$m.bin $(CPMSYS) $(CPMAIMG) \
			$(CPMBIMG) || exit 1; \
		( $(EMUCD) && timeout $(MUTTIMEOUT) ./c900 \
			--disk=$(abspath build/mut-$$m.bin) --input="$(BOOTIN)" \
			--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null </dev/null ) \
			> $(abspath build/mut-$$m.log); \
		echo "--- mutant $$m: emulator exited $$? (ignored, see above)"; \
		grep -q 'CPM-Z8000 Version' build/mut-$$m.log && { \
			echo "verify-bootgate: FAIL -- mutant $$m STILL BOOTED:"; \
			echo "  the mutation changed nothing, so it proves nothing"; \
			exit 1; }; \
		sh tests/bootgate.sh build/mut-$$m.log > build/mut-$$m.gate 2>&1 \
			&& { echo "verify-bootgate: FAIL -- the gate PASSED a medium"; \
			     echo "  that never reached CP/M (mutant $$m).  It is vacuous."; \
			     exit 1; }; \
		cat build/mut-$$m.gate; \
		case $$m in \
		cfg)	want='COULD NOT READ kboot.cfg';; \
		kernel)	want='REPORTED AN ERROR';; \
		base)	want='REPORTED AN ERROR';; \
		esac; \
		grep -q "$$want" build/mut-$$m.gate || { \
			echo "verify-bootgate: FAIL -- mutant $$m was caught, but not"; \
			echo "  by the check meant to catch it (wanted: $$want)"; \
			exit 1; }; \
		grep -q 'LOADER' build/mut-$$m.gate || { \
			echo "verify-bootgate: FAIL -- mutant $$m broke the LOADER and"; \
			echo "  the message does not say so"; exit 1; }; \
	done
	@echo "verify-bootgate: PASS -- the good medium boots, and a corrupt"
	@echo "                 kboot.cfg, an unfindable cpm.sys and a wrong"
	@echo "                 partition base are each caught, as loader faults"
# MWC transients (load/args/rerun, file I/O + error path), the speaker
# driver (BEEP: the emulator's CIO #2 is an inert register file, so this
# proves the BEL path runs, not that it sounds), stock DRI regression
# (STAT, PIP, DDT -- DDT last: its `-' prompt does not latch scripted
# input, so the run then coasts to EMUMAX).
VERIFYIN = $(OSSEL)DIR M*.*\rMHELLO ALPHA BETA-1\rMHELLO\rBEEP 2\rFCOPY HELLO.TXT COPY2.TXT\rTYPE COPY2.TXT\rFCOPY NOPE.TXT X.TXT\rDIR *.TXT\rSTAT COPY2.TXT\rPIP OUT2.TXT=COPY2.TXT\rTYPE OUT2.TXT\rDDT MHELLO.Z8K\r
# Second cold boot of the SAME image (no re-patch): the files written by
# the first session must still be there.
REVERIFYIN = $(OSSEL)DIR *.TXT\rTYPE COPY2.TXT\rMHELLO AGAIN\r$(ENDIN)
# ED interactive session (verify-ed).  ED's '*' prompt is not a gate
# character, so everything after the ED command line runs gate-off (\g),
# paced on the guest's blocked-reading RR0 poll streak (emulator commit
# 0d115a0).  The string is a printf(1) format: \r = CR, \\g survives as
# the gate-off marker, \032 = ^Z (ends ED insert mode), \043 = '#'
# (make would treat a literal one as a comment).  Session: create
# TEST.TXT with three lines, e(xit), TYPE it, re-enter, \043a = #a
# (append all source lines), -b (bottom), insert one more, e, TYPE.
# Split-tool execution session (verify-arx): assemble two staged .8KN
# sources with ASZ8K (Unidot .OBJ out), convert both with XCON to x.out,
# structure-dump one with XDUMP, archive both with AR8K, list, copy the
# original member aside, erase it, extract it back from the archive.
# All four tools are 0xEE0B split-I/D binaries running through the pgmld
# shim.  Byte-identity of the extracted member is checked host-side
# after the run (mkcpmfs.py --extract on the cpma partition).
ARXVERIFYIN = $(OSSEL)ASZ8K MINI.8KN\rASZ8K STARTUP.8KN\rDIR *.OBJ\rXCON -o MINI.O MINI.OBJ\rXCON -o START2.O STARTUP.OBJ\rXDUMP MINI.O\rAR8K rv TEST.A MINI.O START2.O\rAR8K tv TEST.A\rPIP MINIORIG.O=MINI.O\rERA MINI.O\rAR8K xv TEST.A MINI.O\rDIR *.O\r$(ENDIN)
# cpma partition base in 512-byte blocks on the dist disk (hd42-cpm.media).
CPMA_BASEBLK = 38144
# The split tools run through the SC-trap shim (~3-6x native), so the
# verify-arx session needs a bigger instruction budget than EMUMAX.
ARXMAX ?= 2000000000
# TRUNCB writes 1160 records and reads 769 back across four files, which
# does not fit the default budget.
TRUNCBMAX ?= 1500000000

# ---- CP/M 3 V1 wave function tests (44, 45, 42/43/98/107-112, fn 10) ----
# One program per cold boot, then one scripted editing session, all onto a
# fresh patched copy.  MSCOPY's copy is checked byte-for-byte host-side
# after the run by pulling the cpma partition back out, which is the only
# way to prove a 32-records-per-call copy landed intact.  The editing
# session drives the CCP's own function-10 line: each line is mistyped and
# then repaired with the cursor keys, so a command only runs if the editor
# put the buffer right (^A ^H, ^B ^F insert, ^W recall, ^K).
V1IMG	= build/v1test.bin
V1LOG	= build/verify-v1.log
.PHONY: verify-v1
verify-v1: all
	$(MKDISK) $(V1IMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	: > $(V1LOG)
	for c in 'MSCOPY BIG.TXT MSV1.TXT 32' 'ERRTEST' 'CPM3FN'; do \
		{ $(EMUCD) && ./c900 --disk=$(abspath $(V1IMG)) \
			--input="$(OSSEL)$$c\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
			$(EMUSTAT); } \
		| tee -a $(abspath $(V1LOG)); $(EMUOK); done
	{ $(EMUCD) && ./c900 --disk=$(abspath $(V1IMG)) \
		--input="$$(printf '$(V1EDITFMT)')" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee -a $(abspath $(V1LOG))
	@$(EMUOK)
	@grep -q 'copied 160 records in 6 calls' $(V1LOG) || { echo "verify-v1: FAIL -- fn 44 record/call count"; exit 1; }
	@grep -q 'MSCOPY: sums match' $(V1LOG) || { echo "verify-v1: FAIL -- multi-sector copy differs"; exit 1; }
	@grep -q 'MSCOPY: record 4 matches' $(V1LOG) || { echo "verify-v1: FAIL -- random multi-sector read"; exit 1; }
	@test "`grep -c '(error returned)' $(V1LOG)`" = 6 || { echo "verify-v1: FAIL -- fn 45 did not return all six errors"; exit 1; }
	@grep -q 'ERRTEST: normal file I/O still works' $(V1LOG) || { echo "verify-v1: FAIL -- BDOS unusable after error mode"; exit 1; }
	@grep -q 'CPM3FN: fn 98 freed the block' $(V1LOG) || { echo "verify-v1: FAIL -- fn 98"; exit 1; }
	@grep -q 'fn 107 serial  -> C90001' $(V1LOG) || { echo "verify-v1: FAIL -- fn 107"; exit 1; }
	@test "`grep -c 'MHELLO   Z8K'  $(V1LOG)`" = 3 || { echo "verify-v1: FAIL -- an edited command line did not run"; exit 1; }
	@grep -q 'A>TYPE HELLO.TXTJUNK' $(V1LOG) || { echo "verify-v1: FAIL -- fn 10 ^K session missing"; exit 1; }
# `conv=sparse' on every extraction here, and on the twenty-three like it
# below.  A drive-A: partition pulled out of a medium is 10 MB of which a
# packed disk uses about one, and each verify target extracts its own; dense,
# one sweep left 585 MB of them.  dd seeks over the zero blocks instead of
# writing them, so the file's LENGTH and CONTENT are unchanged -- a hole reads
# back as the zeros that were there -- and the `cmp' and --extract steps that
# consume these files cannot tell.  Only `du' can, which is the point.
# It is a GNU coreutils flag; so is most of what this Makefile assumes.
# NOT on the id-b-damaged.img patch further down: that one writes a single
# non-zero byte with conv=notrunc, where sparse means nothing and truncating
# would destroy the image it is editing.
	dd if=$(V1IMG) of=build/v1-cpma.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/v1-fs
	python3 tools/mkcpmfs.py --extract build/v1-cpma.img build/v1-fs
	@cmp build/v1-fs/BIG.TXT build/v1-fs/MSV1.TXT \
		&& echo "verify-v1: PASS -- 32-records-per-call copy byte-identical" \
		|| { echo "verify-v1: FAIL -- multi-sector copy differs on disk"; exit 1; }

# ---- console flow control: ^S/^Q/^C via conbrk() (verify-conbrk) ----
# Nothing else in this suite sends a control character to a RUNNING
# program's console output -- every other scripted session either types
# a command line or edits one at the CCP's function-10 prompt.  Widening
# the keyboard poll to once every eight characters
# changed exactly the code this exercises (src/bdos/conbdos.c conbrk())
# and said outright that this test was owed.
#
# CONBRK.Z8K (src/cmd/conbrk.c) is the guest half: `CONBRK P nnnn' prints
# nnnn "NNNN " tokens one character at a time through BDOS function 2 --
# conbrk()'s own path -- after a preamble that forces conbrk()'s poll
# counter to a known zero, so the first poll inside the measured loop is
# exactly its 8th character (CONBRK_POLL, conbdos.c) and not some
#
# until the emulator grew a primitive for it, and an earlier revision had to strip
#
#
#               the widening could have introduced silently: the widened
CBRKTOKENS = 0200
.PHONY: verify-conbrk
verify-conbrk: all
	@$(EMUOK)

# ---- split-I/D shim offline validation (dev instruments; the on-target
# scanner (src/bdos/zsplit.c, run by pgmld at load time) is what actually
# ships and is authoritative -- these host tools exist to validate it, not
# to replace it) ----
# splittest: host unit tests of the SC-trap emulator, one per form.
# splitcheck: scan every 0xEE0B binary on A:; cross-check decode lengths,
# reachability and symbol boundaries; nonzero exit on any discrepancy.
# The binary list is built in the RECIPE, not at parse time: a `$(wildcard)'
# over $(DISKA) is expanded before `all' has staged drive A:, so on a clean
# checkout splitcheck ran the scanner over an empty argument list and exited
# 0 having examined nothing.  An empty list is now the error it always was.
.PHONY: splitcheck splittest
build/splitchk: tests/splitchk.c src/bdos/zsplit.c src/bdos/zsplit.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -Wall -DHOSTCC -o $@ tests/splitchk.c src/bdos/zsplit.c
build/splittest: tests/splittest.c src/bdos/splitsc.c src/bdos/zsplit.c src/bdos/zsplit.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -DHOSTCC -o $@ tests/splittest.c src/bdos/splitsc.c src/bdos/zsplit.c
splittest: build/splittest
	build/splittest
splitcheck: build/splitchk
	@bins=`ls $(DISKA)/*.Z8K 2>/dev/null`; \
	if [ -z "$$bins" ]; then \
		echo "splitcheck: no .Z8K binaries in $(DISKA) -- run \`make all' first"; \
		echo "splitcheck: refusing to report success over an empty set"; \
		exit 1; \
	fi; \
	echo "splitcheck: scanning `echo $$bins | wc -w` binaries"; \
	build/splitchk $$bins

# THE TRIPLE ONCE CAUGHT A REAL ONE, and it is worth knowing what.  V5
# made the SCB's page$mode byte a live mirror of the BDOS's page mode
# (src/bdos/scb.c) and first shipped that mode OFF -- and in CP/M 3 "off"
# is 0FFh while 0 means ON.  DUMP.COM reads page$mode
# (ref/cpm3/dump.asm:251,374-380,429) and takes a shorter path when paging
# is off, so the target run fell to 14,240/604/864 against the host's
# unchanged 14,314/605/872 and this target failed.  It was right to: the
# same byte had silently stopped SET pausing (`make verify-setb', session
# 3).  The system ships page$mode = PM_ON again and gates its own pager on
# @CONPAGE instead (src/bdos/bdosmisc.c), so the numbers below are v3's
# environment on both machines.  A future divergence here is the same
# question: what did the two sides disagree about?
#
# CP/M 3 directory format (BDOS, mkcpmfs.py, cpm(1)) must stay byte-compatible.
# cpm(1) stays a Coherent source, not vendored: drifting oracle becomes useless.
COHERENT_OS := $(if $(COHERENT_OS),$(COHERENT_OS),$(shell sh tools/deps.sh userland))
.PHONY: dirfmt-check
# $(wildcard $(CPMCMD)), not $(CPMCMD): an absent source must be reported by
# the recipe below, which says what it is for, rather than by make as a
# missing prerequisite with no rule to make it.
build/cpmhost: $(wildcard $(CPMCMD)) | $(OBJDIR)
	@sh tools/deps.sh -n userland '$(COHERENT_OS)' || exit 1
	$(HOSTCC) -std=gnu89 -w -o $@ $(CPMCMD)
dirfmt-check: build/cpmhost
	python3 tests/dirfmt-test.py build/dirfmt build/cpmhost

# The proof that matters for sequencing: a STAMPED drive A: booted by the
# CURRENT, unmodified CP/M.  The BDOS does not maintain stamps yet -- what
# this shows is that it does not break them and does not misreport space.
# DIR/TYPE/STAT/PIP run, PIP and the CCP create files, then the partition is
# pulled back out and the extensions must still be there, intact.
STAMPIMG = build/stamptest.bin
STAMPCPMA = build/cpma-stamped.img
STAMPVERIFYIN = $(OSSEL)DIR\rSTAT\rSTAT *.*\rTYPE HELLO.C\rPIP STAMP1.TXT=HELLO.C\rTYPE STAMP1.TXT\rDIR *.TXT\rSTAT\r$(ENDIN)
.PHONY: verify-stamped
verify-stamped: all build/cpmhost
	cp $(CPMAIMG) $(STAMPCPMA)
	python3 tools/mkcpmfs.py --initdir --label C900A \
		--label-mode create,update --stamp-date 2026-07-30T12:00 $(STAMPCPMA)
	$(MKDISK) $(STAMPIMG) $(CPMSYS) $(STAMPCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(STAMPIMG)) \
		--input="$(STAMPVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath build/verify-stamped.log)
	@$(EMUOK)
	dd if=$(STAMPIMG) of=build/stamp-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --list build/stamp-cpma.img \
		> build/stamp-after.txt; cat build/stamp-after.txt
	@grep -q '128 SFCB entries' build/stamp-after.txt \
		|| { echo "verify-stamped: FAIL -- SFCBs damaged by the stock BDOS"; exit 1; }
	@grep -q 'label     C900A' build/stamp-after.txt \
		|| { echo "verify-stamped: FAIL -- directory label damaged"; exit 1; }
	@grep -q 'STAMP1.TXT' build/stamp-after.txt \
		|| { echo "verify-stamped: FAIL -- PIP did not create its file"; exit 1; }
	@build/cpmhost -f build/stamp-cpma.img ls > build/stamp-after-cpm.txt
	@grep -q '128 SFCB entries' build/stamp-after-cpm.txt \
		|| { echo "verify-stamped: FAIL -- cpm(1) disagrees on the SFCBs"; exit 1; }
	@echo "verify-stamped: PASS -- stock BDOS ran on a stamped drive, extensions intact"

# ---- drive B: (a second drive letter) ----
# Two cold boots on one patched image, then the disk is taken apart
# host-side.  What has to be proved is not that B: works -- a SELDSK that
# handed B: drive A:'s dph would look like it works -- but that B: is
# somewhere ELSE.  So the runtime half only creates evidence (a file on
# each drive, and the geometry each drive reports), and the judging is
# done on the image: tests/driveb-check.py finds each new directory name
# in the 42 MB image and requires it to be inside its own drive's region
# and nowhere else.  Aliasing fails that on both halves at once.
#
# Boot 1 is the session; boot 2 runs ERRTEST, whose invalid-drive case is
# the other half of the drive table -- P: (drive 15) must still be
# refused, and refused as a returned error rather than a dead machine.
DBIMG	= build/dbtest.bin
DBLOG	= build/verify-driveb.log
DBERRLOG = build/verify-driveb-err.log
# A: does the writing so PIP is reachable, then the current drive is
# moved to B: and a write is done from there too (A:PIP: the CCP finds
# the command on A: while B: is the drive it acts on).  The DIR pairs are
# the visibility cross-check: each new file on its own drive, "No file"
# on the other.
DBVERIFYIN = $(OSSEL)SHOW A:[DRIVE]\rSHOW B:[DRIVE]\rPIP B:FROMA.TXT=HELLO.C\rPIP AONLY.TXT=HELLO.C\rDIR B:AONLY.TXT\rDIR AONLY.TXT\rDIR FROMA.TXT\rDIR B:FROMA.TXT\rB:\rA:PIP BNEW.TXT=BONLY.TXT\rDIR\rDIR A:BNEW.TXT\rA:\rDIR B:\r$(ENDIN)
.PHONY: verify-driveb
verify-driveb: all
	$(MKDISK) $(DBIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DBIMG)) \
		--input="$(DBVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(DBLOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DBIMG)) \
		--input="$(OSSEL)ERRTEST\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(DBERRLOG))
	@$(EMUOK)
	@# --- the drive table, as the two drives describe themselves ---
	@# A: MUST still be the drive it was at HEAD: same capacity, same
	@grep -q '81,920: 128 Byte Record Capacity' $(DBLOG) \
		|| { echo "verify-driveb: FAIL -- A: is no longer 10 MB"; exit 1; }
	@grep -q '65,536: 128 Byte Record Capacity' $(DBLOG) \
	@# --- cross-visibility, from the running system ---
	@grep -q 'B: BONLY    TXT' $(DBLOG) \
		|| { echo "verify-driveb: FAIL -- the packed B: image is not readable"; exit 1; }
	@# verify-util's SDIR [SHORT] check.)
	@grep -q 'A: AONLY    TXT' $(DBLOG) \
		|| { echo "verify-driveb: FAIL -- A: did not get its own file"; exit 1; }
	@test "`grep -c 'No file' $(DBLOG)`" = 3 \
		|| { echo "verify-driveb: FAIL -- a file was visible on the drive it was NOT written to (or a DIR failed for another reason)"; exit 1; }
	@# --- the drive that is not there ---
	@grep -q 'open on P:: low=255 code=4 (error returned)' $(DBERRLOG) \
		|| { echo "verify-driveb: FAIL -- P: was not refused with the invalid-drive code"; exit 1; }
	@test "`grep -c 'open on P:: low=255 code=4' $(DBERRLOG)`" = 2 \
		|| { echo "verify-driveb: FAIL -- P: not refused in both error modes"; exit 1; }
	@grep -q 'ERRTEST: normal file I/O still works' $(DBERRLOG) \
		|| { echo "verify-driveb: FAIL -- the system did not survive the select error"; exit 1; }
	@# --- where the bytes actually are ---
	python3 tests/driveb-check.py $(DBIMG) $(CPMA_BASEBLK) $(CPMA_BLOCKS) \
		$(CPMB_BASEBLK) $(CPMB_BLOCKS) $(CPMBIMG)
	@# --- and that the copy is a copy: B:FROMA.TXT vs A:HELLO.C ---
	dd if=$(DBIMG) of=build/db-cpma.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	dd if=$(DBIMG) of=build/db-cpmb.img bs=512 skip=$(CPMB_BASEBLK) \
		count=$(CPMB_BLOCKS) status=none conv=sparse
	rm -rf build/db-fs-a build/db-fs-b
	python3 tools/mkcpmfs.py --extract build/db-cpma.img build/db-fs-a
	python3 tools/mkcpmfs.py --extract build/db-cpmb.img build/db-fs-b
	@cmp build/db-fs-b/FROMA.TXT build/db-fs-a/HELLO.C \
		|| { echo "verify-driveb: FAIL -- the file PIP wrote to B: is not the file it read from A:"; exit 1; }
	@test ! -f build/db-fs-a/BNEW.TXT -a ! -f build/db-fs-a/FROMA.TXT \
		|| { echo "verify-driveb: FAIL -- a B: file is in A:'s region on disk"; exit 1; }
	@test ! -f build/db-fs-b/AONLY.TXT \
		|| { echo "verify-driveb: FAIL -- an A: file is in B:'s region on disk"; exit 1; }
	@echo "verify-driveb: PASS -- B: is its own region (block $(CPMB_BASEBLK)), A: unchanged, P: refused"

BPVERIFYIN = $(OSSEL)SHOW B:[DRIVE]\rPIP AONLY.TXT=HELLO.C\rB:\rA:PIP BNEW.TXT=BONLY.TXT\rDIR\rA:\r$(ENDIN)
BFVERIFYIN = $(OSSEL)SHOW B:[DRIVE]\rPIP B:FROMA.TXT=HELLO.C\rDIR B:\r$(ENDIN)
		--input="$(OSSEL)DIR B:\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
# ---- INITDIR + BDOS function 50 (verify-initdir) ----
# INITDIR rewrites a LIVE directory: a partial or wrong run destroys
# files, so almost none of this target is a transcript grep.  The three
# cold boots create evidence; tests/initdir-check.py judges it.
# Every check is demonstrated red (--red) against other drives/states,
# because a check that cannot fail cannot be merged.
#
# The fixture is B:'s own content plus five files, packed into a
# separate staging directory: src/dist/disk-b/ is left alone because
# verify-driveb and verify-setb grep its DIR output verbatim.  It is
# arranged so the run has real work to do -- with seven files the
# directory label lands in entry 7 and a FILE lands in entry 3, both of
# which are fourth slots, so the run must relocate one ordinary FCB and
# one CP/M 3 extension entry.  A packed B: with only its two shipped
# files leaves every fourth slot free and relocates nothing, which is
# the same shape as section 1's "fixture arranged wrong" case.  An XFCB
# in entry 8 (a non-fourth slot) is the third kind of entry and must
# come through untouched.
IDIMG	= build/idtest.bin
IDLOG	= build/verify-initdir.log
IDLOG2	= build/verify-initdir-2.log
IDLOG3	= build/verify-initdir-3.log
IDSTAGE	= build/initdir-b
IDCPMB	= build/initdir-cpmb.img
IDORACLE = build/initdir-oracle.img
# Boot 1.  DIR/TYPE on B: BEFORE the run is not decoration: it logs the
# drive in, so the BDOS is holding an allocation vector, directory hash
# signatures and a directory-buffer record number for the layout INITDIR
# is about to change.  The same TYPE after the run is what proves the
# cache invalidation (INITDIR.PLI:987-993) happened -- ID3.TXT moves
# from entry 3 to entry 9, and a BDOS still trusting its old signatures
# reports it missing on a disk where it is present.
# \g turns the input gate off after INITDIR's own CR, because INITDIR's
# "(Y/N)?  " prompt is not a character the gate latches on.
# Boot 2: the two refusals.  With no drive on the command line INITDIR
# asks (INITDIR.PLI:288-321) instead of guessing -- answering that
# prompt needs the gate off too.  B: is now formatted, so the second run
# must stop at "Directory already re-formatted."; A: is then answered N,
# which must leave it alone.
.PHONY: verify-initdir
verify-initdir: all
	@# ---- the fixture, and the independent oracle for its result ----
	rm -rf $(IDSTAGE)
	mkdir -p $(IDSTAGE)
	cp src/dist/disk-b/* $(IDSTAGE)/
	@for n in 1 2 3 4 5; do \
		printf 'INITDIR fixture file %s on drive B:\r\n' $$n \
			> $(IDSTAGE)/ID$$n.TXT; done
	python3 tools/mkcpmfs.py --label $(LABELB) --label-mode create,update \
		--xfcb ID4.TXT $(IDCPMB) $(CPMB_BLOCKS) $(IDSTAGE)
	cp $(IDCPMB) $(IDORACLE)
	python3 tools/mkcpmfs.py --initdir $(IDORACLE)
	$(MKDISK) $(IDIMG) $(CPMSYS) $(CPMAIMG) $(IDCPMB)
	@# ---- the BEFORE state, decoded off the disk that will be booted ----
	dd if=$(IDIMG) of=build/id-a-before.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	dd if=$(IDIMG) of=build/id-b-before.img bs=512 skip=$(CPMB_BASEBLK) \
		count=$(CPMB_BLOCKS) status=none conv=sparse
	@python3 tests/initdir-check.py unstamped build/id-b-before.img
	@python3 tests/initdir-check.py at build/id-b-before.img 3 0x00:ID3.TXT
	@python3 tests/initdir-check.py at build/id-b-before.img 7 0x20:C900B
	@python3 tests/initdir-check.py at build/id-b-before.img 8 0x10:ID4.TXT
	@# drive identity, before a single instruction runs: A: ships stamped,
	@# so the very check B: passes must go red on A:
	@python3 tests/initdir-check.py --red unstamped build/id-a-before.img
	@# ---- boot 1: the run ----
	{ $(EMUCD) && ./c900 --disk=$(abspath $(IDIMG)) \
		--input="$$(printf '$(IDIN1FMT)')" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(IDLOG))
	@$(EMUOK)
	@# ---- boot 2: the two refusals ----
	{ $(EMUCD) && ./c900 --disk=$(abspath $(IDIMG)) \
		--input="$$(printf '$(IDIN2FMT)')" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(IDLOG2))
	@$(EMUOK)
	@# ---- boot 3: the function 50 contract, both halves ----
	{ $(EMUCD) && ./c900 --disk=$(abspath $(IDIMG)) \
		--input="$(OSSEL)BIOSET\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(IDLOG3))
	@$(EMUOK)
	@# ---- the AFTER state, off the same disk ----
	dd if=$(IDIMG) of=build/id-a-after.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	dd if=$(IDIMG) of=build/id-b-after.img bs=512 skip=$(CPMB_BASEBLK) \
		count=$(CPMB_BLOCKS) status=none conv=sparse
	@# every fourth slot is now an SFCB, and was not before
	@python3 tests/initdir-check.py stamped build/id-b-after.img
	@python3 tests/initdir-check.py --red stamped build/id-b-before.img
	@# the strongest check: byte-for-byte against mkcpmfs --initdir over
	@# the SAME starting image, an implementation this port already had
	@python3 tests/initdir-check.py oracle build/id-b-after.img $(IDORACLE)
	@# ...and the same assertion on the OTHER drive's slice goes red
	@python3 tests/initdir-check.py --red oracle build/id-a-after.img $(IDORACLE)
	@# nothing lost, nothing duplicated
	@python3 tests/initdir-check.py preserved build/id-b-before.img build/id-b-after.img
	@# ...and that check catches a lost entry: erase one from a copy of
	@# the RESULT (entry 1, ID1.TXT) and run the same comparison
	@cp build/id-b-after.img build/id-b-damaged.img
	@printf '\345' | dd of=build/id-b-damaged.img bs=1 seek=32 conv=notrunc status=none
	@python3 tests/initdir-check.py --red preserved build/id-b-before.img build/id-b-damaged.img
	@# WHICH entry went where -- a count can match with the wrong entry moved
	@python3 tests/initdir-check.py at build/id-b-after.img 3 0x21
	@python3 tests/initdir-check.py at build/id-b-after.img 7 0x21
	@python3 tests/initdir-check.py at build/id-b-after.img 9 0x00:ID3.TXT
	@python3 tests/initdir-check.py at build/id-b-after.img 10 0x20:C900B
	@python3 tests/initdir-check.py at build/id-b-after.img 8 0x10:ID4.TXT
	@python3 tests/initdir-check.py --red at build/id-a-after.img 9 0x00:ID3.TXT
	@# ---- drive identity: A: came out of an `INITDIR B:' run untouched
	@python3 tests/initdir-check.py same build/id-a-before.img build/id-a-after.img
	@python3 tests/initdir-check.py --red same build/id-b-before.img build/id-b-after.img
	@# ---- and the drive the RUNNING system thought it had ----
	@grep -q 'Drive B: directory entries: 512' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- INITDIR did not report B:'s directory size"; exit 1; }
	@grep -q 'disk blocks: 2048' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- INITDIR got a dpb that is not B:'s (B: is 2048 blocks, A: is 2560)"; exit 1; }
	@grep -q 'Entries to relocate: 2' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- pass 1 did not find the two occupied fourth slots"; exit 1; }
	@grep -q 'Entries relocated: 2' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- pass 2 did not relocate them"; exit 1; }
	@grep -q 'Time stamp entries created: 128' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- wrong number of SFCBs reported"; exit 1; }
	@grep -q 'File slots now usable: 384' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- the capacity cost was not reported"; exit 1; }
	@grep -q 'INITDIR complete.' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- the run did not finish"; exit 1; }
	@# ---- cache invalidation: the relocated file still reads AFTERWARDS,
	@# in the same session, from a BDOS that had the old layout cached
	@test "`grep -c 'INITDIR fixture file 3 on drive B:' $(IDLOG)`" = 2 \
		|| { echo "verify-initdir: FAIL -- B:ID3.TXT does not TYPE both before AND after the run: the BDOS is serving a stale directory (INITDIR.PLI:987-993)"; exit 1; }
	@grep -q 'INITDIR fixture file 5 on drive B:' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- a file that did NOT move stopped reading"; exit 1; }
	@grep -q 'B: ID4      TXT' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- the file whose XFCB was preserved is gone"; exit 1; }
	@# ---- boot 2: refusals ----
	@grep -q 'Unrecognized drive.' $(IDLOG2) \
		|| { echo "verify-initdir: FAIL -- INITDIR with no drive did not refuse to guess"; exit 1; }
	@grep -q 'Enter Drive: ' $(IDLOG2) \
		|| { echo "verify-initdir: FAIL -- INITDIR did not ask for a drive"; exit 1; }
	@grep -q 'Directory already re-formatted.' $(IDLOG2) \
		|| { echo "verify-initdir: FAIL -- a second run did not detect the drive is already stamped"; exit 1; }
	@test "`grep -c 'INITDIR TERMINATED.' $(IDLOG2)`" = 2 \
		|| { echo "verify-initdir: FAIL -- expected two refusals (already formatted, and N at the confirmation)"; exit 1; }
	@# ---- boot 3: function 50 refuses what it says it refuses ----
	@grep -q 'BIOSET: sectran(4242) = 4242' $(IDLOG3) \
		|| { echo "verify-initdir: FAIL -- an ALLOWED function 50 code did not reach the BIOS"; exit 1; }
	@test "`grep -c 'BIOSET: .* ok' $(IDLOG3)`" = 9 \
		|| { echo "verify-initdir: FAIL -- the function 50 allow/refuse list is not what src/bdos/iosys.c says it is"; exit 1; }
	@test "`grep -c 'WRONG' $(IDLOG3)`" = 0 \
		|| { echo "verify-initdir: FAIL -- function 50 allowed a code it refuses, or refused one it allows"; exit 1; }
	@echo "verify-initdir: PASS -- B: stamped byte-identically to the mkcpmfs oracle, 2 entries relocated, nothing lost, A: untouched, caches invalidated, function 50 contract held"

# verify-initdir-mutants: the gate on the gate.  tests/initdir-mutate.sh has
# existed since verify-initdir was written and NO TARGET RAN IT -- a mutation
# driver nothing invokes is a demonstration nobody has seen, which is this
# project's defect wearing a lab coat.  Each of M1..M4 breaks one thing
# verify-initdir claims to check (cache invalidation, SFCB zeroing, the fn-50
# allow list, entry relocation) and MUST make it fail; a mutant that passes
# means the assertion for it is decorative.  The sources are restored from
# Restore checked by git, not by diffing a maintained copy.
#
# The save, the mutations and the restore are ONE recipe line on purpose.
# make runs a recipe line containing $(MAKE) even under -n (so that -n
# propagates into sub-makes), but it skips the ordinary lines around it --
# so with `save' on a line of its own, `make -n' applied a mutation and
# then found no build/pristine to restore from, and left a mutated
# src/cmd/initdir.c in the tree.  Paired in one line, whatever make
# chooses to run gets both halves.
.PHONY: verify-initdir-mutants
verify-initdir-mutants: all
	@rm -rf build/pristine; \
	sh tests/initdir-mutate.sh save || exit 1; \
	rc=0; \
	for m in M1 M2 M3 M4; do \
		sh tests/initdir-mutate.sh $$m || { rc=1; break; }; \
		if $(MAKE) --no-print-directory verify-initdir >build/mut-$$m.log 2>&1; then \
			echo "verify-initdir-mutants: FAIL -- $$m PASSED verify-initdir;"; \
			echo "  whatever that mutant broke, nothing is asserting on it"; \
			rc=1; \
		else \
			echo "verify-initdir-mutants: $$m correctly failed verify-initdir"; \
		fi; \
		sh tests/initdir-mutate.sh restore >/dev/null || { rc=1; break; }; \
	done; \
	sh tests/initdir-mutate.sh restore >/dev/null 2>&1 || true; \
	git diff --quiet -- src/cmd/initdir.c src/bdos/iosys.c \
		|| { echo "verify-initdir-mutants: FAIL -- sources not restored"; rc=1; }; \
	rm -rf build/pristine; \
	test $$rc = 0 || exit 1; \
	echo "verify-initdir-mutants: PASS -- all four mutants broke verify-initdir, tree restored"

# ---- self-host chain (dev harness): compile, link and run HELLO on
# target with the DRI toolchain, one command per cold boot (the prompt
# gate eats chained input), each transcript per-line timestamped for
# phase wall times.  Afterwards the cpma partition (block 38144) is
# extracted and the artifacts the chain wrote are read back off it.
#
# They are NOT diffed against a reference: nothing in this tree ships a
# DRI-built HELLO.O or HELLO2.Z8K to diff them against, and one built by
# the host cross-compiler would be a different compiler's output, so the
# comparison would differ by construction.  (An earlier version of this
# comment said the diff happened.  It never did.)  What tests/selfhostchk.sh
# checks instead is that each artifact is the kind of file its producer
# emits -- EE02 for an unlinked object, EE03 for a linked program -- and
# that the string literal in HELLO.C reached the object, the program and
# then the console: a chain from the source to the running system with no
# reference file in it.  It also scans the ZCC and LD8K transcripts for
# those tools' own diagnostics, without which a failed compile or link
# would leave the PREVIOUS HELLO.O/HELLO2.Z8K on the disk and the run
# would pass on them.
SELFDIR	= build/selfhost
CPMA_START = 38144
.PHONY: selfhost
selfhost: all
	$(MKDISK) $(TESTIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TESTIMG)) \
		--input="$(OSSEL)ZCC HELLO.C\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| python3 $(abspath tests/tstamp.py) \
		| tee $(abspath $(SELFDIR).1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TESTIMG)) \
		--input="$(OSSEL)LD8K -O HELLO2.Z8K STARTUP.O HELLO.O LIBCPM.A\r$(ENDIN)" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| python3 $(abspath tests/tstamp.py) \
		| tee $(abspath $(SELFDIR).2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TESTIMG)) \
		--input="$(OSSEL)HELLO2\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| python3 $(abspath tests/tstamp.py) \
		| tee $(abspath $(SELFDIR).3.log)
	@$(EMUOK)
	rm -rf $(SELFDIR); mkdir -p $(SELFDIR)
	dd if=$(TESTIMG) of=$(SELFDIR)/cpma.img bs=512 skip=$(CPMA_START) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(SELFDIR)/cpma.img $(SELFDIR)
	@sh tests/selfhostchk.sh $(SELFDIR).1.log $(SELFDIR).2.log \
		$(SELFDIR).3.log $(SELFDIR) \
		|| { echo "selfhost: FAIL"; exit 1; }
	@echo "selfhost: PASS -- artifacts in $(SELFDIR)/"

# ---- src/app: the native applications, rebuilt on the machine ----
# `selfhost' above proves the chain works on a nine-line HELLO.C.  These
# two targets are that chain doing real work, and they answer a question
# HELLO.C cannot: the five .Z8K files checked into src/app/ beside their
# source -- are they still what that source compiles to?
#
# They cannot be built by `all'.  The compiler is ZCC.Z8K and the linker
# LD8K.Z8K; both are Z8001 programs that run on the target, so a rebuild
# needs the emulator, and `all' must not.  So the binaries are checked in,
# and these targets are what stops "checked in" from meaning "unchecked":
# each one ERASES the .Z8K on drive A:, rebuilds it there from the .C
# beside it, pulls the partition back out and cmps the result against the
# repository's copy.  Erase first, or a failed compile leaves the old file
# in place and the cmp compares the checked-in binary with itself.
#
# The cmp is an assertion and not a hope because ZCC and LD8K are
# byte-reproducible -- measured, not assumed: two entirely separate
# sessions produced fourteen byte-identical SDB objects from the same
# source.  If a cmp here ever fails, the source and the binary have come
# apart and both belong in the same commit; tests/appchk.sh says so.
#
# tests/appbuild.sh does the building, ONE COLD BOOT PER COMMAND, and its
# header says why that is not the extravagance it looks like: a single
# scripted session gets bytes eaten by ZCC's chained passes, and does it
# intermittently, which is the worst way for a verification target to be
# wrong.  The read-back and the assertions stay here.
A3IMG	= build/a3test.bin
A3DIR	= build/a3
A3LOG	= build/verify-a3.log
.PHONY: verify-a3
verify-a3: all
	$(MKDISK) $(A3IMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	sh tests/appbuild.sh $(A3IMG) $(A3LOG).1 SORTFL.Z8K SORTFL
	sh tests/appbuild.sh $(A3IMG) $(A3LOG).2 KILLDU.Z8K KILLDU
	sh tests/appbuild.sh $(A3IMG) $(A3LOG).3 TOHEX.Z8K TOHEX
	sh tests/appbuild.sh $(A3IMG) $(A3LOG).4 FROMHEX.Z8K FROMHEX
	cat $(A3LOG).1 $(A3LOG).2 $(A3LOG).3 $(A3LOG).4 > $(A3LOG)
	rm -rf $(A3DIR); mkdir -p $(A3DIR)
	dd if=$(A3IMG) of=$(A3DIR)/cpma.img bs=512 skip=$(CPMA_START) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(A3DIR)/cpma.img $(A3DIR)
	@sh tests/appchk.sh $(A3LOG) $(A3DIR) \
		SORTFL.Z8K KILLDU.Z8K TOHEX.Z8K FROMHEX.Z8K \
		|| { echo "verify-a3: FAIL"; exit 1; }
	@echo "verify-a3: PASS -- Robert Heller's four utilities rebuilt on the"
	@echo "           machine from the source shipped beside them"

# ---- Gate A: SDB ----
# SDB is a 5,250-line relational DBMS with no terminal dependency at all:
# what it exercises is the BDOS file layer -- creatb, lseek, random-record
# read and write -- which is why the applications plan put it first.
#
# Fourteen compiles and a link, about eight minutes of emulated time.
# This is the slowest target in the suite and it is slow for an honest
# reason: it is a 1984 three-pass C compiler compiling a real program on a
# 6 MHz machine.  Then one more cold boot runs what those fourteen
# compiles produced -- read the help file off the disk, create a relation,
# import three tuples from a text file, print it whole and then through a
# WHERE clause, export it back out.  The export lands in a file, and the
# file is pulled off the partition and checked host-side, so the answer is
# not just something that scrolled past on a transcript.
SDBIMG	= build/sdbtest.bin
SDBDIR	= build/sdb
SDBLOG	= build/verify-sdb
SDBSRC	= CMD COM CRE ERR IEX INT IO JUNK MTH SCN SDB SEL SRT TBL
# The session.  `create' names the attributes and their widths and the
# relation's tuple capacity; `import' reads SDBIN.TXT one attribute value
# per line; the `where' clause compares a num attribute, which in SDB is a
# digit STRING compared by MTH.C's own arithmetic, so it is a real test of
# a file this target just compiled.  The \" are for the shell: SDB wants
# real quotes around a file name, or it scans it as an identifier and
# appends .dat.
.PHONY: verify-sdb
verify-sdb: all
	$(MKDISK) $(SDBIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	sh tests/appbuild.sh $(SDBIMG) $(SDBLOG).1.log SDB.Z8K $(SDBSRC)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SDBIMG)) \
		--input="$(SDBRUNIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } \
		| python3 $(abspath tests/tstamp.py) \
		| tee $(abspath $(SDBLOG)).2.log
	@$(EMUOK)
	rm -rf $(SDBDIR); mkdir -p $(SDBDIR)
	dd if=$(SDBIMG) of=$(SDBDIR)/cpma.img bs=512 skip=$(CPMA_START) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(SDBDIR)/cpma.img $(SDBDIR)
	@sh tests/appchk.sh $(SDBLOG).1.log $(SDBDIR) SDB.Z8K \
		|| { echo "verify-sdb: FAIL"; exit 1; }
	@sh tests/sdbchk.sh $(SDBLOG).2.log $(SDBDIR) \
		|| { echo "verify-sdb: FAIL"; exit 1; }
	@echo "verify-sdb: PASS -- SDB compiled from its own source on the"
	@echo "            machine, byte-identical to src/app/SDB.Z8K; and that"
	@echo "            binary created a relation, imported three tuples,"
	@echo "            selected two of them and exported all three back"
	@echo "            unchanged.  GATE A."

# ---- real-time clock ----
# DATE against BIOS function 23, over the emulator's OKI MSM58321 model
# ($(EMU)/src/bus.c, "OKI MSM58321 real-time clock").  That model is the
# datasheet's sixteen 4-bit registers, address latch, /CS-gated strobes and
# STOP level, with a divider clocked off emulated CPU time -- so it TICKS,
# and a register image read while the counters carry is torn exactly as it
# is on silicon.  Three cold boots:
#
#  live    seeded 2026-07-31 14:32:10 (--rtc).  DATE must read the seeded
#          clock back, two successive reads must DIFFER (the clock runs),
#          a command-line set of 2004-03-01 07:08:09 -- the day after a
#          leap 29 February, a Monday -- must round-trip, and out-of-range
#          arguments must still be rejected without touching the chip.
#  race    the same, with the crystal overclocked (--rtc-ips) so seconds
#  noclock --rtc=none, a board with no module fitted: fn 23 must report
#          "no clock" rather than invent a time, and a set must say so.
RTCIMG	= build/rtctest.bin
RTCSEED	= 2026-07-31T14:32:10
# Instructions per emulated RTC second. DATE C continuously reads the clock
# until RTCRACEMAX, sampling carries without depending on boot alignment.
# The rate must allow rtcget's four retry pairs to converge while producing
# at least one retry (26 excess register reads) in the instruction budget.
# Use rtc-race-sweep to check that margin after changing the driver.
RTCLOG	= build/verify-rtc.log
RTCRACELOG = build/verify-rtc-race.log
RTCNONELOG = build/verify-rtc-noclock.log
RTCVERIFYIN = $(OSSEL)DATE\rDATE\rDATE 03/01/04 07:08:09\rDATE\rDATE 13/45/99 99:99:99\rDATE Q\rDATE 01/01/78 00:00:00\rDATE 12/31/77 12:34:56\rDATE\rDATE 07/04/05 12:34:56\r$(ENDIN)
RTCNONEIN = $(OSSEL)DATE\rDATE 07/31/26 14:32:10\rDATE\r$(ENDIN)
.PHONY: verify-rtc
verify-rtc: all
	$(MKDISK) $(RTCIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RTCIMG)) --rtc=$(RTCSEED) \
		--input="$(RTCVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) \
		2>$(abspath $(RTCLOG)).err; $(EMUSTAT); } | tee $(abspath $(RTCLOG))
	@$(EMUOK)
	@tail -1 $(RTCLOG).err
	@grep -q 'No clock' $(RTCLOG) \
		&& { echo "verify-rtc: FAIL -- the live clock was not seen"; exit 1; } || true
	@test "`tr -d '\r' < $(RTCLOG) | grep -c '^Fri 07/31/26 14:3'`" -ge 2 \
		|| { echo "verify-rtc: FAIL -- the seeded time did not read back"; exit 1; }
	@test "`tr -d '\r' < $(RTCLOG) | grep '^Fri 07/31/26 14:3' | sort -u | wc -l`" -ge 2 \
		|| { echo "verify-rtc: FAIL -- two reads gave the same second: the clock is not running"; exit 1; }
	@test "`tr -d '\r' < $(RTCLOG) | grep -c '^Mon 03/01/04 07:08:'`" -ge 2 \
		|| { echo "verify-rtc: FAIL -- DATE MM/DD/YY HH:MM:SS did not round-trip"; exit 1; }
	@test "`tr -d '\r' < $(RTCLOG) | grep '^Mon 03/01/04 07:08:' | sort -u | wc -l`" -ge 2 \
		|| { echo "verify-rtc: FAIL -- the clock stopped after being set"; exit 1; }
	@test "`grep -c 'Usage: DATE' $(RTCLOG)`" = 2 \
		|| { echo "verify-rtc: FAIL -- bad arguments not rejected"; exit 1; }
	@tr -d '\r' < $(RTCLOG) | grep -q '^Sun 01/01/78 00:00:00' \
		|| { echo "verify-rtc: FAIL -- yy = 78 is not 1978, the low end of the window"; exit 1; }
	@tr -d '\r' < $(RTCLOG) | grep -q '^Fri 12/31/77 12:34:56' \
		|| { echo "verify-rtc: FAIL -- yy = 77 is not 2077, the high end of the window"; exit 1; }
	@test "`tr -d '\r' < $(RTCLOG) | grep -c '^Fri 12/31/77 12:34:5'`" -ge 2 \
		|| { echo "verify-rtc: FAIL -- 2077 did not come back through the year window"; exit 1; }
	@tr -d '\r' < $(RTCLOG) | grep -q '^Mon 07/04/05 12:34:56' \
		|| { echo "verify-rtc: FAIL -- 2005-07-04 did not round-trip"; exit 1; }
	@test "`sed -n 's/.*, leap \([0-9]*\),.*/\1/p' $(RTCLOG).err`" = 3 \
		|| { echo "verify-rtc: FAIL -- the leap-year selection written for 2005 is not the datasheet's countdown code 11"; exit 1; }
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RTCIMG)) --rtc=$(RTCSEED) \
	@$(EMUOK)
	@tail -1 $(RTCRACELOG).err
	@grep -q 'No clock' $(RTCRACELOG) \
		&& { echo "verify-rtc: FAIL -- a read across a carry was not recovered"; exit 1; } || true
		|| { echo "verify-rtc: FAIL -- no read was retried, so no carry was straddled"; exit 1; }
	@test "`sed -n 's/.*(\([0-9]*\) inside a .CS transaction).*/\1/p' $(RTCRACELOG).err`" -ge 1 \
		|| { echo "verify-rtc: FAIL -- no carry landed inside a transaction"; exit 1; }
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RTCIMG)) --rtc=none \
		--input="$(RTCNONEIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(RTCNONELOG))
	@$(EMUOK)
	@test "`grep -c 'No clock in this machine' $(RTCNONELOG)`" = 2 \
		|| { echo "verify-rtc: FAIL -- fn 23 read did not report the missing clock"; exit 1; }
	@grep -q 'No clock: the time was not set' $(RTCNONELOG) \
		|| { echo "verify-rtc: FAIL -- fn 23 set did not report the missing clock"; exit 1; }
	@echo "verify-rtc: PASS -- live clock read, set round-tripped, carry straddle recovered, absent clock reported"

# ---- the clock, on the host ----
# tests/rtctest.c runs the REAL src/bios/rtc900.c and src/cmd/date.c, compiled
# verbatim with the host cc, against the software MSM58321 in
# tests/rtcchip.c.  tests/rtcinc/stdio.h stands in for the DRI type layer
# and gives UBYTE the same SIGNED char and UWORD the same 16 bits the
# target compiler does, so a missing 0xff mask misbehaves here too.
#
# This is not a substitute for verify-rtc -- it never runs a Z8001
# instruction -- but it reaches what a cold boot cannot: every day of the
# representable century, both ends of the two-digit year window, a carry
# injected at a chosen register read, and a chip that lies.
#
# verify-rtc-mutants is the gate on the gate: it rebuilds these tests
# against deliberately broken copies of the driver and requires each break
# to be caught.  A check that cannot fail cannot be merged.
RTCOBJ	= build/rtcobj
.PHONY: verify-rtc-host verify-rtc-mutants
$(RTCOBJ):
	mkdir -p $(RTCOBJ)
$(RTCOBJ)/rtc900.o: src/bios/rtc900.c tests/rtcinc/stdio.h | $(RTCOBJ)
	$(HOSTCC) -std=gnu89 -w -Itests/rtcinc -c $< -o $@
$(RTCOBJ)/date.o: src/cmd/date.c src/cmd/cpm.h | $(RTCOBJ)
	$(HOSTCC) -std=gnu89 -w -Isrc/cmd -Dstatic= -Dmain=date_main -c $< -o $@
$(RTCOBJ)/rtcchip.o: tests/rtcchip.c tests/rtcchip.h | $(RTCOBJ)
	$(HOSTCC) -std=gnu89 -w -Itests -c $< -o $@
$(RTCOBJ)/rtctest.o: tests/rtctest.c tests/rtcchip.h | $(RTCOBJ)
	$(HOSTCC) -std=gnu89 -w -Itests -c $< -o $@
build/rtctest: $(RTCOBJ)/rtctest.o $(RTCOBJ)/rtcchip.o $(RTCOBJ)/rtc900.o \
	       $(RTCOBJ)/date.o
	$(HOSTCC) -o $@ $^
verify-rtc-host: build/rtctest
	build/rtctest
verify-rtc-mutants: build/rtctest
	sh tests/rtcmutate.sh $(HOSTCC)

# ---- the clock across a New Year ----
# Leap-year D10 behaviour across New Year is untested inference from datasheet.
# Emulator and rtcchip.c model it as down-counter decremented on year carry.
# This target settles the emulator model and driver; not a statement about silicon.
#
# Four cold boots.  The clock runs off the instruction count, so every one
# of them is reproducible.
#
#  before   1979 seeded and left alone: the report must say leap 1, the
#           code for a surplus of 3.  This is the value the rollover below
#           has to move, and without it "leap 0 afterwards" proves nothing.
#  carry    DATE sets 12/31/79 23:59:30 -- from the machine, so the moment
#           does not depend on how long the boot took -- and DATE C then
#           prints until the emulator stops.  The transcript has to cross
#           into Tue 01/01/80, and the report has to say leap 0.
#  leapday  1980-02-28 23:59:30 seeded: 29 February must exist.
#  noleap   1981-02-28 23:59:30 seeded, the control: it must not.
#
# DATE C prints thousands of lines, so its transcripts go to files rather
# than through tee, and the target prints the lines it turns on.
NYIMG	 = build/nytest.bin
NYLOG	 = build/verify-rtc-newyear
NYMAX	?= 120000000
NYBEFOREMAX ?= 60000000
.PHONY: verify-rtc-newyear
verify-rtc-newyear: all
	$(MKDISK) $(NYIMG) $(CPMSYS) $(CPMAIMG)
	$(EMUCD) && ./c900 --disk=$(abspath $(NYIMG)) \
		--rtc=1979-12-31T23:00:00 --input="$(OSSEL)DATE\r$(ENDIN)" \
		--max=$(NYBEFOREMAX) 2>$(abspath $(NYLOG))-before.err \
		> $(abspath $(NYLOG))-before.log
	@tail -1 $(NYLOG)-before.err
	@tr -d '\r' < $(NYLOG)-before.log | grep -q '^Mon 12/31/79 23:00:' \
		|| { echo "verify-rtc-newyear: FAIL -- the seeded 1979 clock did not read back"; exit 1; }
	@test "`sed -n 's/.*, leap \([0-9]*\),.*/\1/p' $(NYLOG)-before.err`" = 1 \
		|| { echo "verify-rtc-newyear: FAIL -- 1979 is a surplus of 3 and its code is 01"; exit 1; }
	$(EMUCD) && ./c900 --disk=$(abspath $(NYIMG)) \
		--rtc=1979-12-31T23:50:00 \
		--input="$(OSSEL)DATE\rDATE 12/31/79 23:59:30\rDATE C\r$(ENDIN)" \
		--max=$(NYMAX) 2>$(abspath $(NYLOG))-carry.err \
		> $(abspath $(NYLOG))-carry.log
	@tail -1 $(NYLOG)-carry.err
	@tr -d '\r' < $(NYLOG)-carry.log | grep -m1 '^Mon 12/31/79 23:59:5'
	@tr -d '\r' < $(NYLOG)-carry.log | grep -m1 '^Tue 01/01/80 00:00:0'
	@tr -d '\r' < $(NYLOG)-carry.log | grep -q '^Mon 12/31/79 23:59:5' \
		|| { echo "verify-rtc-newyear: FAIL -- the last seconds of 1979 were never read"; exit 1; }
	@tr -d '\r' < $(NYLOG)-carry.log | grep -q '^Tue 01/01/80 00:00:0' \
		|| { echo "verify-rtc-newyear: FAIL -- the clock did not cross into 1980"; exit 1; }
	@tr -d '\r' < $(NYLOG)-carry.log | grep -q '01/01/79' \
		&& { echo "verify-rtc-newyear: FAIL -- the day and month carried but the year did not"; exit 1; } || true
	@test "`sed -n 's/.*, leap \([0-9]*\),.*/\1/p' $(NYLOG)-carry.err`" = 0 \
		|| { echo "verify-rtc-newyear: FAIL -- the leap-year selection did not count 01 -> 00 across the carry"; exit 1; }
	$(EMUCD) && ./c900 --disk=$(abspath $(NYIMG)) \
		--rtc=1980-02-28T23:59:30 --input="$(OSSEL)DATE C\r$(ENDIN)" \
		--max=$(NYMAX) 2>$(abspath $(NYLOG))-leapday.err \
		> $(abspath $(NYLOG))-leapday.log
	@tail -1 $(NYLOG)-leapday.err
	@tr -d '\r' < $(NYLOG)-leapday.log | grep -m1 '^Thu 02/28/80 23:59:5'
	@tr -d '\r' < $(NYLOG)-leapday.log | grep -m1 '^Fri 02/29/80 00:00:0'
	@tr -d '\r' < $(NYLOG)-leapday.log | grep -q '^Thu 02/28/80 23:59:5' \
		|| { echo "verify-rtc-newyear: FAIL -- 1980-02-28 was never read"; exit 1; }
	@tr -d '\r' < $(NYLOG)-leapday.log | grep -q '^Fri 02/29/80 00:00:0' \
		|| { echo "verify-rtc-newyear: FAIL -- 1980 has no 29 February"; exit 1; }
	@test "`sed -n 's/.*, leap \([0-9]*\),.*/\1/p' $(NYLOG)-leapday.err`" = 0 \
		|| { echo "verify-rtc-newyear: FAIL -- 1980's leap-year selection is not 00"; exit 1; }
	$(EMUCD) && ./c900 --disk=$(abspath $(NYIMG)) \
		--rtc=1981-02-28T23:59:30 --input="$(OSSEL)DATE C\r$(ENDIN)" \
		--max=$(NYMAX) 2>$(abspath $(NYLOG))-noleap.err \
		> $(abspath $(NYLOG))-noleap.log
	@tail -1 $(NYLOG)-noleap.err
	@tr -d '\r' < $(NYLOG)-noleap.log | grep -m1 '^Sat 02/28/81 23:59:5'
	@tr -d '\r' < $(NYLOG)-noleap.log | grep -m1 '^Sun 03/01/81 00:00:0'
	@tr -d '\r' < $(NYLOG)-noleap.log | grep -q '^Sun 03/01/81 00:00:0' \
		|| { echo "verify-rtc-newyear: FAIL -- 1981-02-28 did not carry to 1 March"; exit 1; }
	@tr -d '\r' < $(NYLOG)-noleap.log | grep -q '02/29/81' \
		&& { echo "verify-rtc-newyear: FAIL -- 1981 grew a 29 February"; exit 1; } || true
	@test "`sed -n 's/.*, leap \([0-9]*\),.*/\1/p' $(NYLOG)-noleap.err`" = 3 \
		|| { echo "verify-rtc-newyear: FAIL -- 1981 is a surplus of 1 and its code is 11"; exit 1; }
	@echo "verify-rtc-newyear: PASS -- the model crosses a New Year, counts the leap"
	@echo "                    selection down with it, and grows a 29 February only in 1980"

# ---- CP/M 3 V2 wave: the system control block (fn 49) and parse (152) ----
# One cold boot running SCBTEST, which checks each field against the BDOS
# state it mirrors (fn 25, 32, 26, 108, 109, 44) rather than against a
# value it stored itself, and parses five filenames.  The program prints
# every check and ends with one PASS/FAIL line.
V2IMG	= build/v2test.bin
V2LOG	= build/verify-v2.log
.PHONY: verify-v2
verify-v2: all
	$(MKDISK) $(V2IMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(V2IMG)) \
		--input="$(OSSEL)SCBTEST\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(V2LOG))
	@$(EMUOK)
	@grep -q 'fn 9 now stops at a hash' $(V2LOG) \
		|| { echo "verify-v2: FAIL -- an SCB byte set did not reach the BDOS"; exit 1; }
	@test "`grep -c ' BAD' $(V2LOG)`" = 0 \
		|| { echo "verify-v2: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'SCBTEST: PASS' $(V2LOG) \
		|| { echo "verify-v2: FAIL -- SCBTEST did not finish"; exit 1; }
	@echo "verify-v2: PASS"

		--input="$(OSSEL)V3RET\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
# SDIR, SHOW, SUBMIT: CCP releases one line at a time, so one cold boot per session.
# SDIR pages by default (SCB page-length 0 = 24-line v3 fallback); [NOPAGE] overrides.
# SUBMIT is A:SUBMIT (resident builtin shadows bare name).
# DOL expands to one '$': SUBMIT's output file really is called $$$.SUB.
DOL	= $$
UTILIMG	= build/utiltest.bin
UTILLOG	= build/verify-util
UTILIN1	= $(OSSEL)SDIR [NOPAGE SIZE]\rSDIR [NOPAGE] *.TXT S*.Z8K\rSDIR [NOPAGE A SHORT] Z*.Z8K\r$(ENDIN)
UTILIN2	= $(OSSEL)SDIR [NOPAGE FULL EXCLUDE] *.Z8K *.H\rSDIR [NOPAGE BOGUS]\rSDIR [NOPAGE USER=ALL DRIVE=ALL SIZE] S*.Z8K\rSDIR [NOPAGE DATE] *.SUB\r$(ENDIN)
UTILIN3	= $(OSSEL)SHOW\rSHOW A:[DRIVE]\rSHOW [USERS]\rSHOW [LABEL]\r$(ENDIN)
UTILIN4	= $(OSSEL)A:SUBMIT TEST HELLO SECOND\rTYPE $(DOL)$(DOL)$(DOL).SUB\rA:SUBMIT NOSUCH\rA:SUBMIT BADP\r
UTILIN5	= $(OSSEL)A:SUBMIT TEST HELLO SECOND\r$(DOL)$(DOL)$(DOL)\r

.PHONY: verify-util
verify-util: all
	$(MKDISK) $(UTILIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(UTILIMG)) \
		--input='$(UTILIN1)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(UTILLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(UTILIMG)) \
		--input='$(UTILIN2)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(UTILLOG)-2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(UTILIMG)) \
		--input='$(UTILIN3)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(UTILLOG)-3.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(UTILIMG)) \
		--input='$(UTILIN4)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(UTILLOG)-4.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(UTILIMG)) \
		--input='$(UTILIN5)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(UTILLOG)-5.log)
	@$(EMUOK)
	@# ---- SDIR: the size arithmetic, computed from the DPB, must agree
	@# with what mkcpmfs packed.  BIG.TXT is 20480 bytes = 160 records in
	@# five 4K blocks, and is the one file whose size is fixed by hand.
	@grep -q 'BIG      TXT    20k    160 Dir RW' $(UTILLOG)-1.log \
		|| { echo "verify-util: FAIL -- SDIR size/record arithmetic"; exit 1; }
	@grep -q 'Used/Max Dir Entries For Drive A:' $(UTILLOG)-1.log \
		|| { echo "verify-util: FAIL -- SDIR totals line missing"; exit 1; }
		|| { echo "verify-util: FAIL -- SDIR [SHORT] four-per-line layout"; exit 1; }
	@# [EXCLUDE] must drop every .Z8K and .H and keep the rest
	@grep -q 'HELLO    C ' $(UTILLOG)-2.log \
		|| { echo "verify-util: FAIL -- SDIR [EXCLUDE] dropped a wanted file"; exit 1; }
	@# ' Z8K ' is the type column of printfn; it does not match the echoed
	@# command line (`*.Z8K') nor a name that merely contains it (ASZ8K).
	@sed -n '/Name     Bytes/,/Total Bytes/p' $(UTILLOG)-2.log | grep -q ' Z8K ' \
		&& { echo "verify-util: FAIL -- SDIR [EXCLUDE] kept an excluded file"; exit 1; } || true
	@grep -q 'ERROR: Illegal Option or Modifier.' $(UTILLOG)-2.log \
		|| { echo "verify-util: FAIL -- SDIR bad option not rejected"; exit 1; }
	@# Dated display: Drive A: stamped and labelled, so SDIR [DATE] and
	@# SHOW [LABEL] now display stamp columns and label.  TEST.SUB packed
	@# (not written on target), so stamps are mkcpmfs blanks.
	@sed -n '/Update  *Create/,/Total Bytes/p' $(UTILLOG)-2.log \
		| grep -q 'TEST     SUB' \
		|| { echo "verify-util: FAIL -- SDIR [DATE] did not list the file"; exit 1; }
	@grep -q 'ERROR: Date and Time Stamping Inactive.' $(UTILLOG)-2.log \
		&& { echo "verify-util: FAIL -- SDIR [DATE] refused a stamped drive"; exit 1; } || true
	@# ---- SHOW
	@grep -q 'A: RW, Space:' $(UTILLOG)-3.log \
		|| { echo "verify-util: FAIL -- SHOW space line"; exit 1; }
	@grep -q ': Kilobyte Drive  Capacity' $(UTILLOG)-3.log \
		|| { echo "verify-util: FAIL -- SHOW [DRIVE] characteristics"; exit 1; }
	@grep -q '      512: 32 Byte  Directory Entries' $(UTILLOG)-3.log \
		|| { echo "verify-util: FAIL -- SHOW [DRIVE] read the DPB wrong"; exit 1; }
	@grep -q 'A: Active User :   0' $(UTILLOG)-3.log \
		|| { echo "verify-util: FAIL -- SHOW [USERS]"; exit 1; }
	@grep -q 'A: Number of free directory entries:' $(UTILLOG)-3.log \
		|| { echo "verify-util: FAIL -- SHOW [USERS] directory report"; exit 1; }
	@grep -q 'Label for drive A:' $(UTILLOG)-3.log \
		|| { echo "verify-util: FAIL -- SHOW [LABEL] did not find the label"; exit 1; }
	@grep -q '^$(LABEL) *\. *off *on *on' $(UTILLOG)-3.log \
		|| { echo "verify-util: FAIL -- SHOW [LABEL] mode columns"; exit 1; }
	@# ---- SUBMIT: the four substitutions, then the error paths
	@grep -q 'MHELLO HELLO SECOND' $(UTILLOG)-4.log \
		|| { echo "verify-util: FAIL -- SUBMIT \$$1/\$$2 substitution"; exit 1; }
	@grep -q 'TYPE HELLO.TXT' $(UTILLOG)-4.log \
		|| { echo "verify-util: FAIL -- SUBMIT \$$n inside a longer word"; exit 1; }
	@grep -q 'MHELLO A.B' $(UTILLOG)-4.log \
		|| { echo "verify-util: FAIL -- SUBMIT \$$\$$ did not collapse"; exit 1; }
	@test "`tr -d '\r' < $(UTILLOG)-4.log | grep -c '^MHELLO ONE$$'`" = 1 \
		|| { echo "verify-util: FAIL -- SUBMIT ! did not split the line"; exit 1; }
	@test "`tr -d '\r' < $(UTILLOG)-4.log | grep -c '^MHELLO TWO$$'`" = 1 \
		|| { echo "verify-util: FAIL -- SUBMIT ! did not split the line"; exit 1; }
	@grep -q "ERROR: No 'SUB' File Found" $(UTILLOG)-4.log \
		|| { echo "verify-util: FAIL -- SUBMIT missing-file path"; exit 1; }
	@grep -q 'Error On Line 00001 : Parameter Error' $(UTILLOG)-4.log \
		|| { echo "verify-util: FAIL -- SUBMIT parameter error path"; exit 1; }
	@# ---- and the batch actually runs, across four warm boots
	@grep -q '  arg 2: SECOND' $(UTILLOG)-5.log \
		|| { echo "verify-util: FAIL -- the expanded batch did not run"; exit 1; }
	@grep -q '  arg 1: TWO' $(UTILLOG)-5.log \
		|| { echo "verify-util: FAIL -- the batch stopped before its last line"; exit 1; }
	@echo "verify-util: PASS -- SDIR three formats + options + errors, SHOW four reports, SUBMIT expanded and executed"

# ---- SET: attributes, labels, stamping modes ----
# Four cold boots on one patched image, then the partition is pulled back
# out and read HOST-SIDE.  The on-disk read is the point: every on-target
# rendering here (STAT, SDIR, SHOW) asks the same BDOS that SET just
# talked to, so only the bytes on the medium can say SET wrote what it
# said it wrote.
#
#  1  file attributes -- one file, a wildcard, two names on one command,
#     and a name that does not exist
#  2  the directory label -- rename it and turn UPDATE off, and the mode
#     bits SET did not mention must survive (SET reads the label before
#     it writes it, c900oses/cpm8000/ref/cpm3/set.plm:909-949)
#  3  ACCESS=ON, which is the one stamping mode this system had never
#     run: it shares the create field, so turning it on must turn CREATE
#     off, and the next file OPEN must then stamp that field
#  4  the refusals, including the deliberate password one
SETIMG	= build/settest.bin
SETLOG	= build/verify-set
SETRTC	= 2026-07-31T14:32:10
SETIN1	= $(OSSEL)SET HELLO.TXT [RO,SYS,F1=ON,F3=ON]\rSTAT HELLO.TXT\rSET *.SUB [F2=ON]\rSET HELLO.C HELLO.TXT [ARCHIVE=ON]\rSET NOSUCH.TXT [RO]\r$(ENDIN)
SETIN2	= $(OSSEL)SHOW [LABEL]\rSET [NAME=C900SET]\rSET [UPDATE=OFF]\rSHOW [LABEL]\r$(ENDIN)
SETIN3	= $(OSSEL)SET [ACCESS=ON]\rSDIR [NOPAGE DATE] HELLO.C\rTYPE HELLO.C\rSDIR [NOPAGE DATE] HELLO.C\r$(ENDIN)
SETIN4	= $(OSSEL)SET HELLO.C [PASSWORD=SECRET]\rSET [ACCESS=ON,CREATE=ON]\rSET HELLO.C [RO,RW]\rSET HELLO.C [NAME=X]\rSET [BOGUS]\rSET HELLO.C [ACCESS]\rSET [DIR]\r$(ENDIN)

.PHONY: verify-set
verify-set: all
	$(MKDISK) $(SETIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SETIMG)) --rtc=$(SETRTC) \
		--input='$(SETIN1)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(SETLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SETIMG)) --rtc=$(SETRTC) \
		--input='$(SETIN2)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(SETLOG)-2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SETIMG)) --rtc=$(SETRTC) \
		--input='$(SETIN3)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(SETLOG)-3.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SETIMG)) --rtc=$(SETRTC) \
		--input='$(SETIN4)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(SETLOG)-4.log)
	@$(EMUOK)
	@# ---- 1: attributes, as SET reports them and as STAT sees them
	@grep -q 'A:HELLO   .TXT  set to system (SYS), Read Only (RO)' $(SETLOG)-1.log \
		|| { echo "verify-set: FAIL -- SET did not report the attributes it set"; exit 1; }
	@grep -q '1 Sys RO   1 3  A:HELLO   .TXT' $(SETLOG)-1.log \
		|| { echo "verify-set: FAIL -- STAT does not see RO/SYS/F1/F3"; exit 1; }
	@# [ARCHIVE=ON] names ONE bit: the four HELLO.TXT already carries
	@# have to come back out of the directory with it (set.plm:755-763)
	@grep -q 'A:HELLO   .TXT  set to system (SYS), Read Only (RO) *A13' $(SETLOG)-1.log \
		|| { echo "verify-set: FAIL -- a later SET dropped attributes it was not asked about"; exit 1; }
	@grep -q 'ERROR:  File not found' $(SETLOG)-1.log \
		|| { echo "verify-set: FAIL -- a name that does not exist was not reported"; exit 1; }
	@# ---- 2: the label, and the bits SET was not asked about
	@grep -q '^C900A   \. *off *on *on' $(SETLOG)-2.log \
		|| { echo "verify-set: FAIL -- the image did not start with the label the packer wrote"; exit 1; }
	@grep -q 'A:C900SET \. *off *on *off *on' $(SETLOG)-2.log \
		|| { echo "verify-set: FAIL -- [NAME=] lost the create/update bits it was not asked about"; exit 1; }
	@grep -q 'A:C900SET \. *off *on *off *off' $(SETLOG)-2.log \
		|| { echo "verify-set: FAIL -- [UPDATE=OFF] did not clear update (or lost the name)"; exit 1; }
	@grep -q '^C900SET \. *off *on *off' $(SETLOG)-2.log \
		|| { echo "verify-set: FAIL -- SHOW does not agree with SET about the label"; exit 1; }
	@# ---- 3: access stamping.  Turning it on must turn CREATE off (they
	@# share the field), SDIR's column must change name, and the field
	@# must be BLANK until a file is opened and stamped on the spot.
	@grep -q 'A:C900SET \. *off *off *on *off' $(SETLOG)-3.log \
		|| { echo "verify-set: FAIL -- [ACCESS=ON] did not switch create off"; exit 1; }
	@grep -q 'Update *Access' $(SETLOG)-3.log \
		|| { echo "verify-set: FAIL -- SDIR still thinks the drive stamps creates"; exit 1; }
	@test "`grep -c 'HELLO    C .*07/31/26 14:3' $(SETLOG)-3.log`" = 1 \
		|| { echo "verify-set: FAIL -- the access stamp is missing, or was there before the file was opened"; exit 1; }
	@# ---- 4: the refusals
	@grep -q 'Cannot have both create and access time stamps.' $(SETLOG)-4.log \
		|| { echo "verify-set: FAIL -- ACCESS+CREATE was not refused"; exit 1; }
	@grep -q 'Cannot set RO and RW.' $(SETLOG)-4.log \
		|| { echo "verify-set: FAIL -- RO+RW was not refused"; exit 1; }
	@grep -q 'Option only for drives.' $(SETLOG)-4.log \
		|| { echo "verify-set: FAIL -- a label option on a file was not refused"; exit 1; }
	@grep -q 'Option requires a file reference' $(SETLOG)-4.log \
		|| { echo "verify-set: FAIL -- an attribute option with no file was not refused"; exit 1; }
	@grep -q 'ERROR: Unrecognized option.' $(SETLOG)-4.log \
		|| { echo "verify-set: FAIL -- an unknown option was accepted"; exit 1; }
	@grep -q 'ERROR: This option needs a modifier.' $(SETLOG)-4.log \
		|| { echo "verify-set: FAIL -- a bare [ACCESS] was accepted"; exit 1; }
	@# ---- and now the disk itself, which is the only witness that does
	@# not share a BDOS with the program under test
	dd if=$(SETIMG) of=build/set-cpma.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tests/dirattr.py build/set-cpma.img > build/set-attrs.txt
	@grep 'HELLO.TXT\|TEST.SUB\|HELLO.C' build/set-attrs.txt
	@grep -q '^HELLO.TXT *R S A 1 3$$' build/set-attrs.txt \
		|| { echo "verify-set: FAIL -- on disk HELLO.TXT does not carry exactly R S A 1 3"; exit 1; }
	@grep -q '^TEST.SUB *2$$' build/set-attrs.txt \
		|| { echo "verify-set: FAIL -- on disk the wildcard did not set F2, and only F2"; exit 1; }
	@grep -q '^HELLO.C *A$$' build/set-attrs.txt \
		|| { echo "verify-set: FAIL -- on disk HELLO.C does not carry exactly A"; exit 1; }
	@grep -q '^BIG.TXT *$$' build/set-attrs.txt \
		|| { echo "verify-set: FAIL -- SET touched a file it was never given"; exit 1; }
	python3 tools/mkcpmfs.py --list build/set-cpma.img > build/set-after.txt
	@grep 'label\|^ 0 HELLO.C ' build/set-after.txt
	@grep -q 'label     C900SET      mode 0x41 \[access,exists\]' build/set-after.txt \
		|| { echo "verify-set: FAIL -- on disk the label is not C900SET with access-only stamping"; exit 1; }
	@grep -q '^ *0 HELLO.C .*2026-07-31 14:3' build/set-after.txt \
		|| { echo "verify-set: FAIL -- on disk HELLO.C has no access stamp"; exit 1; }
	@grep -q '128 SFCB entries' build/set-after.txt \
		|| { echo "verify-set: FAIL -- SFCBs damaged"; exit 1; }
	@echo "verify-set: PASS -- attributes on disk, label rename with its other bits kept, access stamping switched on and taken, seven refusals"

# ---- SET's coverage gaps, closed on a second drive letter ----
# verify-set above runs entirely on drive A:, in user area 0, with paging
# that never reaches a page break -- set.c's own author said so.  This
# target is the rest of it, and each subject needs something A: alone
# could not supply:
#
#   (a) SET ON DRIVE B:.  A drive that is not the current one, not the one
#	SET was loaded from, and DELIBERATELY A DIFFERENT SIZE, so a
#	wrong-DPB bug mis-sizes it instead of agreeing with itself.  The
#	stamped-drive paths need a stamped B:, which INITDIR would make on
#	target and which this system does not have -- so mkcpmfs.py
#	--initdir makes it host-side, exactly as it already makes A:.  The
#	UNSTAMPED refusal is checked too, on the stock cpmb.img.
#   (b) USER AREAS.  ccpuser.py puts one file on B: in user area 3 and SET on A:.
#	Transient runs in the area it was found in, so user-0 fallback scans user 0.
#   (c) [PAGE].  Thirty matching files is more than one 24-line page.  The
#	RETURN that answers the page prompt is fed past the emulator's
#	prompt gate (\g), which is the only way to send a keystroke to a
#	program that prints no `>' or `#' for the gate to latch on.
#   (d) THE BDOS ERROR CODES.  Code 2, Drive Read Only, is reachable now
#	that the read-only vector survives a warm boot -- this BDOS's
#	warmboot() no longer clears ro_dsk the way CP/M 2.2 did, matching
#	CP/M 3, where the vector is cleared only by function 13 or 37, not
#	by every transient exiting: SET sets the drive read-only with
#	function 28, and the NEXT SET is
#	refused by the BDOS before function 30 runs.  Code 4 is NOT
#	reachable, and session 6 shows why -- SET selects the drive with
#	function 14 outside return mode, as v3 does (set.plm:1426-1433),
#	so an absent drive takes the BDOS's own Abort/Retry/Continue
#	prompt first.  The session answers it.
#   (e) THE TABLE LIMITS.  181 names match one wildcard, which the old
#	128-entry expansion table silently truncated, and nine file specs,
#	one more than the old MAXSPEC of 8.  Both are counted ON DISK.
#
# The witness for everything that reaches the disk is the disk.  With two
# drives that is not enough on its own -- a check must also prove it read
# the drive it thinks it did -- so each region is asked for the file and
# the label only IT should have, and for the absence of the other's.
SETBLOG	 = build/verify-setb
SETBSRC	 = build/setb-src
SETBA	 = build/setb-a
SETBCPMA = build/setb-cpma.img
SETBCPMB = build/setb-cpmb.img
SETBMAX	 = 900000000
SETBIN1 = $(OSSEL)SET B:[NAME=C900SETB]\rSHOW B:[LABEL]\rSET B:BONLY.TXT [RO,SYS,F2=ON]\rSET A:HELLO.TXT B:READMEB.TXT [F4=ON]\rSET A:HELLO.TXT [F1=ON] B:READMEB.TXT [F2=ON]\r$(ENDIN)
SETBIN2 = $(OSSEL)B:\rUSER 3\rA:SETU3 U3FILE.TXT [RO,F1=ON]\rA:SETU3 BONLY.TXT [SYS]\rUSER 0\rA:SET U3FILE.TXT [SYS]\r$(ENDIN)
SETBIN3 = $(OSSEL)A:SET B:PAGE*.TXT [F3=ON]\r\g\r$(ENDIN)
SETBIN4 = $(OSSEL)A:SET B:*.TXT [NOPAGE,F4=ON]\rA:SET B:SPECA.TXT B:SPECB.TXT B:SPECC.TXT B:SPECD.TXT B:SPECE.TXT B:SPECF.TXT B:SPECG.TXT B:SPECH.TXT B:SPECI.TXT [NOPAGE,F2=ON]\r$(ENDIN)
SETBIN5 = $(OSSEL)SET [RO]\rPIP RONEW.TXT=HELLO.C\rSET HELLO.TXT [SYS]\rSET [RW]\rPIP RONEW.TXT=HELLO.C\rDIR RONEW.TXT\r$(ENDIN)
SETBIN6 = $(OSSEL)SET B:[NAME=NOPE]\rSET P:HELLO.TXT [RO]\r\gA\r$(ENDIN)

.PHONY: verify-setb
verify-setb: all
	@# ---- fixtures: a stamped, crowded B: and a copy of SET in user 3
	rm -rf $(SETBSRC) $(SETBA)
	mkdir -p $(SETBSRC)
	cp src/dist/disk-b/BONLY.TXT src/dist/disk-b/READMEB.TXT $(SETBSRC)
	python3 tools/setbfill.py $(SETBSRC)
	python3 tools/mkcpmfs.py --initdir --label $(LABELB) \
		--label-mode create,update $(SETBCPMB) $(CPMB_BLOCKS) $(SETBSRC)
	python3 tools/ccpuser.py $(SETBCPMB) U3FILE.TXT 3
	cp -r $(DISKA) $(SETBA)
	cp $(UOBJDIR)/SET.Z8K $(SETBA)/SETU3.Z8K
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(SETBCPMA) $(CPMA_BLOCKS) $(SETBA)
	python3 tools/ccpuser.py $(SETBCPMA) SETU3.Z8K 3
	for n in 1 2 3 4 5; do \
		$(MKDISK) build/setb-$$n.img \
			$(CPMSYS) $(SETBCPMA) $(SETBCPMB) || exit 1; done
	@# session 6 wants drive B: as the packer leaves it: UNSTAMPED
	$(MKDISK) build/setb-6.img $(CPMSYS) \
		$(SETBCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-1.img) \
		--rtc=$(SETRTC) --input='$(SETBIN1)' --max=$(SETBMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-2.img) \
		--rtc=$(SETRTC) --input='$(SETBIN2)' --max=$(SETBMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-3.img) \
		--rtc=$(SETRTC) --input='$(SETBIN3)' --max=$(SETBMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-3.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-4.img) \
		--rtc=$(SETRTC) --input='$(SETBIN4)' --max=$(SETBMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-4.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-5.img) \
		--rtc=$(SETRTC) --input='$(SETBIN5)' --max=$(SETBMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-5.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-6.img) \
		--rtc=$(SETRTC) --input='$(SETBIN6)' --max=$(SETBMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-6.log)
	@$(EMUOK)
	@# ---- 1: SET's stamped-drive paths, on the drive that is not A:
	@grep -q 'B:C900SETB\. *off *on *off *on' $(SETBLOG)-1.log \
		|| { echo "verify-setb: FAIL -- [NAME=] on B: did not write the label, or lost the create/update bits it was not asked about"; exit 1; }
	@grep -q '^C900SETB\. *off *on *on .*07/31/26 14:32' $(SETBLOG)-1.log \
		|| { echo "verify-setb: FAIL -- SHOW B: does not agree with SET, or fn 100 wrote no update stamp on B:"; exit 1; }
	@grep -q 'B:BONLY   \.TXT  set to system (SYS), Read Only (RO)' $(SETBLOG)-1.log \
		|| { echo "verify-setb: FAIL -- file attributes on B: were not set"; exit 1; }
	@grep -q 'A:HELLO   \.TXT  set to directory (DIR), Read Write (RW).*4' $(SETBLOG)-1.log \
		|| { echo "verify-setb: FAIL -- the A: half of a two-drive command did not run"; exit 1; }
	@grep -q 'B:READMEB \.TXT  set to directory (DIR), Read Write (RW).*4' $(SETBLOG)-1.log \
		|| { echo "verify-setb: FAIL -- the B: half of a two-drive command did not run (SET did not reselect between specs)"; exit 1; }
	@# options are GLOBAL: a second [ ] group is an error, not a file name
	@grep -q 'ERROR: Cannot set local options for file.' $(SETBLOG)-1.log \
		|| { echo "verify-setb: FAIL -- a second option group was not refused (its tokens were taken as file names)"; exit 1; }
	@grep -q '^FILE: READMEB.TXT' $(SETBLOG)-1.log \
		|| { echo "verify-setb: FAIL -- the local-options refusal did not name the file it stopped on"; exit 1; }
	@# ---- 2: user areas.  SET sees the area it RUNS in, and only that one
	@grep -q 'B:U3FILE  \.TXT  set to directory (DIR), Read Only (RO).*1' $(SETBLOG)-2.log \
		|| { echo "verify-setb: FAIL -- SET in user area 3 did not find the user-3 file"; exit 1; }
	@test "`grep -c 'ERROR:  File not found' $(SETBLOG)-2.log`" = 2 \
		|| { echo "verify-setb: FAIL -- SET crossed a user-area boundary (a user-0 file from user 3, or the reverse)"; exit 1; }
	@# ---- 3: [PAGE] stops, asks, and CARRIES ON when the RETURN arrives
	@test "`grep -c 'Press RETURN to continue.' $(SETBLOG)-3.log`" = 1 \
		|| { echo "verify-setb: FAIL -- 30 files over a 24-line page did not produce exactly one page break"; exit 1; }
	@test "`grep -c 'B:PAGE.*set to' $(SETBLOG)-3.log`" = 30 \
		|| { echo "verify-setb: FAIL -- paging swallowed or repeated lines"; exit 1; }
	@sed -n '/Press RETURN to continue./,$$p' $(SETBLOG)-3.log | grep -q 'B:PAGE30 ' \
		|| { echo "verify-setb: FAIL -- the RETURN did not resume the listing"; exit 1; }
	@# ---- 4: nine file specs on one command line
	@test "`grep -c 'B:SPEC. *\.TXT  set to directory (DIR), Read Write (RW).*24' $(SETBLOG)-4.log`" = 9 \
		|| { echo "verify-setb: FAIL -- a file spec past the eighth was dropped"; exit 1; }
	@# ---- 5: the read-only DRIVE, and the BDOS error code SET reports
	@grep -q 'Drive A: set to Read Only (RO)' $(SETBLOG)-5.log \
		|| { echo "verify-setb: FAIL -- [RO] on a drive did not report"; exit 1; }
	@grep -q '^ERROR: A: Drive Read Only' $(SETBLOG)-5.log \
		|| { echo "verify-setb: FAIL -- SET's bdoserror() did not report code 2"; exit 1; }
	@grep -q 'A: RONEW    TXT' $(SETBLOG)-5.log \
		|| { echo "verify-setb: FAIL -- [RW] did not put the drive back (fn 37)"; exit 1; }
	@# ---- 6: the two refusals that are not SET's own
	@grep -q 'Directory needs to be re-formatted for time/date stamps.' $(SETBLOG)-6.log \
		|| { echo "verify-setb: FAIL -- an unstamped B: accepted a stamping label (fn 100 did not return 0FFh)"; exit 1; }
	@grep -q 'CP/M Disk select error on drive P' $(SETBLOG)-6.log \
		|| { echo "verify-setb: FAIL -- an absent drive was not refused by the BDOS"; exit 1; }
	@# ================= and now the disk.  Both of them. =================
	dd if=build/setb-1.img of=build/setb-1-a.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	dd if=build/setb-1.img of=build/setb-1-b.img bs=512 skip=$(CPMB_BASEBLK) \
		count=$(CPMB_BLOCKS) status=none conv=sparse
	dd if=build/setb-2.img of=build/setb-2-b.img bs=512 skip=$(CPMB_BASEBLK) \
		count=$(CPMB_BLOCKS) status=none conv=sparse
	dd if=build/setb-4.img of=build/setb-4-b.img bs=512 skip=$(CPMB_BASEBLK) \
		count=$(CPMB_BLOCKS) status=none conv=sparse
	python3 tests/dirattr.py build/setb-1-b.img > build/setb-1-b.txt
	python3 tests/dirattr.py build/setb-1-a.img > build/setb-1-a.txt
	@grep 'BONLY\|READMEB' build/setb-1-b.txt
	@grep -q '^BONLY.TXT *R S 2$$' build/setb-1-b.txt \
		|| { echo "verify-setb: FAIL -- on disk B:BONLY.TXT does not carry exactly R S 2"; exit 1; }
	@grep -q '^READMEB.TXT *4$$' build/setb-1-b.txt \
		|| { echo "verify-setb: FAIL -- on disk B:READMEB.TXT does not carry exactly F4"; exit 1; }
	@# the A: half of the same command landed in A:'s region, and the B:
	@# half did NOT: an aliased DPB would put both in one place
	@grep -q '^HELLO.TXT *4$$' build/setb-1-a.txt \
		|| { echo "verify-setb: FAIL -- on disk A:HELLO.TXT does not carry exactly F4"; exit 1; }
	@if grep -q '^BONLY.TXT' build/setb-1-a.txt; then \
		echo "verify-setb: FAIL -- a B: file appears in A:'s region"; exit 1; fi
	@# the label went to B:, and A: still has its own
	python3 tools/mkcpmfs.py --list build/setb-1-b.img > build/setb-1-b.lst
	python3 tools/mkcpmfs.py --list build/setb-1-a.img > build/setb-1-a.lst
	@grep -q 'label     C900SETB     mode 0x31 \[update,create,exists\]' \
		build/setb-1-b.lst \
		|| { echo "verify-setb: FAIL -- on disk B:'s label is not C900SETB with create+update kept"; exit 1; }
	@grep -q 'label     C900A ' build/setb-1-a.lst \
		|| { echo "verify-setb: FAIL -- SET renamed A:'s label while it was working on B:"; exit 1; }
	@# user areas, from the bytes: area 3 holds one file and it changed,
	@# area 0's copy of the name SET refused is untouched
	@test "`python3 tests/dirattr.py build/setb-2-b.img --user 3 | wc -l`" = 1 \
		|| { echo "verify-setb: FAIL -- user area 3 does not hold exactly one file on disk"; exit 1; }
	@python3 tests/dirattr.py build/setb-2-b.img --user 3 | grep -q '^U3FILE.TXT *R 1$$' \
		|| { echo "verify-setb: FAIL -- on disk the user-3 file does not carry exactly RO+F1"; exit 1; }
	@python3 tests/dirattr.py build/setb-2-b.img --user 0 | grep -q '^BONLY.TXT *$$' \
		|| { echo "verify-setb: FAIL -- on disk a SET run in user area 3 changed a user-0 file"; exit 1; }
	@# the wildcard limit: EVERY match, counted on disk

# ---- CP/M 3 date and time stamping ----
# The BDOS half: stamps written by the running system, on the STAMPED
# drive A: the build produces.  Two cold boots on one image.
#
#  STAMPT  drives functions 101/102/104/105 from the program side, with
#	   the clock moved by function 104 so the run is deterministic
#	   whatever the RTC says: create the file at 14 Jun 1985 12:34,
#	   reopen and write it at 15 Jun 1985 08:00, and the create and
#	   update stamps must then be those two different minutes.
#  PIP	   the same thing through a stock DRI utility that knows nothing
#	   about stamping, with the clock seeded on the command line, and
#	   read back through SDIR [DATE] -- the rendering a user sees.
#
# Afterwards the partition is pulled out of the image and the stamps are
# checked on disk, which is the only way to prove they were written in
# 8080 byte order rather than merely read back consistently.
STAMPBIMG = build/stampbdos.bin
STAMPBLOG = build/verify-stamp
STAMPRTC = 2026-07-31T14:32:10
STAMPIN2 = $(OSSEL)PIP PIPSTAMP.TXT=HELLO.C\rSDIR [NOPAGE DATE] PIPSTAMP.TXT\rSHOW [LABEL]\r$(ENDIN)
.PHONY: verify-stamp
verify-stamp: all
	$(MKDISK) $(STAMPBIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(STAMPBIMG)) \
		--rtc=$(STAMPRTC) --input="$(OSSEL)STAMPT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(STAMPBLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(STAMPBIMG)) \
		--rtc=$(STAMPRTC) --input="$(STAMPIN2)" --max=$(EMUMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(STAMPBLOG)-2.log)
	@$(EMUOK)
	@grep -q 'STAMPT: PASS' $(STAMPBLOG)-1.log \
		|| { echo "verify-stamp: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'STAMPT: fn 101 label mode 31' $(STAMPBLOG)-1.log \
		|| { echo "verify-stamp: FAIL -- fn 101 label mode"; exit 1; }
	@grep -q 'STAMPT: fn 100 -> 0  fn 101 now 31' $(STAMPBLOG)-1.log \
		|| { echo "verify-stamp: FAIL -- fn 100 did not rewrite the label"; exit 1; }
	@grep -q 'PIPSTAMP TXT .* 07/31/26 14:3.*07/31/26 14:3' $(STAMPBLOG)-2.log \
		|| { echo "verify-stamp: FAIL -- SDIR [DATE] did not show PIP's stamps"; exit 1; }
	@grep -q 'Label for drive A:' $(STAMPBLOG)-2.log \
		|| { echo "verify-stamp: FAIL -- SHOW [LABEL]"; exit 1; }
	dd if=$(STAMPBIMG) of=build/stampb-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --list build/stampb-cpma.img \
		> build/stampb-after.txt
	@grep -q '128 SFCB entries' build/stampb-after.txt \
		|| { echo "verify-stamp: FAIL -- SFCBs damaged"; exit 1; }
	@grep 'label     $(LABEL)' build/stampb-after.txt
	@# fn 100 rewrote the label while STAMPT had the clock at DAY1, so
	@# the label's own UPDATE stamp is that minute and its CREATE stamp
	@# is still the packer's blank -- v3 stamps a label's create field
	@# only when it makes one (dirlbl.asm:120-137).
	@grep -q 'label     $(LABEL).*created -  updated 1985-06-14 12:34' \
		build/stampb-after.txt \
		|| { echo "verify-stamp: FAIL -- fn 100 label stamp"; exit 1; }
	@grep 'STAMPT.TXT' build/stampb-after.txt
	@grep -q 'STAMPT.TXT.*1985-06-14 12:34.*1985-06-15 08:00' build/stampb-after.txt \
		|| { echo "verify-stamp: FAIL -- on-disk stamps wrong (byte order?)"; exit 1; }
	@grep 'PIPSTAMP.TXT' build/stampb-after.txt
	@grep -q 'PIPSTAMP.TXT.*2026-07-31 14:3.*2026-07-31 14:3' build/stampb-after.txt \
		|| { echo "verify-stamp: FAIL -- PIP's file is not stamped on disk"; exit 1; }
	@echo "verify-stamp: PASS -- create and update stamps written by the BDOS, on disk in 8080 order"

# ---- CP/M 3 date stamps vs. DRI's own SFCB/XFCB byte layout ----
# verify-stamp only proves our reader can read back what our writer wrote;
# it never opens DRI's bdos30.asm/xfcb.lit and checks the bytes against
# them.  This target reimplements DRI's SFCB subfield arithmetic
# (bdos30.asm:3314-3327) and XFCB/label offsets (xfcb.lit) independently,
# straight off the raw directory bytes already produced by verify-stamp.
.PHONY: verify-stamp-dri
verify-stamp-dri: verify-stamp
	python3 tests/dri-sfcb-check.py build/stampb-cpma.img \
		"STAMPT|TXT|1985-06-14 12:34|1985-06-15 08:00" \
		"PIPSTAMP|TXT|2026-07-31 14:3|2026-07-31 14:3"
	@echo "verify-stamp-dri: PASS -- on-disk SFCB stamps match DRI's own byte layout"

# ---- BDOS function 99, truncate file ----
# The one operation that makes a file shorter, and the one CP/M 3's CCP
# consumes a submit file with.  TRUNCT writes a 300-record file (two
# directory entries, ten 4K blocks), cuts it to 100 records, and checks
# the size, the surviving data, the new end of file and -- the part that
# is easy to get wrong -- that the six freed blocks came back to the
# allocation vector.  Afterwards the partition is pulled out and the
# shortened file is read host-side, which is the independent check that
# the directory entries were rewritten correctly and not merely made to
# agree with themselves.
TRUNCIMG = build/trunctest.bin
TRUNCLOG = build/verify-trunc.log
.PHONY: verify-trunc
verify-trunc: all
	$(MKDISK) $(TRUNCIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TRUNCIMG)) \
		--input="$(OSSEL)TRUNCT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(TRUNCLOG))
	@$(EMUOK)
	@grep -q 'TRUNCT: PASS' $(TRUNCLOG) \
		|| { echo "verify-trunc: FAIL -- see the BAD lines above"; exit 1; }
	dd if=$(TRUNCIMG) of=build/trunc-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/trunc-fs
	python3 tools/mkcpmfs.py --extract build/trunc-cpma.img build/trunc-fs
	@test "`wc -c < build/trunc-fs/TRUNCT.TXT`" = 12800 \
		|| { echo "verify-trunc: FAIL -- the truncated file is not 100 records on disk"; exit 1; }
	@echo "verify-trunc: PASS -- fn 99 shortened the file and returned its blocks"

		--input="$(OSSEL)WILDT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
# ---- the sign-on prints once per cold boot ----
# bdosinit() (src/bdos/bdosmisc.c) prints the identification, and ccpif.s's
# `tsetb sysinit; jr mi, ccploop' latch (c900oses/cpm8000/ref/may83/ccp/ccpif.z8k:88-91)
# runs bdosinit() on the cold start only.  BIOS function 1 re-enters the
# CCP through ccpentry_ without reloading cpm.sys, so every warm boot
# must yield the prompt alone.  The session below runs four transient
# programs -- each one an exit through BDOS fn 0 -> warmboot() ->
# bwboot() -> bios(1) -- with built-ins mixed in, and the identification
# must still appear exactly once in the whole transcript.  The expected
# text comes from bdosinit()'s own prt_line() calls, so the check follows
# the banner's wording instead of pinning it.
SIGNIMG = build/signontest.bin
SIGNLOG = build/verify-signon.log
SIGNVERIFYIN = $(OSSEL)DIR M*.*\rMHELLO ONE\rTYPE HELLO.TXT\rSTAT HELLO.TXT\rMHELLO TWO\rDIR *.TXT\rBEEP 2\r$(ENDIN)
# Transient programs above (MHELLO, STAT, MHELLO, BEEP) = warm boots.
# All four run to completion: a program that stops at a prompt of its own
# (SDIR's pager, DDT) would never reach BDOS fn 0 and so never warm-boot.
SIGNWARMBOOTS = 4
.PHONY: verify-signon
verify-signon: all
	$(MKDISK) $(SIGNIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SIGNIMG)) \
		--input="$(SIGNVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(SIGNLOG))
	@$(EMUOK)
	@# The transients have to have actually run, or "printed once" would
	@# hold for a session that never warm-booted at all.
	@test "`grep -c 'MWC hello from the Coherent Z8001 pipeline' $(SIGNLOG)`" = 2 \
		|| { echo "verify-signon: FAIL -- MHELLO did not run twice"; exit 1; }
	@grep -q 'BEEP' $(SIGNLOG) \
		|| { echo "verify-signon: FAIL -- BEEP did not run"; exit 1; }
	@python3 tests/signoncount.py src/bdos/bdosmisc.c $(OBJDIR)/cpmver.h $(SIGNLOG) $(SIGNWARMBOOTS) \
		|| { echo "verify-signon: FAIL -- see the counts above"; exit 1; }
	@echo "verify-signon: PASS -- one sign-on, $(SIGNWARMBOOTS) warm boots"
# ---- the cold-start system identification (src/bdos/bdosmisc.c bdosinit) ----
# One cold boot, and the three lines the system introduces itself with are
# checked against the values THIS build was told to use -- not against
# whatever the image happens to say, which would pass for any string.
# tests/bannerchk.sh holds the assertions so the mutation gate below can run
# the same ones against broken builds.
BANIMG	= build/bannertest.bin
BANLOG	= build/verify-banner.log
.PHONY: verify-banner verify-banner-mutants
verify-banner: all
	$(MKDISK) $(BANIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(BANIMG)) \
		--input="$(OSSEL)DIR *.TXT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(BANLOG))
	@$(EMUOK)
	@sh tests/bannerchk.sh $(BANLOG) '$(CPMVER)' '$(CPMDATE)' '$(COPYYEAR)' \
		|| { echo "verify-banner: FAIL"; exit 1; }
	@echo "verify-banner: PASS -- version, date, the 1982 attribution and the contributors' line"

# The gate on the gate.  A check that cannot fail cannot be merged, so
# every assertion in bannerchk.sh is shown failing on a build that has the
# corresponding defect -- a stale version, a stale date, a stale year, a
# dropped line, a reordered banner.  Each mutant is a full rebuild and a
# full cold boot, which is slow and is the point: it proves the assertion
# reaches the running system, not a host-side string.
verify-banner-mutants:
	EMU='$(EMU)' KBOOT='$(KBOOT)' CPMSYS='$(CPMSYS)' \
	CPMAIMG='$(CPMAIMG)' CPMBIMG='$(CPMBIMG)' EMUMAX='$(EMUMAX)' CPMVER='$(CPMVER)' \
	CPMDATE='$(CPMDATE)' COPYYEAR='$(COPYYEAR)' \
		sh tests/bannermutate.sh

# ---- the four paths the stamping wave shipped without exercising ----
# Each of these needs a drive A: the build does not produce, so each one
# packs or edits its own cpma image first.  Every assertion that matters
# is made twice: once by the program on target, and once host-side over
# the bytes of the partition afterwards, with mkcpmfs.py --entries, which
# decodes each field in the 8080 order the format uses instead of asking
# the system to agree with itself.

# ---- ACCESS stamping (label bit 40h) ----
# The create stamp and the access stamp are the same four bytes and the
# label picks which; drive A: ships with create, so the access path at
# the head of open (bdos30.asm:4117-4120) is never taken anywhere else.
# The image here is relabelled access,update and ASTAMPT then does the
# one thing that tells the two apart -- an open with no write in it.
ASTAMPIMG = build/astampbdos.bin
ASTAMPCPMA = build/cpma-access.img
ASTAMPLOG = build/verify-stampa.log
.PHONY: verify-stampa
verify-stampa: all
	cp $(CPMAIMG) $(ASTAMPCPMA)
	python3 tools/mkcpmfs.py --label $(LABEL) --label-mode access,update \
		$(ASTAMPCPMA)
	$(MKDISK) $(ASTAMPIMG) $(CPMSYS) $(ASTAMPCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(ASTAMPIMG)) \
		--input="$(OSSEL)ASTAMPT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(ASTAMPLOG))
	@$(EMUOK)
	@grep -q 'ASTAMPT: fn 101 label mode 61' $(ASTAMPLOG) \
		|| { echo "verify-stampa: FAIL -- the drive is not labelled access,update"; exit 1; }
	@grep -q 'ASTAMPT: PASS' $(ASTAMPLOG) \
		|| { echo "verify-stampa: FAIL -- see the BAD lines above"; exit 1; }
	dd if=$(ASTAMPIMG) of=build/stampa-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --entries build/stampa-cpma.img \
		> build/stampa-after.txt
	@grep 'ASTAMPT' build/stampa-after.txt
	@# the access field is a day LATER than the update field, which
	@# create stamping cannot produce: only an open moved it, and the
	@# last open wrote nothing.
	@grep -q 'sfcb .*create 1985-06-16 07:07 update 1985-06-15 08:00' \
		build/stampa-after.txt \
		|| { echo "verify-stampa: FAIL -- the access stamp is not on disk"; exit 1; }
	@grep -q 'label C900A .*mode 0x61 \[access,update,exists\]' \
		build/stampa-after.txt \
		|| { echo "verify-stampa: FAIL -- label mode on disk"; exit 1; }
	@echo "verify-stampa: PASS -- an open with no write moved the access stamp"

# ---- function 100 making a directory label from nothing ----
# Only the rewrite half of func100 has ever run, because drive A: always
# arrives labelled.  Two images with no label: one WITH SFCBs, where the
# label is made and then rewritten a day later, and one without, where
# any mode with a stamping bit in it must be refused (bdos30.asm:4890-
# 4892).  The made-then-rewritten label is the discriminator: `created'
# at the first minute and `updated' at the second is a pair only the
# make path can leave, because a rewrite never touches byte 24.
NOLBLCPMA = build/cpma-nolabel.img
PLAINCPMA = build/cpma-plain.img
NOLBLIMG = build/lblnew.bin
PLAINIMG = build/lblnew-plain.bin
LBLLOG = build/verify-lblnew
.PHONY: verify-lblnew
verify-lblnew: all
	python3 tools/mkcpmfs.py --initdir $(NOLBLCPMA) $(CPMA_BLOCKS) $(DISKA)
	python3 tools/mkcpmfs.py $(PLAINCPMA) $(CPMA_BLOCKS) $(DISKA)
	@python3 tools/mkcpmfs.py --entries $(NOLBLCPMA) | grep -c ' label ' \
		| grep -qx 0 \
		|| { echo "verify-lblnew: FAIL -- the test image already has a label"; exit 1; }
	$(MKDISK) $(NOLBLIMG) $(CPMSYS) $(NOLBLCPMA)
	$(MKDISK) $(PLAINIMG) $(CPMSYS) $(PLAINCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(NOLBLIMG)) \
		--input="$(OSSEL)LBLNEW\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(LBLLOG)-1.log)
	@$(EMUOK)
	@# a second COLD boot of the same image: the login scan has to find
	@# a label that was written above the last file entry, which is the
	@# high-water-mark rule this wave fixed.
	{ $(EMUCD) && ./c900 --disk=$(abspath $(NOLBLIMG)) \
		--input="$(OSSEL)SHOW [LABEL]\rSDIR [NOPAGE DATE] LBLNEW.TXT\r$(ENDIN)" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } | tee $(abspath $(LBLLOG)-2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PLAINIMG)) \
		--input="$(OSSEL)LBLNEW NOSFCB\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(LBLLOG)-3.log)
	@$(EMUOK)
		--input="$(OSSEL)LBLNEW PWLABEL\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
	@grep -q 'LBLNEW: PASS' $(LBLLOG)-1.log \
		|| { echo "verify-lblnew: FAIL -- see the BAD lines above"; exit 1; }
		|| { echo "verify-lblnew: FAIL -- fn 100 did not make the label"; exit 1; }
	@grep -q 'Label for drive A:' $(LBLLOG)-2.log \
		|| { echo "verify-lblnew: FAIL -- the new label is invisible after a cold boot"; exit 1; }
	@grep -q 'C900N' $(LBLLOG)-2.log \
		|| { echo "verify-lblnew: FAIL -- SHOW [LABEL] found a different label"; exit 1; }
	@grep -q 'LBLNEW   TXT .* 06/14/85 12:34  06/14/85 12:34' $(LBLLOG)-2.log \
		|| { echo "verify-lblnew: FAIL -- SDIR [DATE] did not list the stamped file"; exit 1; }
	@grep -q 'LBLNEW: PASS' $(LBLLOG)-3.log \
		|| { echo "verify-lblnew: FAIL -- the no-SFCB image, see the BAD lines above"; exit 1; }
	dd if=$(NOLBLIMG) of=build/lblnew-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --entries build/lblnew-cpma.img \
		> build/lblnew-after.txt
	@grep ' label ' build/lblnew-after.txt
	@# The arrangement this is a test OF: the label has to be the
	@# highest-numbered entry on the drive, above every file, or the
	@# high water mark some file leaves would cover it and the cold-boot
	@# check above would pass without proving anything.
	@awk '/ label /{l=$$2} / file /{if ($$2+0 > f) f=$$2+0} \
	      END{ if (l+0 > f) exit 0; \
		   print "verify-lblnew: FAIL -- the label (entry " l \
			 ") is not above the last file entry (" f ")"; exit 1 }' \
		build/lblnew-after.txt || exit 1
	@# byte 24 stamped at the make, byte 28 at the rewrite a day later
	@grep -q 'label C900N .*mode 0x71 \[access,update,create,exists\] created 1985-06-14 12:34 updated 1985-06-15 08:00' \
		build/lblnew-after.txt \
		|| { echo "verify-lblnew: FAIL -- a made label must carry BOTH of its own stamps"; exit 1; }
	dd if=$(PLAINIMG) of=build/lblplain-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --entries build/lblplain-cpma.img \
		> build/lblplain-after.txt
	@# a drive with no SFCBs still stamps the label's OWN fields: they
	@# live in the label entry (bytes 24 and 28), not in an SFCB, and v3
	@# stamps them unconditionally once the mode check has passed
	@# (bdos30.asm:4907-4910, after the `ani 0111$$0000b' refusal).
	@grep -q 'label C900X .*mode 0x01 \[exists\] created 1985-06-14 12:34 updated 1985-06-14 12:34' \
		build/lblplain-after.txt \
		|| { echo "verify-lblnew: FAIL -- the no-SFCB label is wrong on disk"; exit 1; }
	@echo "verify-lblnew: PASS -- fn 100 made a label, stamped both its fields, and survived a cold boot"

# ---- function 99 landing exactly on a boundary ----
# TRUNCT cuts at record 100, in the middle of everything.  These four cuts
# land on the record boundaries the arithmetic in trunf() has to get
# exactly right, including one inside a LATER directory entry.  The size
# and free-space checks are on target; the encoding -- EX=2k with RC=80h,
# never EX=2k+1 with RC=0 -- can only be seen in the directory bytes, so
# it is checked host-side.
TRUNCBIMG = build/truncbtest.bin
TRUNCBLOG = build/verify-truncb.log
.PHONY: verify-truncb
verify-truncb: all
	$(MKDISK) $(TRUNCBIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TRUNCBIMG)) \
		--input="$(OSSEL)TRUNCB\r$(ENDIN)" --max=$(TRUNCBMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(TRUNCBLOG))
	@$(EMUOK)
	@grep -q 'TRUNCB: PASS' $(TRUNCBLOG) \
		|| { echo "verify-truncb: FAIL -- see the BAD lines above"; exit 1; }
	dd if=$(TRUNCBIMG) of=build/truncb-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/truncb-fs
	python3 tools/mkcpmfs.py --extract build/truncb-cpma.img build/truncb-fs
	@python3 tools/mkcpmfs.py --entries build/truncb-cpma.img \
		> build/truncb-after.txt
	@grep 'TRUNCB' build/truncb-after.txt
	@# 384 records = a full directory entry (EX=1 RC=80h) plus half of
	@# the next one (EX=2 RC=80h, four of its eight blocks)
	@grep -q 'file  TRUNCB1 .TXT user  0 ex 0x01 s1 0x00 s2 0x00 rc 0x80 nblk 8' \
		build/truncb-after.txt \
		|| { echo "verify-truncb: FAIL -- TRUNCB1 first entry"; exit 1; }
	@grep -q 'file  TRUNCB1 .TXT user  0 ex 0x02 s1 0x00 s2 0x00 rc 0x80 nblk 4' \
		build/truncb-after.txt \
		|| { echo "verify-truncb: FAIL -- TRUNCB1 boundary entry (EX/RC encoding?)"; exit 1; }
	@test "`grep -c 'file  TRUNCB1 ' build/truncb-after.txt`" = 2 \
		|| { echo "verify-truncb: FAIL -- TRUNCB1 has the wrong number of entries"; exit 1; }
	@grep -q 'file  TRUNCB2 .TXT user  0 ex 0x01 s1 0x00 s2 0x00 rc 0x80 nblk 8' \
		build/truncb-after.txt \
		|| { echo "verify-truncb: FAIL -- TRUNCB2 (256 records must be EX=1 RC=80h)"; exit 1; }
	@test "`grep -c 'file  TRUNCB2 ' build/truncb-after.txt`" = 1 \
		|| { echo "verify-truncb: FAIL -- TRUNCB2 kept an entry past the cut"; exit 1; }
	@grep -q 'file  TRUNCB3 .TXT user  0 ex 0x00 s1 0x00 s2 0x00 rc 0x80 nblk 4' \
		build/truncb-after.txt \
		|| { echo "verify-truncb: FAIL -- TRUNCB3 (128 records must be EX=0 RC=80h)"; exit 1; }
	@grep -q 'file  TRUNCB4 .TXT user  0 ex 0x00 s1 0x00 s2 0x00 rc 0x01 nblk 1' \
		build/truncb-after.txt \
		|| { echo "verify-truncb: FAIL -- TRUNCB4 (one record)"; exit 1; }
	@test "`wc -c < build/truncb-fs/TRUNCB1.TXT`" = 49152 \
		|| { echo "verify-truncb: FAIL -- TRUNCB1 is not 384 records on disk"; exit 1; }
	@test "`wc -c < build/truncb-fs/TRUNCB2.TXT`" = 32768 \
		|| { echo "verify-truncb: FAIL -- TRUNCB2 is not 256 records on disk"; exit 1; }
	@test "`wc -c < build/truncb-fs/TRUNCB3.TXT`" = 16384 \
		|| { echo "verify-truncb: FAIL -- TRUNCB3 is not 128 records on disk"; exit 1; }
	@test "`wc -c < build/truncb-fs/TRUNCB4.TXT`" = 128 \
		|| { echo "verify-truncb: FAIL -- TRUNCB4 is not 1 record on disk"; exit 1; }
	@echo "verify-truncb: PASS -- fn 99 lands on a logical extent, a directory entry and a block boundary"

# ---- function 99 on a SPARSE file ----
# The one place where the record number and the disk map disagree.  v3
# recomputes the surviving entry's extent from the map -- get$dir$ext
# (c900oses/cpm8000/ref/cpm3/bdos30.asm:1347-1381) walks it backwards for the last non-zero
# block -- and func99 applies set$rc3 (bdos30.asm:1932-1935) when that
# comes out below the extent the record number named, or when the map is
# empty (bdos30.asm:4836-4844).  A DENSE file has no zero in its map before
# the end, so the two agree and TRUNCT/TRUNCB cannot tell them apart; a
# file with holes is where they part.  TRUNCS makes holes with function 34
# and cuts inside them.  The sizes function 35 reports are checked on the
# machine, the directory bytes host-side: the whole point of the two
# divergent cases is an EX/RC pair the record number would never produce.
TRUNCSIMG = build/truncstest.bin
TRUNCSLOG = build/verify-truncs.log
.PHONY: verify-truncs
verify-truncs: all
	$(MKDISK) $(TRUNCSIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TRUNCSIMG)) \
		--input="$(OSSEL)TRUNCS\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(TRUNCSLOG))
	@$(EMUOK)
	@grep -q 'TRUNCS: PASS' $(TRUNCSLOG) \
		|| { echo "verify-truncs: FAIL -- see the BAD lines above"; exit 1; }
	dd if=$(TRUNCSIMG) of=build/truncs-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --entries build/truncs-cpma.img \
		> build/truncs-after.txt
	@grep 'TRUNCS[123]' build/truncs-after.txt
	@# the cut is in logical extent 1, the last surviving block is in
	@# logical extent 0, so the entry names 0 and set$$rc3 puts RC at 80h
	@grep -q 'file  TRUNCS1 .TXT user  0 ex 0x00 s1 0x00 s2 0x00 rc 0x80 nblk 1' \
		build/truncs-after.txt \
		|| { echo "verify-truncs: FAIL -- TRUNCS1 (extent not recomputed from the disk map?)"; exit 1; }
	@# nothing survives in the map at all: set$$rc3 with dminx = 0
	@grep -q 'file  TRUNCS2 .TXT user  0 ex 0x00 s1 0x00 s2 0x00 rc 0x00 nblk 0' \
		build/truncs-after.txt \
		|| { echo "verify-truncs: FAIL -- TRUNCS2 (an empty map must be RC = 0)"; exit 1; }
	@# the control: the map still reaches the cut's own logical extent,
	@# so set$$rc3 must NOT fire and the answer is the dense one
	@grep -q 'file  TRUNCS3 .TXT user  0 ex 0x01 s1 0x00 s2 0x00 rc 0x17 nblk 2' \
		build/truncs-after.txt \
		|| { echo "verify-truncs: FAIL -- TRUNCS3 (the extent was lowered when it must not be)"; exit 1; }
	@test "`grep -c 'file  TRUNCS1 ' build/truncs-after.txt`" = 1 \
		|| { echo "verify-truncs: FAIL -- TRUNCS1 has the wrong number of entries"; exit 1; }
	@echo "verify-truncs: PASS -- fn 99 takes the surviving extent from the disk map, not the record number"

# ---- an XFCB (type 10h..1Fh) actually on the disk ----
# The login scan must raise the high water mark for one and allocate no
# block from one: its sixteen trailing bytes are a password, not a disk
# map.  Two runs of the same program, one on an image carrying an XFCB
# for HELLO.C and one on the image without it, and function 46 has to
# report the same free space on both -- a scan that walked those bytes
# would mark blocks in use that no file owns.
#
# The password is not arbitrary.  Read as four little-endian block
# numbers, `4Z3Z2Z1X' (XOR key 30h) is blocks 360, 618, 874 and 1130 --
# four distinct free blocks well inside DSM.  Most passwords decode to
# numbers PAST DSM, and free$sp only counts bits below DSM, so a login
# scan that walked them would corrupt memory beyond the allocation
# vector while reporting the free space unchanged.  This one makes the
# same defect show up as 128 records of missing space instead, which is
# the difference between a check that can fail and one that cannot.
XFCBCPMA = build/cpma-xfcb.img
XFCBIMG = build/xfcbtest.bin
XFCBCTLIMG = build/xfcbctl.bin
XFCBLOG = build/verify-xfcb
.PHONY: verify-xfcb
verify-xfcb: all
	cp $(CPMAIMG) $(XFCBCPMA)
	python3 tools/mkcpmfs.py --xfcb HELLO.C:0xe0:4Z3Z2Z1X $(XFCBCPMA)
	@python3 tools/mkcpmfs.py --entries $(XFCBCPMA) | grep ' xfcb ' \
		> build/xfcb-before.txt
	@cat build/xfcb-before.txt
	$(MKDISK) $(XFCBIMG) $(CPMSYS) $(XFCBCPMA)
	$(MKDISK) $(XFCBCTLIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XFCBIMG)) \
		--input="$(OSSEL)XFCBT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(XFCBLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XFCBCTLIMG)) \
		--input="$(OSSEL)XFCBT NONE\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(XFCBLOG)-2.log)
	@$(EMUOK)
	@grep -q 'XFCBT: PASS' $(XFCBLOG)-1.log \
		|| { echo "verify-xfcb: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'XFCBT: PASS' $(XFCBLOG)-2.log \
		|| { echo "verify-xfcb: FAIL -- the control run"; exit 1; }
	@test "`tr -d '\r' < $(XFCBLOG)-1.log | grep '^XFCBT: free '`" \
	    = "`tr -d '\r' < $(XFCBLOG)-2.log | grep '^XFCBT: free '`" \
		|| { echo "verify-xfcb: FAIL -- an XFCB changed the free space (blocks allocated from a password?)"; exit 1; }
	dd if=$(XFCBIMG) of=build/xfcb-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --entries build/xfcb-cpma.img | grep ' xfcb ' \
		> build/xfcb-after.txt
	@cat build/xfcb-after.txt
	@cmp -s build/xfcb-before.txt build/xfcb-after.txt \
		|| { echo "verify-xfcb: FAIL -- the XFCB did not survive the session byte for byte"; exit 1; }
	@echo "verify-xfcb: PASS -- an XFCB costs no space, is reachable and is left alone"

# ---- the two oldest sessions, and what they are allowed to conclude ----
# These are the smoke tests a person runs first, so they are the worst
# place for a check that cannot fail -- which is what they were until the
# assertions below were added: they patched a disk, booted, piped the
# transcript into tee and stopped, and make saw only tee.
#
# The assertions live in tests/verifychk.sh, in the shape
# tests/bannerchk.sh uses, so that they can be run against a doctored
# transcript to show each one failing.  Three things are checked here in
# the recipe instead, because they need values only make has:
# bannerchk.sh compares the sign-on against the version, date and year
# THIS build was told to use, and signoncount.py reads the banner out of
# bdosinit() and requires it exactly once across the session's warm
# boots.  reverify gets neither: it boots the image verify left behind,
# which may have been built on an earlier day, so a date comparison
# there would be measuring the calendar (tests/verifychk.sh checks the
# sign-on structurally instead).
#
# Transient programs in VERIFYIN, each an exit through BDOS fn 0 and so
# a warm boot: MHELLO, MHELLO, BEEP, FCOPY, FCOPY, STAT, PIP.  DIR and
# TYPE are CCP built-ins and warm-boot nothing, and DDT stops at its own
# prompt without ever reaching fn 0, so it is not counted.
VERIFYWARMBOOTS = 7
.PHONY: verify reverify
verify: all
	$(MKDISK) $(TESTIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TESTIMG)) \
		--input="$(VERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath build/verify.log)
	@$(EMUOK)
	@sh tests/bannerchk.sh build/verify.log '$(CPMVER)' '$(CPMDATE)' \
		'$(COPYYEAR)' || { echo "verify: FAIL -- the sign-on"; exit 1; }
	@python3 tests/signoncount.py src/bdos/bdosmisc.c $(OBJDIR)/cpmver.h \
		build/verify.log $(VERIFYWARMBOOTS) \
		|| { echo "verify: FAIL -- see the counts above"; exit 1; }
	@sh tests/verifychk.sh verify build/verify.log \
		|| { echo "verify: FAIL"; exit 1; }
	@echo "verify: PASS -- cold boot, one sign-on across $(VERIFYWARMBOOTS) warm boots,"
	@echo "        a transient with its command tail, the bell, a copy that TYPEs"
	@echo "        back identical, a reported failure, and STAT, PIP and DDT"

reverify:
	@test -f $(TESTIMG) || { echo "reverify: run make verify first"; exit 1; }
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TESTIMG)) \
		--input="$(REVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath build/reverify.log)
	@$(EMUOK)
	@sh tests/verifychk.sh reverify build/reverify.log \
		|| { echo "reverify: FAIL"; exit 1; }
	@echo "reverify: PASS -- both files the first session wrote survived the"
	@echo "          cold boot, and one of them still reads back byte for byte"

# ED interactive session on a fresh patched copy of the dist image:
# insert three lines, save, TYPE, re-enter, append a line, save, TYPE.
# Passes when the appended line shows up in the second TYPE (it appears
# once as insert-mode echo and once typed back -- grep -c 2).
.PHONY: verify-ed verify-arx verify-legacy
verify-ed: all
	$(MKDISK) build/edtest.bin $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/edtest.bin) \
		--input="$$(printf '$(EDVERIFYFMT)')" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath build/verify-ed.log)
	@$(EMUOK)
	@test "$$(grep -c 'appended fourth line' build/verify-ed.log)" = 2 \
		&& echo "verify-ed: PASS" \
		|| { echo "verify-ed: FAIL -- appended line not typed back"; exit 1; }

# Split dev tools actually executing (not just scan-validated): ASZ8K,
# XCON, XDUMP, AR8K on a fresh patched copy.  After the session the cpma
# partition is pulled out of the image and the AR8K-extracted member is
# compared byte-for-byte against the copy PIP made of the original.
verify-arx: all
	$(MKDISK) build/arxtest.bin $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/arxtest.bin) \
		--input="$(ARXVERIFYIN)" --max=$(ARXMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath build/verify-arx.log)
	@$(EMUOK)
	@grep -q 'magic = EE03 nseg = 1' build/verify-arx.log \
		|| { echo "verify-arx: FAIL -- XDUMP header dump missing"; exit 1; }
	@grep -q 'extracting:     mini.o' build/verify-arx.log \
		|| { echo "verify-arx: FAIL -- AR8K extract missing"; exit 1; }
	dd if=build/arxtest.bin of=build/arx-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/arx-fs
	python3 tools/mkcpmfs.py --extract build/arx-cpma.img build/arx-fs
	@cmp build/arx-fs/MINI.O build/arx-fs/MINIORIG.O \
		&& echo "verify-arx: PASS -- extracted member byte-identical" \
		|| { echo "verify-arx: FAIL -- extracted member differs"; exit 1; }

# ---- row 11 ("stock CP/M-8000 1.3 binaries keep running") ----
# One session over every binary in vendor/z8001mb/cpm8k/packages/base/
# (vendor/SOURCES) not already exercised by another target: PIP and STAT
# against HELLO.C (which `all' stages), and DUMP against the same file
# for a known-bytes check.  ED, DDT and LD8K are already proven by
# verify-ed, verify and selfhost; this target does not repeat them.
# privileged-instruction TRAP; fixing the split-I/D fast path they
# strictly rather than recording it.
LEGIMG	= build/legacytest.bin
LEGLOG	= build/verify-legacy.log
LEGIN	= $(OSSEL)PIP LEGCOPY.TXT=HELLO.C\rSTAT LEGCOPY.TXT\rDUMP HELLO.C\rNMZ8K STARTUP.O\rSIZEZ8K MHELLO.Z8K\rASZ8K MINI.8KN\rXCON -o MINI.O MINI.OBJ\rXDUMP MINI.O\rAR8K rv TEST.A MINI.O\r$(ENDIN)
verify-legacy: all
	$(MKDISK) $(LEGIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(LEGIMG)) \
		--input="$(LEGIN)" --max=$(ARXMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(LEGLOG))
	@$(EMUOK)
	dd if=$(LEGIMG) of=build/leg-cpma.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/leg-fs
	python3 tools/mkcpmfs.py --extract build/leg-cpma.img build/leg-fs
	@sh tests/legacychk.sh $(LEGLOG) build/leg-fs > build/verify-legacy-summary.txt \
		|| { cat build/verify-legacy-summary.txt; echo "verify-legacy: FAIL"; exit 1; }
	@cat build/verify-legacy-summary.txt

# ---- CCP options session (verify-ccp): search path, named directories,
# IF/ELSE/FI in submit files and the error handler, all driven from
# CCP.CFG.  Drive A: is packed from $(DISKA)/ PLUS src/dist/disk-ccp/ (the config
# file and the submit files), so the images every other target builds
# stay stock -- with no CCP.CFG the CCP behaves exactly as before.
# U5HELLO.Z8K (a copy of MHELLO) and U5ONLY.TXT are moved into user area
# 5 after packing: nothing on the running system can write there, since
# a transient found by the user-area fallback runs *in* the user area it
# was found in.
CCPFS	= build/ccpfs
CCPIMG	= build/ccp-cpma.img
CCPTEST	= build/ccptest.bin
CCPLOG	= build/verify-ccp.log
# Session: a command resolved through the path from user area 5; a
# named directory as a file prefix; a named directory as a command
# (drive + user change); a submit file taking the IF branch and another
# taking the ELSE branch; an unresolvable command handed to the handler;
# and finally the SUBMIT built-in reached by its bare name, which is the
# first submit of the session and so the case the built-in used to drop
# on the floor (see the SUBCMD comment in src/ccp/ccp.c).
.PHONY: verify-ccp
verify-ccp: all
	rm -rf $(CCPFS)
	mkdir -p $(CCPFS)
	cp $(DISKA)/* src/dist/disk-ccp/* $(CCPFS)/
	cp $(UOBJDIR)/MHELLO.Z8K $(CCPFS)/U5HELLO.Z8K
	python3 tools/mkcpmfs.py $(CCPIMG) $(CPMA_BLOCKS) $(CCPFS)
	python3 tools/ccpuser.py $(CCPIMG) U5HELLO.Z8K 5 U5ONLY.TXT 5
	$(MKDISK) $(CCPTEST) $(CPMSYS) $(CCPIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CCPTEST)) \
		--input="$(CCPVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CCPLOG))
	@$(EMUOK)
	@grep -q 'arg 1: PATHOK' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- path did not reach user area 5"; exit 1; }
	@test "$$(grep -c 'found it through a named directory' $(CCPLOG))" = 2 \
		|| { echo "verify-ccp: FAIL -- named directory not used twice"; exit 1; }
	@grep -q '5A>' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- NAME: did not change user area"; exit 1; }
	@grep -q 'the BDOS directory + extent walk works' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- IF ERROR branch not taken"; exit 1; }
	@grep -q 'Stock DRI utilities' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- ELSE branch not taken"; exit 1; }
	@grep -q 'arg 1: NESTOK' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- nested IF EXIST / IF ~EXIST"; exit 1; }
	@grep -q 'CCP ERROR HANDLER: BADCMD IS NOT A COMMAND' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- error handler did not run"; exit 1; }
	@grep -q 'arg 1: BADCMD' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- failing line not passed to the handler"; exit 1; }
	@# The SUBMIT built-in, typed bare, on the session's first submit:
	@# the batch must run with $$1/$$2 substituted and must survive the
	@# warm boots between its lines (each MHELLO is a transient).
	@grep -q 'arg 2: SECOND' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- bare SUBMIT did not run the batch"; exit 1; }
	@grep -q 'arg 1: TWO' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- the batch stopped before its last line"; exit 1; }
	@echo "verify-ccp: PASS"

# ---- the user-0 fallback, where DRI puts it (verify-user0) ----
# The deviation this closes was architectural: we did the fallback in the
# CCP, so a transient found in user 0 also RAN in user 0 and could not see
# the files of the area the user was actually in.  v3 does it in the BDOS
# -- search$user0 in function 15 (c900oses/cpm8000/ref/cpm3/bdos30.asm:3940-3974), which
# never touches the user number, and v3's CCP restores it before loading
# for exactly that reason (ccp3.asm:1304-1305).
#
# The rule is NOT "look in user 0".  It is "look in user 0 and keep the
# file only if it is a SYS file" (:4006-4015), so the fixture is a matched
# pair on one drive: SET.Z8K carries SYS and must be reachable from user
# area 3, SDIR.Z8K does not and must not be.  One bit is the whole
# difference between the two, which is what makes each check able to fail.
#
# Two witnesses, because the transcript alone cannot separate "SET ran in
# user 3" from "SET ran in user 0 and user 0 happens to have that file":
#
#   U0T	    the BDOS interface directly, from a program that is ALREADY in
#	    user area 3 -- so the open, the SYS rule, the read-only rule and
#	    the next-DIRECTORY-ENTRY read are measured without the CCP in the
#	    way.  That last record is 258, not the obvious 200: A: folds two
#	    extents into one entry (EXM 1, BLS 4096, src/bios/bios900.c:145-150),
#	    so a record below 256 needs no second directory search and cannot
#	    tell a persisted flag from a lost one -- the mutation gate caught
#	    exactly that.
#   the disk    every attribute SET wrote, decoded host-side per user area
#	    (tests/dirattr.py --user N).  A file that changed in the wrong
#	    area, or did not change in the right one, fails here even
#	    though the session's own output would read as a success.
#
# Drive identity: A: is the only drive the session writes, so the A:
# decode is asked for a name that exists only on B: (BONLY.TXT) as well as
# for its own -- a slice taken at the wrong offset fails on both halves.
U0FS	= build/u0fs
U0CPMA	= build/u0-cpma.img
U0IMG	= build/u0test.bin
U0LOG	= build/verify-user0
# Session 1, in user area 3:
#   U0T			the BDOS rule, measured from inside user area 3
#   SET U3ONLY.TXT	SET lives ONLY in user 0.  It has to be found there,
#			run here, and see a file that exists only here
#   SDIR [NOPAGE]	control: same drive, same user 0, NO SYS bit: CCP must not resolve it.
#   SET U0PLAIN.TXT	the reverse boundary: user 0's own file is invisible
#			from here
# then USER 0 and the same SET, which must work, so that the file's
# attributes on disk say which area each command reached.
U0IN = $(OSSEL)USER 3\rU0T\rSET U3ONLY.TXT [RO,F1=ON]\rSDIR [NOPAGE]\rSET U0PLAIN.TXT [F2=ON]\rUSER 0\rSET U0PLAIN.TXT [F3=ON]\r$(ENDIN)
.PHONY: verify-user0
verify-user0: all
	rm -rf $(U0FS)
	mkdir -p $(U0FS)
	cp $(DISKA)/* $(U0FS)/
	python3 tools/u0fill.py $(U0FS)
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(U0CPMA) $(CPMA_BLOCKS) $(U0FS)
	@# SET is shared out of user 0 (SYS); SDIR is the control that is not
	python3 tools/ccpuser.py $(U0CPMA) SET.Z8K 0S SDIR.Z8K 0 \
		U0SHARE.TXT 0S U0PLAIN.TXT 0 U3ONLY.TXT 3 U0T.Z8K 3
	$(MKDISK) $(U0IMG) $(CPMSYS) $(U0CPMA) \
		$(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(U0IMG)) \
		--rtc=$(SETRTC) --input='$(U0IN)' --max=$(EMUMAX) $(EMUIDLE) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(U0LOG).log)
	@$(EMUOK)
	@# ---- the BDOS rule, from a program that did not need it to run
	@grep -q 'U0T: PASS' $(U0LOG).log \
		|| { echo "verify-user0: FAIL -- U0T: see its BAD lines above"; exit 1; }
	@grep -q 'U0T: rec 258 00 \[0258\]' $(U0LOG).log \
		|| { echo "verify-user0: FAIL -- the second DIRECTORY ENTRY of a user-0 file did not read: the fallback flag did not survive into the next-entry open"; exit 1; }
	@grep -q 'U0T: user 3 after' $(U0LOG).log \
		|| { echo "verify-user0: FAIL -- the user number moved"; exit 1; }
	@# ---- the CCP path: found in user 0, RUN in user 3
	@grep -q 'A:U3ONLY  \.TXT  set to directory (DIR), Read Only (RO)' $(U0LOG).log \
		|| { echo "verify-user0: FAIL -- a transient found in user 0 did not see user area 3's own file"; exit 1; }
	@grep -q 'SDIR?' $(U0LOG).log \
		|| { echo "verify-user0: FAIL -- a user-0 file WITHOUT SYS was shared out of user 0"; exit 1; }
	@test "`grep -c 'ERROR:  File not found' $(U0LOG).log`" = 1 \
		|| { echo "verify-user0: FAIL -- user area 0's own file was visible from user 3 (or user 0 lost sight of it)"; exit 1; }
	@# ---- and now the disk, per user area
	dd if=$(U0IMG) of=build/u0-after.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tests/dirattr.py build/u0-after.img --user 3 > build/u0-u3.txt
	python3 tests/dirattr.py build/u0-after.img --user 0 > build/u0-u0.txt
	@cat build/u0-u3.txt
	@grep -q '^U3ONLY.TXT *R 1$$' build/u0-u3.txt \
		|| { echo "verify-user0: FAIL -- on disk the user-3 file does not carry exactly R 1"; exit 1; }
	@grep -q '^U0PLAIN.TXT *3$$' build/u0-u0.txt \
		|| { echo "verify-user0: FAIL -- on disk the user-0 file does not carry exactly F3: F2 leaked across from the user-3 command, or the user-0 command never ran"; exit 1; }
	@grep -q '^SET.Z8K *S$$' build/u0-u0.txt \
		|| { echo "verify-user0: FAIL -- SET.Z8K is not the SYS file the fixture packed"; exit 1; }
	@grep -q '^BONLY.TXT' build/u0-u0.txt \
		&& { echo "verify-user0: FAIL -- this is drive B:, not drive A:"; exit 1; } || true
	@echo "verify-user0: PASS -- found in user 0, run in user 3, SYS required, read-only, two directory entries"

# ---- the transient CCP (src/ccp/ccprun.c, src/ccp/ccpsv.h, src/ccp/ccpcrt.s) ----
# verify-ccp proves the CCP's FEATURES.  This target proves the one
# property that only matters because the CCP is now a transient: state
# that used to survive because nothing ever reloaded the CCP has to
# survive a reload.  Two boots of the same image.
#
# Boot 1, three things that each span at least one reload:
#
#   SUBMIT CHAINT DEEP -- CHAINT.SUB runs MHELLO (a transient, so the
#     system warm-boots and re-reads A:CCP.Z8K) on its 1st, 3rd and 7th
#     lines, with an IF/ELSE around the middle one.  For its last line
#     to run at all, the open .SUB FCB and its record position have to
#     come back (sv_subfcb/sv_sub_index/sv_subdma); for `$1' to still
#     expand, the command that started the submit has to come back
#     (sv_save_sub); and for the ELSE branch to stay dead, the IF stack
#     has to come back (sv_fstk/sv_fdep) -- that last one is ours, not
#     CP/M 3's: IF/ELSE/FI is an extension v3 never had, so nothing in
#     v3's own design says where its stack should live across a reload;
#     it was decided to go in the CCP state page next to the other
#     fields that survive a reload the same way.
#
#   MHELLO C1!MHELLO C2!MHELLO C3 -- the `!'-chained console line, which
#     is what CP/M 3 puts in an RSX page (ccp3.asm:97,178) and we put in
#     the state page (sv_usercmd/sv_user_ptr).  Two reloads, and the
#     third command has to be found after both.
#
# Boot 2, the same image with A:CCP.Z8K removed: the system must say so
# and stop, which is the check that the CCP really is being loaded from
# that file and not, say, still linked into CPM.SYS.
CCPTFS	 = build/ccptfs
CCPTNOFS = build/ccptnofs
CCPTIMG	 = build/ccpt-cpma.img
CCPTNOIMG = build/ccptno-cpma.img
CCPTTEST = build/ccpttest.bin
CCPTNOTEST = build/ccptnotest.bin
CCPTLOG	 = build/verify-ccpt.log
CCPTNOLOG = build/verify-ccpt-nofile.log
CCPTIN	 = $(OSSEL)DIR CCP.*\rSUBMIT CHAINT DEEP\rMHELLO C1!MHELLO C2!MHELLO C3\r
.PHONY: verify-ccpt
verify-ccpt: all
	rm -rf $(CCPTFS) $(CCPTNOFS)
	mkdir -p $(CCPTFS)
	cp $(DISKA)/* src/dist/disk-ccp/* $(CCPTFS)/
	python3 tools/mkcpmfs.py $(CCPTIMG) $(CPMA_BLOCKS) $(CCPTFS)
	$(MKDISK) $(CCPTTEST) $(CPMSYS) $(CCPTIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CCPTTEST)) \
		--input="$(CCPTIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CCPTLOG))
	@$(EMUOK)
	@grep -q 'CCP      Z8K' $(CCPTLOG) \
		|| { echo "verify-ccpt: FAIL -- A:CCP.Z8K is not on the drive"; exit 1; }
	@grep -q 'arg 1: T1' $(CCPTLOG) \
		|| { echo "verify-ccpt: FAIL -- the submit file did not start"; exit 1; }
	@grep -q 'arg 1: T2' $(CCPTLOG) \
		|| { echo "verify-ccpt: FAIL -- the IF branch did not run after a reload"; exit 1; }
	@grep -q 'arg 1: T3' $(CCPTLOG) \
		|| { echo "verify-ccpt: FAIL -- the .SUB record position did not survive a CCP reload"; exit 1; }
	@test "`grep -c 'arg 1: TBAD' $(CCPTLOG)`" = 0 \
		|| { echo "verify-ccpt: FAIL -- the ELSE branch ran: the IF stack did not survive a CCP reload"; exit 1; }
	@test "`grep -c 'arg 2: DEEP' $(CCPTLOG)`" = 3 \
		|| { echo "verify-ccpt: FAIL -- \$$1 stopped expanding: the submitting command line did not survive a CCP reload"; exit 1; }
	@grep -q 'arg 1: C2!MHELLO' $(CCPTLOG) \
		|| { echo "verify-ccpt: FAIL -- the 2nd !-chained command did not survive a CCP reload"; exit 1; }
	@grep -q 'arg 1: C3' $(CCPTLOG) \
		|| { echo "verify-ccpt: FAIL -- the 3rd !-chained command did not survive two CCP reloads"; exit 1; }
	cp -r $(CCPTFS) $(CCPTNOFS)
	rm -f $(CCPTNOFS)/CCP.Z8K
	python3 tools/mkcpmfs.py $(CCPTNOIMG) $(CPMA_BLOCKS) $(CCPTNOFS)
	$(MKDISK) $(CCPTNOTEST) $(CPMSYS) $(CCPTNOIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CCPTNOTEST)) \
		--input="$(OSSEL)DIR\r" --max=100000000 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CCPTNOLOG))
	@$(EMUOK)
	@grep -q 'Cannot load A:CCP.Z8K' $(CCPTNOLOG) \
		|| { echo "verify-ccpt: FAIL -- a missing CCP.Z8K was not reported: the CCP is not being loaded from the file"; exit 1; }
	@test "`grep -c 'A>' $(CCPTNOLOG)`" = 0 \
		|| { echo "verify-ccpt: FAIL -- there was a prompt without a CCP file"; exit 1; }
	@echo "verify-ccpt: PASS -- .SUB position, \$$n expansion, IF stack and !-chain all survive a CCP reload"

# ---- Resident System Extensions (src/bdos/rsx.c, src/bdos/rsxhdr.h) ----
# One cold boot, seven commands, and every observable the layer has:
#
#   RSXT                  nothing resident: fn 60 goes unclaimed, the
#                         chain head is 0, the TPA top is the whole
#                         segment and RSXT's own output stays lower case
#   RSXLDR UCASE.RSX T    attach with the warm-boot flag set.  The
#                         message it prints AFTERWARDS comes back in
#                         upper case, so the chain was live within the
#                         same call the attach returned from
#   RSXT                  gone: RSXLDR's exit was a warm boot, and a
#                         module flagged for removal does not survive one
#                         (loader3.asm:326-380).  Everything reads as it
#                         did in step 1
#   RSXLDR UCASE.RSX      attach to stay
#   RSXT                  intercepted (upper case), fn 60 answered by the
#                         module, chain head at F000, and the base page's
#                         TPA top has dropped below the module -- which
#                         is the fence that let the module survive this
#                         very program being loaded
#   RSXT                  again, from a second load: still there
#   MHELLO RSX            an unrelated program still runs.  Its function
#                         2 output is folded and its function 9 output is
#                         NOT, which is the pass-down path: UCASE claims
#                         one function and hands the rest to the BDOS
#
# (RSXORG), so a fence that fails to move, or moves by the wrong amount,
RSXIMG	= build/rsxtest.bin
RSXLOG	= build/verify-rsx.log
RSXVERIFYIN = $(OSSEL)RSXT\rRSXLDR UCASE.RSX T\rRSXT\rRSXLDR UCASE.RSX\rRSXT\rRSXT\rMHELLO RSX\r$(ENDIN)
.PHONY: verify-rsx
verify-rsx: all
	$(MKDISK) $(RSXIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RSXIMG)) \
		--input="$(RSXVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(RSXLOG))
	@$(EMUOK)
	@test "`grep -c 'rsxt: fn60 unclaimed' $(RSXLOG)`" = 2 \
		|| { echo "verify-rsx: FAIL -- fn 60 was claimed when no module was resident (or the temporary module survived a warm boot)"; exit 1; }
	@test "`grep -c 'rsxt: head=0000' $(RSXLOG)`" = 2 \
		|| { echo "verify-rsx: FAIL -- the chain head is not empty when it should be"; exit 1; }
	@# still dominates and the number below is unchanged.
	@grep -q 'RSXLDR: ATTACHED AT F000 TEMPORARY' $(RSXLOG) \
		|| { echo "verify-rsx: FAIL -- the temporary attach did not report through the module it had just attached"; exit 1; }
	@grep -q 'RSXLDR: ATTACHED AT F000 RESIDENT' $(RSXLOG) \
		|| { echo "verify-rsx: FAIL -- the resident attach failed"; exit 1; }
	@test "`grep -c 'RSXT: HELLO FROM RSXT' $(RSXLOG)`" = 2 \
		|| { echo "verify-rsx: FAIL -- function 2 was not intercepted by the resident module, twice"; exit 1; }
	@test "`grep -c 'RSXT: HEAD=F000' $(RSXLOG)`" = 2 \
		|| { echo "verify-rsx: FAIL -- the module is not in the chain after a program load"; exit 1; }
	@test "`grep -c 'RSXT: HTPA=EDF8' $(RSXLOG)`" = 2 \
		|| { echo "verify-rsx: FAIL -- the loader did not fence the module out of the TPA"; exit 1; }
	@grep -q 'RSXT: FN60 ANSWERED' $(RSXLOG) \
		|| { echo "verify-rsx: FAIL -- the module did not answer its own function 60 sub-function"; exit 1; }
	@grep -q 'MWC hello from the Coherent' $(RSXLOG) \
		|| { echo "verify-rsx: FAIL -- function 9 was folded too: the module is claiming calls it does not handle"; exit 1; }
	@grep -q 'ARG 1: RSX' $(RSXLOG) \
		|| { echo "verify-rsx: FAIL -- an unrelated program's function 2 output was not intercepted"; exit 1; }
	@echo "verify-rsx: PASS -- attach, intercept, pass down, fence, survive a load, remove on warm boot"

# ---- more than one module at a time (src/bdos/rsx.c, src/bdos/bdosglue.s) ----
# verify-rsx above runs one module, and one module cannot exercise a
# chain: relinking that is wrong with two is invisible with one, and a
# pass-down that skips the module above it looks exactly like a correct
# pass-down when there is nothing above it.  Six cold boots, each on its
# own fresh copy of the disk, and every one of them has two modules in
# it at some point:
#
#   1  no module, then UCASE.RSX and PROT.RSX resident together, then
#      RSXT2 twice.  The control run first: with an empty chain the
#      delete SUCCEEDS and the file is gone, which is what gives the
#      refusal in the two-module runs its meaning
#   2  the same pair stacked the other way round -- PROT.RSX attached
#      first and UCASEL.RSX (UCASE linked low, src/cmd/ucrsxl.s) under it,
#      and then a third attach that has to be REFUSED: PROT.RSX is
#      linked for E800, which is no longer below the bottom of the
#      chain (v3's calcdest rule, loader3.asm:615-632).  The chain has
#      to be exactly as it was afterwards
#   3  a TEMPORARY module below a resident one: the head goes away at
#      the warm boot and the survivor above it becomes the head
#   4  a temporary module ABOVE a resident one: the tail goes away and
#      the head stays where it is
#   5  both temporary: the chain empties and the fence goes all the way
#      back up
#   6  three stock split-I/D (0xEE0B) tools -- ASZ8K, XCON, XDUMP --
#      run with both modules resident
#
# What each run is really asking:
#
#   THE CHAIN.  RSXT2 walks the modules from the head through their
#   `next' fields and prints every prefix field of each (src/bdos/rsxhdr.h).
#   The system wrote those links; a program that had no part in writing
#   them reads them back, so `prev' -- which nothing in the system reads
#   -- is checked as well as `next'.  The dumps below are exact.
#   THE TRAVERSAL.  Sub-function 200 belongs to UCASE.RSX and 201 to
#   PROT.RSX (src/cmd/ucrsx.s, src/cmd/prsx.s).  With both resident, each
#   answer had to pass through the other module to be given, so an
#   `unclaimed' where a number is expected means a pass-down went to the
#   BDOS instead of to the module above it.
#   A FILE FUNCTION, AND A RESULT POST-PROCESSED.  PROT.RSX refuses
#   function 19 without passing it down (dirlbl.asm:37-45's shape) and
#   counts function 15's results AFTER the BDOS has produced them
#   (v3's `call'-style module).  PROT=0211 is opens, failures, deletes:
#   two opens that worked, one that did not and one delete refused, and
#   those numbers can only be right if the module saw what the BDOS
#   answered.
#
#   CCP is now transient, so function 15 on .Z8K goes through SC gate.
#   Open counts are cumulative per session: one extra open per RSXT2 command.
#   THE SPLIT-I/D BYPASS.  Run 6's tools print in lower case, which is
#   the bypass working, and PROT's counters do not move for the files
#   the tools opened -- 0611 is RSXT2's own two plus one CCP open for
#   each of the four commands after the attach, and nothing else.  A
#   split tool whose opens leaked into the chain would read higher.  The
#   chain is intact afterwards.
#
# The htpa values are exact and each names one module's org: E5F8 for
RSX2IMG	= build/rsx2test.bin
RSX2LOG	= build/verify-rsx2
RSX2IN1	= $(OSSEL)RSXT2\rRSXLDR UCASE.RSX PROT.RSX\rRSXT2\rRSXT2\r
RSX2IN2	= $(OSSEL)RSXLDR PROT.RSX UCASEL.RSX\rRSXLDR PROT.RSX\rRSXT2\r
RSX2IN3	= $(OSSEL)RSXLDR UCASE.RSX PROT.RSX T\rRSXT2\r
RSX2IN4	= $(OSSEL)RSXLDR UCASE.RSX T PROT.RSX\rRSXT2\r
RSX2IN5	= $(OSSEL)RSXLDR UCASE.RSX T PROT.RSX T\rRSXT2\r
RSX2IN6	= $(OSSEL)RSXLDR UCASE.RSX PROT.RSX\rASZ8K MINI.8KN\rXCON -o MINI.O MINI.OBJ\rXDUMP MINI.O\rRSXT2\r$(ENDIN)
# ---- three modules, and a module removed from the MIDDLE ----
# Runs 1-6 never put more than two modules in the chain and never removed
# one with survivors on both sides of it, so two things the layer is
# written for had only ever been reasoned about:
#
#   THE WALK IS FOR N.  src/bdos/bdosglue.s rsxwalk finds the module a
#   pass-down came from by running up the chain from the head, and
#   src/bdos/rsx.c rsxwboot() rebuilds the chain from its survivors.  Both are
#   loops, and a loop that is right for two can be wrong for three -- the
#   bug this layer already produced once was a pass-down that was right
#   only for the last module in the chain.
#   A CALL HAS TO CROSS TWO MODULES.  With two copies of UCASE.RSX both
#   answering sub-function 0C8h, nothing distinguishes a pass-down that
#   crossed one module from one that crossed two.  UCASE3.RSX
#   (src/cmd/ucrsx3.s) claims 0CAh instead, so with it at the HEAD and
#   UCASE.RSX at the TOP, `RSXT2: UCASE=' can only appear if the call
#   travelled the whole chain; if any hop went to the BDOS instead the
#   answer is 0FFh and RSXT2 prints `ucase unclaimed'.
#
#   7  three modules, all resident.  RSXLDR prints the links the ATTACH
#      wrote -- the only moment they exist, since the warm boot that ends
#      RSXLDR rebuilds every one of them -- and RSXT2 then prints the
#      links the REBUILD wrote, for three survivors.
#   8  the middle one temporary.  After the warm boot the chain is
#      E000 -> F000 with a 202-byte hole at E800, and the fence does NOT
#      move: v3 lowers it only when the module that went was the bottom
#      one (loader3.asm:375-378 `mov a,d ! ora a ! cz fixchain2'), so a
#      middle module's space is not reclaimed there either.
#   9  the bottom TWO temporary.  Only the top module survives and the
#      fence has to climb over both dead ones in a single walk.
RSX2IN7	= $(OSSEL)RSXLDR UCASE.RSX PROT.RSX UCASE3.RSX\rRSXT2\r
RSX2IN8	= $(OSSEL)RSXLDR UCASE.RSX PROT.RSX T UCASE3.RSX\rRSXT2\r
RSX2IN9	= $(OSSEL)RSXLDR UCASE.RSX PROT.RSX T UCASE3.RSX T\rRSXT2\r
# The dumped prefix of each module, up to but not including `len': the
# length is the module's own size and moves whenever its source does,
# while everything before it is the system's bookkeeping.
S1PROT	= RSXT2: MOD E800 PROT     SER=C90001 WF=00 NB=00 EC=00 PL=00 PREV=0000 NEXT=F000
S1UCASE	= RSXT2: MOD F000 UCASE    SER=C90001 WF=00 NB=00 EC=FF PL=00 PREV=E800 NEXT=0000
S2UCASE	= RSXT2: MOD E000 UCASE    SER=C90001 WF=00 NB=00 EC=00 PL=00 PREV=0000 NEXT=E800
S2PROT	= RSXT2: MOD E800 PROT     SER=C90001 WF=00 NB=00 EC=FF PL=00 PREV=E000 NEXT=0000
S3UCASE	= RSXT2: MOD F000 UCASE    SER=C90001 WF=00 NB=00 EC=FF PL=00 PREV=0000 NEXT=0000
S4PROT	= rsxt2: mod E800 PROT     ser=C90001 wf=00 nb=00 ec=FF pl=00 prev=0000 next=0000
# Run 7: three survivors, relinked in memory order by rsxwboot().
S7UC3	= RSXT2: MOD E000 UCASE3   SER=C90001 WF=00 NB=00 EC=00 PL=00 PREV=0000 NEXT=E800
S7PROT	= RSXT2: MOD E800 PROT     SER=C90001 WF=00 NB=00 EC=00 PL=00 PREV=E000 NEXT=F000
S7UCASE	= RSXT2: MOD F000 UCASE    SER=C90001 WF=00 NB=00 EC=FF PL=00 PREV=E800 NEXT=0000
# Run 8: the middle module gone, and the two survivors joined across it.
S8UC3	= RSXT2: MOD E000 UCASE3   SER=C90001 WF=00 NB=00 EC=00 PL=00 PREV=0000 NEXT=F000
S8UCASE	= RSXT2: MOD F000 UCASE    SER=C90001 WF=00 NB=00 EC=FF PL=00 PREV=E000 NEXT=0000
# Run 9: one survivor, and it is the TOP module.
S9UCASE	= RSXT2: MOD F000 UCASE    SER=C90001 WF=00 NB=00 EC=FF PL=00 PREV=0000 NEXT=0000
# The three links an attach writes for a chain of three.  The middle line
# is the one a two-module chain has no equivalent of: both of its fields
# name another module.
C3HEAD	= RSXLDR: CHAIN E000 PREV=0000 NEXT=E800 EC=0000
C3MID	= RSXLDR: CHAIN E800 PREV=E000 NEXT=F000 EC=0000
C3TOP	= RSXLDR: CHAIN F000 PREV=E800 NEXT=0000 EC=00FF
.PHONY: verify-rsx2
verify-rsx2: all
	for n in 1 2 3 4 5 7 8 9; do \
		$(MKDISK) $(RSX2IMG) $(CPMSYS) $(CPMAIMG); \
		case $$n in \
		1) in='$(RSX2IN1)';; 2) in='$(RSX2IN2)';; 3) in='$(RSX2IN3)';; \
		4) in='$(RSX2IN4)';; 5) in='$(RSX2IN5)';; 7) in='$(RSX2IN7)';; \
		8) in='$(RSX2IN8)';; 9) in='$(RSX2IN9)';; esac; \
		{ $(EMUCD) && ./c900 --disk=$(abspath $(RSX2IMG)) \
			--input="$${in}$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
			$(EMUSTAT); } \
		| tee $(abspath $(RSX2LOG))-$$n.log; $(EMUOK); done
	$(MKDISK) $(RSX2IMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RSX2IMG)) \
		--input="$(RSX2IN6)" --max=$(ARXMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(RSX2LOG))-6.log
	@$(EMUOK)
#	--- 1: the control, then two modules resident together
	@grep -q 'rsxt2: chain empty' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the chain was not empty before anything was attached"; exit 1; }
	@grep -q 'rsxt2: delete guard=0 ok' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the control delete did not reach the BDOS, so the refusal below proves nothing"; exit 1; }
	@grep -q 'rsxt2: reopen guard=255 fail' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the control delete left the file behind"; exit 1; }
	@grep -q 'RSXLDR: ATTACHED AT F000 RESIDENT' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the first module did not attach"; exit 1; }
	@grep -q 'RSXLDR: ATTACHED AT E800 RESIDENT' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the second module did not attach below the first"; exit 1; }
	@grep -q 'RSXLDR: CHAIN E800 PREV=0000 NEXT=F000 EC=0000' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the links the ATTACH wrote are wrong for the module that went in below (the warm-boot rebuild repairs them, so this is the only run that can see it)"; exit 1; }
	@grep -q 'RSXLDR: CHAIN F000 PREV=E800 NEXT=0000 EC=00FF' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the attach did not link the module ABOVE back to the new one"; exit 1; }
	@test "`grep -c '$(S1PROT)' $(RSX2LOG)-1.log`" = 2 \
		|| { echo "verify-rsx2: FAIL -- the head module's links are wrong with two modules resident"; exit 1; }
	@test "`grep -c '$(S1UCASE)' $(RSX2LOG)-1.log`" = 2 \
		|| { echo "verify-rsx2: FAIL -- the second module was not relinked when one attached below it"; exit 1; }
	@test "`grep -c 'RSXT2: UCASE=' $(RSX2LOG)-1.log`" = 2 \
		|| { echo "verify-rsx2: FAIL -- the pass-down did not reach the module ABOVE the head: it went to the BDOS"; exit 1; }
	@grep -q 'RSXT2: PROT=0311' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the file counters are wrong: an interception or a post-processed result was lost"; exit 1; }
	@grep -q 'RSXT2: PROT=0622' $(RSX2LOG)-1.log \
		|| { echo "verify-rsx2: FAIL -- the counters did not carry over into a second program load: the module is not the same one it was"; exit 1; }
	@test "`grep -c 'RSXT2: DELETE GUARD=255 FAIL' $(RSX2LOG)-1.log`" = 2 \
		|| { echo "verify-rsx2: FAIL -- function 19 was not intercepted"; exit 1; }
	@test "`grep -c 'RSXT2: REOPEN GUARD=. OK' $(RSX2LOG)-1.log`" = 2 \
		|| { echo "verify-rsx2: FAIL -- the refused delete reached the BDOS anyway: the file is gone"; exit 1; }
	@test "`grep -c 'RSXT2: HTPA=E5F8' $(RSX2LOG)-1.log`" = 2 \
		|| { echo "verify-rsx2: FAIL -- the fence is not below the LOWEST of the two modules"; exit 1; }
#	--- 2: the same two modules stacked the other way round
	@grep -q 'rsxldr: attached at E800 resident' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- PROT.RSX was expected to attach first and to fold nothing"; exit 1; }
	@grep -q 'RSXLDR: ATTACHED AT E000 RESIDENT' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- UCASEL.RSX did not attach below PROT.RSX"; exit 1; }
	@grep -q 'RSXLDR: CHAIN E000 PREV=0000 NEXT=E800 EC=0000' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- the attach-time links are wrong with the modules in this order"; exit 1; }
	@grep -q 'RSXLDR: CHAIN E800 PREV=E000 NEXT=0000 EC=00FF' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- the attach did not link the module above back in this order"; exit 1; }
	@grep -q '$(S2UCASE)' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- the head's links are wrong with the modules in the other order"; exit 1; }
	@grep -q '$(S2PROT)' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- the module above the head was not relinked in this order"; exit 1; }
	@grep -q 'RSXLDR: THE SYSTEM REFUSED THE ATTACH 00F2 ORG=E800' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- a module that no longer fits below a two-module chain was not refused (v3's calcdest rule, loader3.asm:615-632)"; exit 1; }
	@grep -q 'RSXT2: PROT=0611' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- with PROT.RSX resident first, RSXLDR's own opens of the module files should have been counted"; exit 1; }
	@grep -q 'RSXT2: UCASE=' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- the head module did not answer its own sub-function"; exit 1; }
	@grep -q 'RSXT2: HTPA=DDF8' $(RSX2LOG)-2.log \
		|| { echo "verify-rsx2: FAIL -- the fence does not follow the lower module in this order"; exit 1; }
#	--- 3: a temporary module BELOW a resident one
	@grep -q 'RSXLDR: ATTACHED AT E800 TEMPORARY' $(RSX2LOG)-3.log \
		|| { echo "verify-rsx2: FAIL -- the temporary attach under a resident module failed"; exit 1; }
	@grep -q 'RSXLDR: CHAIN E800 PREV=0000 NEXT=F000 EC=0000' $(RSX2LOG)-3.log \
		|| { echo "verify-rsx2: FAIL -- the two modules were not both resident before the warm boot that removed one"; exit 1; }
	@test "`grep -c 'RSXT2: MOD ' $(RSX2LOG)-3.log`" = 1 \
		|| { echo "verify-rsx2: FAIL -- the chain does not hold exactly the one survivor"; exit 1; }
	@grep -q '$(S3UCASE)' $(RSX2LOG)-3.log \
		|| { echo "verify-rsx2: FAIL -- the survivor was not rebuilt as a chain of one when the head went away"; exit 1; }
	@grep -q 'RSXT2: PROT UNCLAIMED' $(RSX2LOG)-3.log \
		|| { echo "verify-rsx2: FAIL -- the temporary module survived the warm boot"; exit 1; }
	@grep -q 'RSXT2: UCASE=' $(RSX2LOG)-3.log \
		|| { echo "verify-rsx2: FAIL -- removing the module below took the resident one with it"; exit 1; }
	@grep -q 'RSXT2: DELETE GUARD=0 OK' $(RSX2LOG)-3.log \
		|| { echo "verify-rsx2: FAIL -- function 19 is still being intercepted by a module that is gone"; exit 1; }
	@grep -q 'RSXT2: HTPA=EDF8' $(RSX2LOG)-3.log \
		|| { echo "verify-rsx2: FAIL -- the fence did not come back up to the surviving module"; exit 1; }
#	--- 4: a temporary module ABOVE a resident one
	@grep -q 'RSXLDR: ATTACHED AT E800 RESIDENT' $(RSX2LOG)-4.log \
		|| { echo "verify-rsx2: FAIL -- the resident attach under a temporary module failed"; exit 1; }
	@grep -q 'RSXLDR: CHAIN E800 PREV=0000 NEXT=F000 EC=0000' $(RSX2LOG)-4.log \
		|| { echo "verify-rsx2: FAIL -- the two modules were not both resident before the warm boot that removed one"; exit 1; }
	@test "`grep -c 'rsxt2: mod ' $(RSX2LOG)-4.log`" = 1 \
		|| { echo "verify-rsx2: FAIL -- the chain does not hold exactly the one survivor"; exit 1; }
	@grep -q '$(S4PROT)' $(RSX2LOG)-4.log \
		|| { echo "verify-rsx2: FAIL -- the surviving HEAD was not rebuilt when the module above it went away"; exit 1; }
	@grep -q 'rsxt2: ucase unclaimed' $(RSX2LOG)-4.log \
		|| { echo "verify-rsx2: FAIL -- the temporary module above survived the warm boot"; exit 1; }
	@grep -q 'rsxt2: prot=0311' $(RSX2LOG)-4.log \
		|| { echo "verify-rsx2: FAIL -- the surviving module no longer intercepts or post-processes"; exit 1; }
	@grep -q 'rsxt2: htpa=E5F8' $(RSX2LOG)-4.log \
		|| { echo "verify-rsx2: FAIL -- the fence moved even though the lowest module is still there"; exit 1; }
#	--- 5: both temporary
	@grep -q 'RSXLDR: CHAIN E800 PREV=0000 NEXT=F000 EC=0000' $(RSX2LOG)-5.log \
		|| { echo "verify-rsx2: FAIL -- the two temporary modules were not both resident before the warm boot"; exit 1; }
	@grep -q 'rsxt2: chain empty' $(RSX2LOG)-5.log \
		|| { echo "verify-rsx2: FAIL -- two temporary modules did not both go at the warm boot"; exit 1; }
	@grep -q 'rsxt2: ucase unclaimed' $(RSX2LOG)-5.log \
		|| { echo "verify-rsx2: FAIL -- the upper temporary module is still answering"; exit 1; }
	@grep -q 'rsxt2: prot unclaimed' $(RSX2LOG)-5.log \
		|| { echo "verify-rsx2: FAIL -- the lower temporary module is still answering"; exit 1; }
#	--- 6: stock split-I/D tools with both modules resident
	@grep -q 'Zilog CP/M-Z8000 Assembler' $(RSX2LOG)-6.log \
		|| { echo "verify-rsx2: FAIL -- ASZ8K did not run, or its banner was folded: a split-I/D tool went through the chain"; exit 1; }
	@grep -q 'magic = EE03' $(RSX2LOG)-6.log \
		|| { echo "verify-rsx2: FAIL -- XCON/XDUMP did not run with modules resident"; exit 1; }
	@grep -q '$(S1PROT)' $(RSX2LOG)-6.log \
		|| { echo "verify-rsx2: FAIL -- the chain did not survive the split-I/D tools"; exit 1; }
	@grep -q '$(S1UCASE)' $(RSX2LOG)-6.log \
		|| { echo "verify-rsx2: FAIL -- the chain did not survive the split-I/D tools"; exit 1; }
	@grep -q 'RSXT2: PROT=0611' $(RSX2LOG)-6.log \
		|| { echo "verify-rsx2: FAIL -- the split tools' own file calls reached the module: the bypass leaks"; exit 1; }
	@grep -q 'RSXT2: HTPA=E5F8' $(RSX2LOG)-6.log \
		|| { echo "verify-rsx2: FAIL -- the fence did not survive the split-I/D tools"; exit 1; }
#	--- 7: three modules, all resident
	@grep -q '$(C3HEAD)' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- the attach-time links of the HEAD of a three-module chain are wrong"; exit 1; }
	@grep -q '$(C3MID)' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- the attach did not relink the MIDDLE module: both its fields have to name another module, which is a case two modules cannot produce"; exit 1; }
	@grep -q '$(C3TOP)' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- the module attached FIRST was disturbed when a third went in below it"; exit 1; }
	@test "`grep -c 'RSXT2: MOD ' $(RSX2LOG)-7.log`" = 3 \
		|| { echo "verify-rsx2: FAIL -- the chain does not hold exactly three modules after the warm boot"; exit 1; }
	@grep -q '$(S7UC3)' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- rsxwboot rebuilt the head of a three-survivor chain wrongly"; exit 1; }
	@grep -q '$(S7PROT)' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- rsxwboot rebuilt the MIDDLE of a three-survivor chain wrongly"; exit 1; }
	@grep -q '$(S7UCASE)' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- rsxwboot rebuilt the top of a three-survivor chain wrongly"; exit 1; }
	@grep -q 'RSXT2: UCASE=' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- sub-function 0C8h did not reach the TOP module: a pass-down went to the BDOS after crossing fewer than two modules"; exit 1; }
#   0411, not the 0311 this session was written with: the transient CCP
#   reaches the BDOS through the gate, so a module now sees the CCP's own
#   open as well as the program's.  Every other session in this target took
#   the same +1 when the CCP left the system image; this one was added by a
#   lane that branched before that and so never got it.
	@grep -q 'RSXT2: PROT=0411' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- the middle module's counters are wrong, so a call it should have seen went past it"; exit 1; }
	@grep -q 'RSXT2: HTPA=DDF8' $(RSX2LOG)-7.log \
		|| { echo "verify-rsx2: FAIL -- the fence is not below the lowest of three modules"; exit 1; }
#	--- 8: the MIDDLE module removed, survivors above and below it
	@grep -q '$(C3MID)' $(RSX2LOG)-8.log \
		|| { echo "verify-rsx2: FAIL -- the three modules were not all resident before the warm boot that removed the middle one"; exit 1; }
	@test "`grep -c 'RSXT2: MOD ' $(RSX2LOG)-8.log`" = 2 \
		|| { echo "verify-rsx2: FAIL -- removing the middle module did not leave exactly two survivors"; exit 1; }
	@grep -q '$(S8UC3)' $(RSX2LOG)-8.log \
		|| { echo "verify-rsx2: FAIL -- the survivor BELOW the removed module was not relinked to the one above it"; exit 1; }
	@grep -q '$(S8UCASE)' $(RSX2LOG)-8.log \
		|| { echo "verify-rsx2: FAIL -- the survivor ABOVE the removed module was not relinked to the one below it"; exit 1; }
	@grep -q 'RSXT2: PROT UNCLAIMED' $(RSX2LOG)-8.log \
		|| { echo "verify-rsx2: FAIL -- the temporary MIDDLE module survived the warm boot"; exit 1; }
	@grep -q 'RSXT2: DELETE GUARD=0 OK' $(RSX2LOG)-8.log \
		|| { echo "verify-rsx2: FAIL -- function 19 is still being intercepted by the middle module, which is gone"; exit 1; }
	@grep -q 'RSXT2: UCASE=' $(RSX2LOG)-8.log \
		|| { echo "verify-rsx2: FAIL -- the pass-down does not cross the hole the removed middle module left"; exit 1; }
	@grep -q 'RSXT2: HTPA=DDF8' $(RSX2LOG)-8.log \
		|| { echo "verify-rsx2: FAIL -- the fence moved when a MIDDLE module was removed; v3 lowers it only for the bottom one (loader3.asm:375-378)"; exit 1; }
#	--- 9: the bottom two removed, the fence climbs over both
	@test "`grep -c 'RSXT2: MOD ' $(RSX2LOG)-9.log`" = 1 \
		|| { echo "verify-rsx2: FAIL -- removing the bottom two of three did not leave exactly one survivor"; exit 1; }
	@grep -q '$(S9UCASE)' $(RSX2LOG)-9.log \
		|| { echo "verify-rsx2: FAIL -- the lone survivor was not rebuilt as a chain of one"; exit 1; }
	@grep -q 'RSXT2: UCASE=' $(RSX2LOG)-9.log \
		|| { echo "verify-rsx2: FAIL -- the survivor no longer answers its own sub-function"; exit 1; }
	@grep -q 'RSXT2: HTPA=EDF8' $(RSX2LOG)-9.log \
		|| { echo "verify-rsx2: FAIL -- the fence did not climb past BOTH removed modules in one walk"; exit 1; }
	@echo "verify-rsx2: PASS -- two modules stacked both ways, three modules, chain links read back, pass-down across a whole chain of three, a file function intercepted and a BDOS result post-processed, removal from either end and from the MIDDLE, and split-I/D tools bypassing a live chain"

# ---- GENCOM: modules bound to a program file (src/cmd/gencom.c) ----
# verify-rsx and verify-rsx2 both attach modules with RSXLDR, which is a
# program v3 does not have.  This target exercises v3's OWN route: GENCOM
# staples modules onto a program file and the loader attaches them every
# time that program is run (gencom.plm; loader3.asm:216-244, here
# src/bdos/pgmld.c ldrsx).  Three runs:
#
#   1  bind ONE temporary module, run the program twice, then strip.
#      PROTN.RSX (src/cmd/prsxn.s) is PROT.RSX with the bank flag set, which
#      is the only lever a module has for saying "do not outlive the
#      program I am bound to": GENCOM reads that byte and forces the
#      warm-boot flag to 0FFh (gencom.plm:1186-1189).  So the module is
#      resident for exactly one program run, which is why the SECOND run
#      reads identically to the first -- and reading identically is the
#      check, because a module that had survived would carry its counters
#      forward and answer 0422 instead of 0211 the second time (that is
#      what run 1 of verify-rsx2 shows a surviving module doing).
#      The strip then has to give back a program that still runs.
#   2  bind THREE, and read the chain from inside the bound program.
#      Warm boot rebuilds all ATTACH links. Running a second time must FAIL:
#      two modules are resident, nothing relocates the third.
#   3  bind, then re-bind the same module name, then bind a set that
#      cannot stack.  The replacement is v3's (gencom.plm:381-382,
#      remover :1446-1552); the refusal is ours and it exists only
#      because there is no relocator, so it has to happen at bind time.
#      The program still runs afterwards: a refused bind leaves the file
#      it was given alone.
GCIMG	= build/gctest.bin
GCLOG	= build/verify-gencom
GCIN1	= $(OSSEL)RSXT2\rGENCOM RSXT2.Z8K PROTN.RSX\rRSXT2\rRSXT2\rGENCOM RSXT2.Z8K\rRSXT2\r
GCIN2	= $(OSSEL)GENCOM RSXT2.Z8K UCASE.RSX PROTN.RSX UCASE3.RSX\rRSXT2\rRSXT2\r
GCIN3	= $(OSSEL)GENCOM MHELLO.Z8K PROT.RSX\rGENCOM MHELLO.Z8K PROTN.RSX\rMHELLO\rRSXT2\rGENCOM MHELLO.Z8K UCASE.RSX PROT.RSX\rMHELLO\r
# The bound module as the loader leaves it.  wf=FF is the whole point:
# nothing set it but GENCOM, from nb=01 (gencom.plm:1186).
# Run 1 has no UCASE module in it, so nothing folds its output: these
# are lower case where runs 2 and 3 of verify-rsx2 are upper.
G1PROT	= rsxt2: mod E800 PROT     ser=C90001 wf=FF nb=01 ec=FF pl=00 prev=0000 next=0000
# Three bound modules, with the links the ATTACH wrote.
G2UC3	= RSXT2: MOD E000 UCASE3   SER=C90001 WF=00 NB=00 EC=00 PL=00 PREV=0000 NEXT=E800
G2PROT	= RSXT2: MOD E800 PROT     SER=C90001 WF=FF NB=01 EC=00 PL=00 PREV=E000 NEXT=F000
G2UCASE	= RSXT2: MOD F000 UCASE    SER=C90001 WF=00 NB=00 EC=FF PL=00 PREV=E800 NEXT=0000
.PHONY: verify-gencom
verify-gencom: all
	for n in 1 2 3; do \
		$(MKDISK) $(GCIMG) $(CPMSYS) $(CPMAIMG); \
		case $$n in \
		1) in='$(GCIN1)';; 2) in='$(GCIN2)';; 3) in='$(GCIN3)';; esac; \
		{ $(EMUCD) && ./c900 --disk=$(abspath $(GCIMG)) \
			--input="$${in}$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
			$(EMUSTAT); } \
		| tee $(abspath $(GCLOG))-$$n.log; $(EMUOK); done
#	--- 1: one temporary module, bound, run twice, stripped
	@grep -q 'rsxt2: chain empty' $(GCLOG)-1.log \
		|| { echo "verify-gencom: FAIL -- the chain was not empty before anything was bound"; exit 1; }
	@grep -q 'gencom: PROT     at E800 len 00CA nb=0001 temporary' $(GCLOG)-1.log \
		|| { echo "verify-gencom: FAIL -- GENCOM did not read the bank flag and force the module temporary (gencom.plm:1186-1189)"; exit 1; }
	@test "`grep -c '$(G1PROT)' $(GCLOG)-1.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- the loader did not attach the bound module on BOTH runs of the program"; exit 1; }
	@test "`grep -c 'rsxt2: prot=0211' $(GCLOG)-1.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- the second run did not get a FRESH copy of the module: counters carried over, so the warm boot did not remove it"; exit 1; }
	@test "`grep -c 'rsxt2: htpa=E5F8' $(GCLOG)-1.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- the bound module was not fenced out of the program's TPA"; exit 1; }
	@test "`grep -c 'rsxt2: delete guard=255 fail' $(GCLOG)-1.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- the bound module did not intercept function 19"; exit 1; }
	@test "`grep -c 'GENCOM completed.' $(GCLOG)-1.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- the bind or the strip did not complete"; exit 1; }
	@test "`grep -c 'rsxt2: chain empty' $(GCLOG)-1.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- the strip left the container in place, or the stripped program no longer runs"; exit 1; }
		|| { echo "verify-gencom: FAIL -- the stripped program did not get the whole TPA back"; exit 1; }
#	--- 2: three modules bound, links read from inside the bound program
	@grep -q 'gencom: UCASE    at F000 len 0076 nb=0000 resident' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- GENCOM did not record the first module"; exit 1; }
	@grep -q 'gencom: UCASE3   at E000 len 0076 nb=0000 resident' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- GENCOM did not record the third module"; exit 1; }
	@test "`grep -c 'RSXT2: MOD ' $(GCLOG)-2.log`" = 3 \
		|| { echo "verify-gencom: FAIL -- the loader did not attach all three bound modules"; exit 1; }
	@grep -q '$(G2UC3)' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- the head module's attach-time links are wrong; nothing else in the suite can see them, because the warm boot rewrites them"; exit 1; }
	@grep -q '$(G2PROT)' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- the middle module's attach-time links are wrong"; exit 1; }
	@grep -q '$(G2UCASE)' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- the module bound FIRST is not the top of the chain"; exit 1; }
	@grep -q 'RSXT2: UCASE=' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- a call did not travel the whole bound chain"; exit 1; }
	@grep -q 'RSXT2: HTPA=DDF8' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- the program was not loaded below all three of its own modules"; exit 1; }
	@grep -q 'Insufficient memory' $(GCLOG)-2.log \
		|| { echo "verify-gencom: FAIL -- a program whose RESIDENT modules are already in was loaded a second time; with no relocator its modules cannot be placed twice"; exit 1; }
#	--- 3: replace in place, and a set that cannot stack
	@grep -q 'gencom: PROT     at E800 len 00CA nb=0000 resident' $(GCLOG)-3.log \
		|| { echo "verify-gencom: FAIL -- the first bind did not happen"; exit 1; }
	@test "`grep -c 'gencom: replacing PROT' $(GCLOG)-3.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- a module of a name already in the header was appended instead of replacing it (gencom.plm:381-382)"; exit 1; }
	@grep -q 'gencom: PROT     at E800 len 00CA nb=0001 temporary' $(GCLOG)-3.log \
		|| { echo "verify-gencom: FAIL -- the replacement did not take"; exit 1; }
	@grep -q 'gencom: UCASE    is linked at F000 length 0076, which does not fit below E800' $(GCLOG)-3.log \
		|| { echo "verify-gencom: FAIL -- a set of modules that cannot stack as linked was accepted; the loader would refuse it at run time instead (v3 calcdest, loader3.asm:615-632)"; exit 1; }
	@test "`grep -c 'MWC hello from the Coherent' $(GCLOG)-3.log`" = 2 \
		|| { echo "verify-gencom: FAIL -- the bound program stopped running, or the REFUSED bind damaged the file it was given"; exit 1; }
	@grep -q 'rsxt2: chain empty' $(GCLOG)-3.log \
		|| { echo "verify-gencom: FAIL -- the replaced module was not temporary: it outlived the program it was bound to"; exit 1; }
	@echo "verify-gencom: PASS -- modules bound to a program file and attached by the loader, the bank flag forcing a module temporary, three modules bound with the attach-time links read from inside the program, replace in place, a set that cannot stack refused at bind time, and a strip that gives the program back"
# ---- cursor addressing (verify-crsr) ----
# Two halves, because one console cannot answer for the other.
#
#   host   src/bios/crsr.c compiled natively with its two screen accessors
#          pointed at arrays (tests/crsrtest.c), which is the only way the
#          LR video backend can be exercised at all: the emulator has no
#          video card, so on it the ROM always picks the serial console.
#   target CRSRDEMO run on the emulator, whose serial byte stream is fed
#          to a small ANSI terminal model (tests/vt.py) that renders it as
#          an 80x25 grid.  The assertions are then by ROW AND COLUMN --
#          asserting on the byte stream would only show that bytes were
#          emitted, not that anything was positioned.
#
# CRSRDEMO's coordinates (src/cmd/crsrdemo.c) are the contract: box corners
# at (4,10)/(4,60)/(14,10)/(14,60), labels inside it, the four one-step
# motions around the anchor at (2,40), an erase-to-end-of-line at (16,30)
# and an erase-to-end-of-screen at (19,20).
# ---- directory hashing on/off A-B test (PLAN.md sec 9 rows 5 and 8) ----
# Row 5 (hashing/BCB) was CANNOT-VERIFY and row 8 (GENCPM) was FAIL for the
# same reason: nothing could turn hashing off, so there was no A-B to run.
# src/bdos/dskhash.c hashen[2] is that switch (see its own comment); this
# builds a second cpm.sys with it compiled off and compares the two on the
# EMULATOR's own instruction count (see tests/hashab.py's own docstring
# for why a wildcard `DIR' cannot show this and TYPE can).
HASHOFFDIR = build/hashoff
HASHOFFOBJ = $(HASHOFFDIR)/obj
HASHOFFSYS = $(HASHOFFDIR)/cpm.sys
HASHABSRC  = build/hashab-src
HASHABB    = build/hashab-b.img
HASHABON   = build/hashab-on.bin
HASHABOFF  = build/hashab-off.bin
HASHABN    = 384

# A phony name DISTINCT from $(HASHOFFSYS) itself: this recurses into the
# same Makefile with OBJDIR/CPMSYS/DEFS overridden, so the normal build's
# own build/obj and build/cpm.sys are never touched by it.  Naming this
# phony target $(HASHOFFSYS) instead would make the recursive $(MAKE)
# see the very same rule for that target name and recurse forever --
# there being no OTHER rule left to build the real file with.
.PHONY: hashoff-cpmsys
hashoff-cpmsys:
	$(MAKE) OBJDIR=$(HASHOFFOBJ) CPMSYS=$(HASHOFFSYS) \
		DEFS='-DHASH_A_DEFAULT=0 -DHASH_B_DEFAULT=0' $(HASHOFFSYS)

$(HASHABB): tools/mkcpmfs.py
	@rm -rf $(HASHABSRC) && mkdir -p $(HASHABSRC)
	@i=0; while [ $$i -lt $(HASHABN) ]; do \
		n=`printf %03d $$i`; \
		printf 'hash test file %s\r\n' $$n > $(HASHABSRC)/F$$n.TXT; \
		i=$$((i+1)); \
	done
	python3 tools/mkcpmfs.py --label C900B --label-mode create,update \
		$@ $(CPMB_BLOCKS) $(HASHABSRC)

$(HASHABON): $(CPMSYS) $(CPMARIMG) $(HASHABB) $(wildcard $(KBOOT)) tools/mkcpmdisk.py
	$(MKDISK) $@ $(CPMSYS) $(CPMARIMG) $(HASHABB)

$(HASHABOFF): hashoff-cpmsys $(CPMARIMG) $(HASHABB) $(wildcard $(KBOOT)) tools/mkcpmdisk.py
	$(MKDISK) $@ $(HASHOFFSYS) $(CPMARIMG) $(HASHABB)

.PHONY: verify-hash-ab
verify-hash-ab: $(HASHABON) $(HASHABOFF)
	python3 tests/hashab.py --emu $(EMU) --on $(abspath $(HASHABON)) \
		--off $(abspath $(HASHABOFF)) --n $(HASHABN)

CRSRIMG	= build/crsrtest.bin
CRSRLOG	= build/verify-crsr.log
# No $(ENDIN) here: this target replays the transcript through a terminal
# emulator and asserts what is ON THE SCREEN.  Echoing one more command
# scrolls the frame CRSRDEMO drew, so the mark would break the assertion.
# The run ends on idle, as it did before.
CRSRIN	= $(OSSEL)CRSRDEMO\r

.PHONY: verify-crsr
verify-crsr: all build/crsrtest
	./build/crsrtest
	$(MKDISK) $(CRSRIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CRSRIMG)) \
		--input="$(CRSRIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CRSRLOG))
	@$(EMUOK)
	@# the frame: four corners at named coordinates, and a side at a row
	@# no text is written to, so a screen that merely scrolled cannot pass
	python3 tests/vt.py $(CRSRLOG) \
		--cell 4 10 '+' --cell 4 60 '+' \
		--cell 14 10 '+' --cell 14 60 '+' \
		--cell 12 10 '|' --cell 12 60 '|' \
		--cell 6 14 'CP/M-8000 on the Commodore 900' \
		--cell 10 18 'row 10, column 18 -- here.' \
		|| { echo "verify-crsr: FAIL -- direct cursor addressing (ESC Y) did not place the frame"; exit 1; }
	@# the four one-step motions, each one cell from the anchor at (2,40)
	python3 tests/vt.py $(CRSRLOG) \
		--cell 1 40 'U' --cell 3 40 'D' \
		--cell 2 39 'L' --cell 2 41 'R' \
		--blank 2 40 1 \
		|| { echo "verify-crsr: FAIL -- ESC A/B/C/D did not move one cell each way"; exit 1; }
	@# erase to end of line: kept left of column 30, gone from it
	python3 tests/vt.py $(CRSRLOG) \
		--cell 16 0 'EOL' --cell 16 29 'x' --blank 16 30 50 \
		|| { echo "verify-crsr: FAIL -- ESC K did not erase exactly to the end of the line"; exit 1; }
	@# erase to end of screen: row 18 whole, row 19 up to column 20, and
	@# row 20 gone entirely
	python3 tests/vt.py $(CRSRLOG) \
		--cell 18 69 'y' --cell 19 19 'y' \
		--blank 19 20 60 --blank 20 0 80 \
		|| { echo "verify-crsr: FAIL -- ESC J did not erase to the end of the screen"; exit 1; }
	@# the two parser properties: an unclaimed escape reaches the console
	@# as its own bytes, and CAN abandons a half-typed address
	python3 tests/vt.py $(CRSRLOG) --cell 21 0 'PT' --cell 21 10 'CN' \
		|| { echo "verify-crsr: FAIL -- an unclaimed escape or a CAN abort swallowed the text after it"; exit 1; }
	@grep -q "`printf '\033&'`" $(CRSRLOG) \
		|| { echo "verify-crsr: FAIL -- the unclaimed ESC & did not reach the console verbatim"; exit 1; }
	@# nothing was broken for ordinary output: the sign-on still reads
	@# straight, and the CCP prompt is where the program left the cursor
	python3 tests/vt.py $(CRSRLOG) --cell 22 0 'CRSRDEMO done.' --cell 24 0 'A>' \
		|| { echo "verify-crsr: FAIL -- the CCP did not resume from where the program left the cursor"; exit 1; }
	@echo "verify-crsr: PASS -- addressing, motion, erase and pass-through, on the screen"

# The harness is K&R, like the source it includes.

build/crsrtest: tests/crsrtest.c src/bios/crsr.c | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -o $@ tests/crsrtest.c
# THE LAYOUT IS PART OF THE TEST (H7).  The bug is that filero()'s nested
# dirscan reads a SECOND directory record into the one directory buffer,
# leaving the delete() that called error(5) holding a pointer into the wrong
# one -- so it only bites when CONCTGT.TXT's two entries are in different
# records.  Which they are is decided by how many entries the rest of A:
# takes: 97 and 124 straddle, 95, 96 and 98 do not, and a session on a
# non-straddling disk passes having tested nothing.  tests/concpad.sh pins
# the count when the image is built; concfree.py refuses to run the session
# on a disk where the pin did not hold.
	python3 tests/concfree.py $(CPMACONC)

# ---- verify-xdospoll5: FN 131 (POLL DEVICE) ON DEVICE 0, PROVED (C9) ----
# run/C2.md left this one open: fn 131 on device 0 takes conbdos.c getch()'s
# own PW_CON wait (src/bdos/xdos.c xpoll()), but with one console and the
# emulator's own input feeder deciding when a scripted byte lands, a test
# could only hang or pass by luck -- there was no way to hold a key back
# from a SPECIFIC process's own console on demand.  E4's per-port wires
# (`--wireN=PATH', tests/wirecon.py's `--wire') end that: wired to console
# 1, the byte is under THIS target's control instead of the feeder's.
#
# TWO PROPERTIES, TWO PHASES, because they fail in different ways.
#
# BLOCKS.  XDOSPOL.Z8K (src/cmd/xdospol.c) creates CONCY.Z8K -- the same
# compute-bound job verify-conc5 uses -- and then, with `P', moves itself
# to console 1 and calls fn 131 on device 0 with the wire left silent for
# the whole run.  CONCY's tick count is read the same way verify-conc5
# reads it (BIOS fn 24, no BDOS gate in the loop) and compared against the
# SAME program run without `P', where it never touches fn 131 at all.  This
# is verify-conc5's own instrument, reused rather than rebuilt: one process
# genuinely blocked in fn 131 costs the other nothing but a console poll
# per dispatch (C5.md's 0.6%), the same call spinning or failing to wait
# costs it a share of every tick (C5.md's 7.4%) -- so $(CONCZTOL), already
# measured against exactly that gap, is what this target checks the delta
# against too.  The run also asserts the poller's own "got"/"done" lines
# NEVER appear: a key arriving on a silent wire would mean something else
# fed it one, and "consumes ~nothing while parked" would be measuring the
# wrong run.
#
# WAKES.  A second boot of the identical disk, `XDOSPOL P' again, this
# time with tests/wirecon.py sending a byte -- but only AFTER it has seen
# "XDOSPOL: waiting" on the wire, which XDOSPOL prints immediately before
# calling fn 131.  That ordering is the proof: the byte is provably absent
# at the moment the call is entered, so a reply can only come from having
# actually waited for it.  fn 131 only peeks (bconstat), so XDOSPOL then
# reads the byte for real with fn 1 and reports it, moves back to console
# 0, and finishes -- and the target checks the reported byte, the move
# back, and the finish line, none of which a call that returned early or
# never returned could produce.
XDOSPOLLDISK  = build/xdospoll.bin
XPOLLALOG     = build/verify-xdospoll5-alone.log
XPOLLPLOG     = build/verify-xdospoll5-poll-c0.log
XPOLLPWLOG    = build/verify-xdospoll5-poll-c1.log
XPOLLWLOG     = build/verify-xdospoll5-wake-c0.log
XPOLLWWLOG    = build/verify-xdospoll5-wake-c1.log
.PHONY: verify-xdospoll5
verify-xdospoll5: all $(CPMAXDOSPOL)
	$(MKDISK) $(XDOSPOLLDISK) $(CPMSYS) $(CPMAXDOSPOL) $(CPMBIMG)
	@# ---- phase 1: ALONE -- CONCY's baseline tick count ----
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XDOSPOLLDISK)) \
		| tee $(abspath $(XPOLLALOG))
	@$(EMUOK)
	@# ---- phase 2: the poller parked in fn 131, wire silent throughout ----
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(XDOSPOLLDISK) \
		--log $(XPOLLPLOG) --wire-log $(XPOLLPWLOG)
	@# ---- phase 3: the same call, woken by a byte sent only after the
	@#      wire shows the process was already waiting for it ----
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(XDOSPOLLDISK) \
		--send-after='XDOSPOL: waiting' --send='K' \
		--log $(XPOLLWLOG) --wire-log $(XPOLLWWLOG)
	@tr -d '\r' < $(XPOLLALOG) > build/xdospoll-alone.txt
	@tr -d '\r' < $(XPOLLPLOG) > build/xdospoll-poll-c0.txt
	@tr -d '\r' < $(XPOLLPWLOG) > build/xdospoll-poll-c1.txt
	@tr -d '\r' < $(XPOLLWLOG) > build/xdospoll-wake-c0.txt
	@tr -d '\r' < $(XPOLLWWLOG) > build/xdospoll-wake-c1.txt
	@grep -q 'XDOSPOL: no second process' build/xdospoll-alone.txt build/xdospoll-poll-c0.txt build/xdospoll-wake-c0.txt \
		&& { echo "verify-xdospoll5: FAIL -- function 144 refused; a 512 KB machine has"; \
		     echo "             no free page (src/bios/pgalloc.c).  The emulator is 1 MB."; \
		     exit 1; } || true
	@grep -q 'XDOSPOL: no console 1' build/xdospoll-poll-c0.txt build/xdospoll-wake-c0.txt \
		&& { echo "verify-xdospoll5: FAIL -- function 148 refused console 1; the wire did"; \
		     echo "             not attach (src/bios/bios900.c coninit)."; exit 1; } || true
		     echo "             processes; it is not the baseline it claims to be."; exit 1; }
	@# THE NEGATIVE: on a silent wire, the poller must never have woken.
	@grep -q '^XDOSPOL: waiting' build/xdospoll-poll-c1.txt \
		|| { echo "verify-xdospoll5: FAIL -- XDOSPOL never printed its wait marker on"; \
		     echo "             console 1; fn 148 or the wire did not reach it"; exit 1; }
	@grep -qE 'XDOSPOL: (got|back 0|done)' build/xdospoll-poll-c0.txt build/xdospoll-poll-c1.txt \
		&& { echo "verify-xdospoll5: FAIL -- the poller woke on a wire nothing was ever"; \
		     echo "             sent on; fn 131 is not blocking, or something else fed"; \
		     echo "             it a byte."; exit 1; } || true
	@sed -n 's/.*CONCY: Y done.*ticks=\([0-9][0-9]*\).*/\1/p' build/xdospoll-alone.txt \
		> build/xdospoll-ticks-alone.txt
	@sed -n 's/.*CONCY: Y done.*ticks=\([0-9][0-9]*\).*/\1/p' build/xdospoll-poll-c0.txt \
		> build/xdospoll-ticks-poll.txt
	@for f in ticks-alone ticks-poll; do test -s build/xdospoll-$$f.txt \
		|| { echo "verify-xdospoll5: FAIL -- CONCY never printed its tick count ($$f)."; \
		     echo "             transcripts are above."; exit 1; }; done
	@awk -v tol=$(CONCZTOL) \
	     -v ya=`cat build/xdospoll-ticks-alone.txt` -v yb=`cat build/xdospoll-ticks-poll.txt` \
	  'BEGIN { \
	     printf "verify-xdospoll5: fn 131 device 0, an idle poller, measured\n"; \
	     printf "  %-22s %8s %8s %9s\n", "job", "alone", "+poller", "delta"; \
	     dy = (yb - ya) * 100.0 / ya; \
	     printf "  %-22s %8d %8d %8.1f%%\n", "CONCY 16 units", ya, yb, dy; \
	     printf "  processes live         %8d %8d\n", 2, 2; \
	     ay = dy < 0 ? -dy : dy; \
	     if (ay > tol) { \
	       printf "verify-xdospoll5: FAIL -- fn 131 cost CONCY more than %d%%: it is not\n", tol; \
	       printf "             genuinely blocked (compare CONCZTOL, run/C5.md: 0.6%%/7.4%%)\n"; \
	       exit 1; } }' \
	|| exit 1
	@# ---- phase 3's checks: it woke, with the right byte, and got home ----
	@grep -q '^XDOSPOL: waiting' build/xdospoll-wake-c1.txt \
		|| { echo "verify-xdospoll5: FAIL -- XDOSPOL (wake run) never printed its wait"; \
		     echo "             marker on console 1"; exit 1; }
	@# No `^' anchor: function 1 echoes the byte it reads, so the sent `K'
	@# and this line share one line on the wire with nothing between them
	@# (`KXDOSPOL: got K') -- the echo is CONIN's, not a defect.
	@grep -q 'XDOSPOL: got K' build/xdospoll-wake-c1.txt \
		|| { echo "verify-xdospoll5: FAIL -- fn 131 returned but did not read the byte"; \
		     echo "             back correctly (function 1, console 1)"; exit 1; }
	@grep -q '^XDOSPOL: back 0' build/xdospoll-wake-c0.txt \
		|| { echo "verify-xdospoll5: FAIL -- XDOSPOL never moved back to console 0 after"; \
		     echo "             fn 131 returned; it did not really wake"; exit 1; }
	@grep -q '^XDOSPOL: done' build/xdospoll-wake-c0.txt \
		|| { echo "verify-xdospoll5: FAIL -- XDOSPOL did not reach its own end after"; \
		     echo "             waking"; exit 1; }
	@echo "verify-xdospoll5: PASS -- fn 131 device 0 blocks (CONCY's tick count is"
	@echo "             unaffected within CONCZTOL, and the poller never woke on a"
	@echo "             silent wire) and wakes (it read the byte only after a wire"
	@echo "             trace proves the byte was not there when it was asked for)"
# ---- verify-all: every verify-* target above, one after another ----
# Sequential because the targets share build/ (media, transcripts, and the
# images `all' rebuilds).  Each runs in its own $(MAKE) so a failure ends
# that target only; the rest still run, and the summary names every verdict.
# Exit status is non-zero iff any target failed.  The list is read from this
# file at run time -- a target added above is picked up without registering
# it here -- and the pattern keeps hyphens (verify-hash-ab, verify-rtc-host).
# A test target, reached from nothing: `all' never runs an emulator.
VERIFYALLLOG = build/verify-all.log
.PHONY: verify-all
verify-all:
	@mkdir -p build; rm -f $(VERIFYALLLOG); pass=0; fail=0; \
	for t in $$(sed -n 's/^\(verify-[a-z0-9-]*\):.*/\1/p' tests/verify.mk \
		| grep -v '^verify-all$$' | sort -u); do \
		echo "=== $$t"; \
		if $(MAKE) --no-print-directory $$t; then \
			echo "PASS $$t" >> $(VERIFYALLLOG); pass=$$((pass+1)); \
		else \
			echo "FAIL $$t" >> $(VERIFYALLLOG); fail=$$((fail+1)); \
		fi; \
	done; \
	echo "=== verify-all summary ($(VERIFYALLLOG))"; cat $(VERIFYALLLOG); \
	echo "verify-all: $$pass passed, $$fail failed, $$((pass+fail)) run"; \
	[ "$$fail" -eq 0 ]

HELPIMG	= build/helptest.bin
HELPLOG	= build/verify-help.log
HELPSRC	= src/dist/disk-a/HELP.HLP
HELPS1	= writes the result to
HELPS2	= console INPUT for the program
HELPS3	= PIP copies files
HELPIN	= $(OSSEL)HELP\rSUBMIT\rSUBMIT PARAMETERS\rSUB\rNOSUCHTOPIC\r\r$(ENDIN)

.PHONY: verify-help
verify-help: all $(CPMAIMG)
	@for s in "$(HELPS1)" "$(HELPS2)" "$(HELPS3)"; do \
		n=`grep -c "$$s" $(HELPSRC)`; \
		test "$$n" = 1 || { echo "verify-help: FAIL -- the sentinel '$$s' occurs $$n times in $(HELPSRC), not once; the test cannot tell topics apart with it"; exit 1; }; \
	 done
	$(MKDISK) $(HELPIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(HELPIMG)) \
		--input="$(HELPIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(HELPLOG))
	@$(EMUOK)
	@# 1. the topic list is exactly the file's level-1 markers.
	@sed -n 's,^  ///1,,p' $(HELPSRC) | tr -d '\015' | sort > build/help-want.txt
	@# the transcript's line ends are CR CR LF -- the program writes
	@# CR LF and the console driver adds its own CR to the LF -- so the
	@# CRs come off before any anchored pattern is applied.
	@tr -d '\015' < $(HELPLOG) | sed -n '/^Topics:/,/^$$/p' \
		| sed -n 's/^    \([A-Za-z0-9]*\) *$$/\1/p' \
		| tr 'a-z' 'A-Z' | sort -u > build/help-got.txt
	@cmp -s build/help-want.txt build/help-got.txt \
		|| { echo "verify-help: FAIL -- the topic list is not the file's level-1 topics:"; \
		     diff build/help-want.txt build/help-got.txt; exit 1; }
	@# 2. one topic, and only that topic.  Both HELP SUBMIT and the
	@#    prefix request HELP SUB print it, so the count is 2.
	@test "`grep -c '$(HELPS1)' $(HELPLOG)`" = 2 \
		|| { echo "verify-help: FAIL -- HELP SUBMIT and the prefix request HELP SUB did not each print the SUBMIT topic exactly once"; exit 1; }
	@test "`grep -c '$(HELPS3)' $(HELPLOG)`" = 0 \
		|| { echo "verify-help: FAIL -- an unrelated topic's text was printed: HELP is not bounding the topic it was asked for"; exit 1; }
	@# 3. the subtopic descent.  HELP SUBMIT names it; HELP SUBMIT
	@#    PARAMETERS prints its text, which HELP SUBMIT must NOT.
	@grep -q '^    PARAMETERS' $(HELPLOG) \
		|| { echo "verify-help: FAIL -- HELP SUBMIT did not name its subtopics"; exit 1; }
	@test "`grep -c '$(HELPS2)' $(HELPLOG)`" = 1 \
		|| { echo "verify-help: FAIL -- the subtopic's text did not appear exactly once: either the descent failed or a parent topic printed its children's text as well"; exit 1; }
	@# 4. an unknown topic is refused -- once, for NOSUCHTOPIC alone.
	@test "`grep -c 'No information on that topic' $(HELPLOG)`" = 1 \
		|| { echo "verify-help: FAIL -- the refusal did not fire exactly once (NOSUCHTOPIC refused, the prefix SUB accepted)"; exit 1; }
	@echo "verify-help: PASS -- topic list, one bounded topic, subtopic descent, prefix match, refusal"

# ---- console paging (src/bdos/conbdos.c pagelf, the SCB's 1Ch/1Dh/2Ch) ----
# The same program, the same output, twice: once with page$mode 0 and once
# with 0FFh.  What is asserted is the DIFFERENCE the byte makes, which is
# the only thing the feature is:
#
#   - with paging OFF the console never pauses;
#   - with paging ON it pauses NLINES/PAGE times, both numbers read out of
#     src/cmd/paget.c at run time so that changing either one changes what
#     this target expects rather than breaking it;
#   - EVERY line arrives in BOTH runs, all NLINES of them, numbered -- so a
#     pause released the output that followed it instead of eating it;
#   - and the SCB reads back what was written to it, so the byte the
#     driver acted on is the byte a program can see.
#
# NOTHING HERE COUNTS LINES OF TRANSCRIPT OR FIXES A POSITION.  Where the
# pause prompt lands depends on how much the CCP printed before PAGET
# started, and PAGET zeroes @CONLINE for exactly that reason; the test
# does not need to know, and must not, because a scheduler that
# interleaves another console's output would move it.
#
# Two cold boots, one per mode, because a run that has paused has consumed
# scripted input and the next one must not inherit its position.
# `\g' turns off the wait-for-a-prompt pacing: `Press RETURN to Continue '
# ends in a space, not in `>' or `#', so without it the emulator would
# never release the keystroke the pause is waiting for.
PAGEIMG	 = build/pagetest.bin
PAGELOGF = build/verify-page-off.log
PAGELOGN = build/verify-page-on.log
PAGEINF	 = $(OSSEL)PAGET OFF\r$(ENDIN)
PAGEINN	 = $(OSSEL)PAGET ON\r\g\r\r\r\r$(ENDIN)
PAGEPROMPT = Press RETURN to Continue

.PHONY: verify-page
verify-page: all $(CPMAIMG)
	$(MKDISK) $(PAGEIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PAGEIMG)) \
		--input="$(PAGEINF)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PAGELOGF))
	@$(EMUOK)
	$(MKDISK) $(PAGEIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PAGEIMG)) \
		--input="$(PAGEINN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PAGELOGN))
	@$(EMUOK)
	@# both runs finished, and printed every line they said they would
	@nl=`sed -n 's/^#define[ 	]*NLINES[ 	][ 	]*\([0-9][0-9]*\).*/\1/p' src/cmd/paget.c`; \
	 pg=`sed -n 's/^#define[ 	]*PAGE[ 	][ 	]*\([0-9][0-9]*\).*/\1/p' src/cmd/paget.c`; \
	 test -n "$$nl" -a -n "$$pg" \
		|| { echo "verify-page: FAIL -- NLINES/PAGE could not be read out of src/cmd/paget.c, so this target does not know what to expect"; exit 1; }; \
	 for f in $(PAGELOGF) $(PAGELOGN); do \
		grep -q 'PAGET: DONE' $$f \
			|| { echo "verify-page: FAIL -- PAGET did not finish in $$f"; exit 1; }; \
		i=1; while [ $$i -le $$nl ]; do \
			n=`printf %02d $$i`; \
			grep -q "PAGET LINE $$n" $$f \
				|| { echo "verify-page: FAIL -- line $$n never reached the console in $$f: output that followed a pause was lost"; exit 1; }; \
			i=`expr $$i + 1`; \
		done; \
		grep -q 'readback page=05 mode=FF default=00' $$f \
			|| { echo "verify-page: FAIL -- the SCB did not read back what PAGET wrote to it in $$f; the driver and the published bytes disagree"; exit 1; }; \
	 done; \
	 off=`grep -c '$(PAGEPROMPT)' $(PAGELOGF)`; \
	 on=`grep -c '$(PAGEPROMPT)' $(PAGELOGN)`; \
	 want=`expr $$nl / $$pg`; \
	 test "$$off" = 0 \
		|| { echo "verify-page: FAIL -- the console paused $$off times with page mode OFF; the byte is not being honoured"; exit 1; }; \
	 test "$$on" = "$$want" \
		|| { echo "verify-page: FAIL -- the console paused $$on times with page mode ON, not $$want ($$nl lines at $$pg lines to the page)"; exit 1; }; \
	 echo "verify-page: PASS -- $$nl lines arrived in full both ways; $$want pauses with page mode on, none with it off"

# ---- PROFILE.SUB at cold start (src/ccp/ccp.c profstart) ----
# CP/M 3's CCP runs PROFILE.SUB once, before its first prompt, by chaining
# to that command line (ref/cpm3/ccp3.asm:460-473 `ckboot').  Ours does the
# same, and the three things worth asserting are all differences between
# two boots of the SAME system on two images that differ by ONE FILE:
#
#   1. with PROFILE.SUB on A:, every line of it runs -- and runs BEFORE
#      the first command the console types, which is what "at cold start"
#      means and is checked by ordering the profile's output against that
#      command's, not against any line number;
#   2. each line runs EXACTLY ONCE.  Every line here loads a program, so
#      each one ends in a warm boot and a reloaded CCP; a cold-start test
#      that did not check this would pass just as happily on a CCP that
#      restarted the profile after every command and never reached the
#      console at all;
#   3. with no PROFILE.SUB the CCP says nothing about it.  v3 has to set
#      errflg to suppress the complaint it would otherwise print; we ask
#      the directory first, and this leg is what says so.
#
# The profile's lines are read out of src/dist/disk-a-prof/PROFILE.SUB at
# run time, so editing the fixture changes what is expected rather than
# breaking the target.
PROFIMG	 = build/proftest.bin
PROFLOGY = build/verify-profile-yes.log
PROFLOGN = build/verify-profile-no.log
PROFSRC	 = src/dist/disk-a-prof/PROFILE.SUB
PROFIN	 = $(OSSEL)MHELLO CONSOLE-COMMAND\r$(ENDIN)

.PHONY: verify-profile
verify-profile: all $(CPMAPROF)
	$(MKDISK) $(PROFIMG) $(CPMSYS) $(CPMAPROF)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PROFIMG)) \
		--input="$(PROFIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PROFLOGY))
	@$(EMUOK)
	$(MKDISK) $(PROFIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PROFIMG)) \
		--input="$(PROFIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PROFLOGN))
	@$(EMUOK)
	@# 1 and 2: every argument the profile passes MHELLO appears, once.
	@tr -d '\015' < $(PROFLOGY) > build/prof-y.txt
	@tr -d '\015' < $(PROFLOGN) > build/prof-n.txt
	@n=0; \
	 for a in `tr -d '\015' < $(PROFSRC) | sed -n 's/^MHELLO  *\([^ ][^ ]*\).*/\1/p'`; do \
		n=`expr $$n + 1`; \
		c=`grep -c "arg 1: $$a" build/prof-y.txt`; \
		test "$$c" = 1 \
			|| { echo "verify-profile: FAIL -- the profile line for $$a ran $$c times, not once; a cold-start profile that reruns on every warm boot never reaches the console"; exit 1; }; \
		if grep -q "arg 1: $$a" build/prof-n.txt; then \
			echo "verify-profile: FAIL -- $$a ran on the image that has no PROFILE.SUB"; exit 1; fi; \
	 done; \
	 test "$$n" -ge 2 \
		|| { echo "verify-profile: FAIL -- $(PROFSRC) has $$n usable lines; this target needs at least two, because one line cannot show that a second one followed it"; exit 1; }; \
	 echo "verify-profile: $$n profile lines, each run once"
	@# 1 again, as an ORDER and not a position: the last thing the
	@# profile did comes before the first thing the console asked for.
	@last=`tr -d '\015' < $(PROFSRC) | sed -n 's/^MHELLO  *\([^ ][^ ]*\).*/\1/p' | tail -1`; \
	 lp=`grep -n "arg 1: $$last" build/prof-y.txt | head -1 | cut -d: -f1`; \
	 cp=`grep -n 'arg 1: CONSOLE-COMMAND' build/prof-y.txt | head -1 | cut -d: -f1`; \
	 test -n "$$lp" -a -n "$$cp" \
		|| { echo "verify-profile: FAIL -- the console command did not run after the profile at all"; exit 1; }; \
	 test "$$lp" -lt "$$cp" \
		|| { echo "verify-profile: FAIL -- the profile ran after the typed command, so it is not a cold-start profile"; exit 1; }
	@# 3: the run with no profile is quiet about it, and still works.
	@grep -q 'arg 1: CONSOLE-COMMAND' build/prof-n.txt \
		|| { echo "verify-profile: FAIL -- the typed command did not run on the image with no PROFILE.SUB"; exit 1; }
	@if grep -q 'PROFILE' build/prof-n.txt; then \
		echo "verify-profile: FAIL -- the CCP mentioned PROFILE on a system that has none; a missing profile must be silent"; exit 1; fi
	@echo "verify-profile: PASS -- the profile runs once at cold start, before the first typed command, and a system without one is silent"

# ---- verify-kermit: A FILE OUT AND BACK OVER THE SERIAL LINE (N2) ----
# KERMIT.Z8K (src/cmd/cpmio.c + the E-Kermit engine, src/cmd/kermit.c) moves
# a file between this machine and the other end of the spare RS-232 port,
# which is the first thing on this port that is neither a console nor a
# test harness -- and, until a network exists, the only way a file gets onto
# a real C900's CP/M disk.
#
# TWO BOOTS ON ONE DISK, WHICH IS WHAT MAKES IT A ROUND TRIP.  The first
# boot runs `KERMIT R' and the host peer pushes a 1 KB file at it; the guest
# writes it to A: through the ordinary BDOS file calls.  The second boot,
# ON THE SAME IMAGE, runs `KERMIT S KTEST.BIN' and the peer catches it.  The
# assertion is `cmp': every byte that went out came back.  Nothing in the
# path is stubbed -- the bytes cross BIOS functions 6 and 7 on the channel
# at 0x0120, through the C8 receive ring, with the console unbound from that
# channel by function 28 for the duration.
#
# THE FILE IS PSEUDO-RANDOM AND ITS SEED IS FIXED.  Random content is what
# makes the test binary-clean: it contains control characters, high bytes,
# SOH, CR and the protocol's own quote character, so a transfer that mangles
# any of those fails.  (It did: the first passing round trip differed in
# exactly the three bytes that were '#'.)  1024 bytes is eight CP/M records,
# so the length is exact -- CP/M cannot record a partial last record, and a
# file that is not a multiple of 128 would come back padded and the cmp
# would be measuring the file system, not the transfer.
#
# WHAT THE OTHER END IS, SAID PLAINLY.  tests/kermitpeer.py, a Kermit
# written for this test.  There is no gkermit, ckermit or kermit on this
# build machine, so the interop leg the task wanted could not be run; see
# that file's own banner, which says what this does and does not prove.
KERMIMG	 = build/kermtest.bin
KERMSRC	 = build/ktest-src.bin
KERMBACK = build/ktest-back.bin
.PHONY: verify-kermit
verify-kermit: all
	@python3 -c 'import random; random.seed(20260907); \
open("$(KERMSRC)","wb").write(bytes(random.randrange(256) for _ in range(1024)))'
	$(MKDISK) $(KERMIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	rm -f $(KERMBACK)
	python3 tests/kermitpeer.py --emu '$(EMU)' --disk $(KERMIMG) \
		--mode send --file $(KERMSRC) --as KTEST.BIN \
		--max=$(EMUMAX) --log build/verify-kermit-out.log
	python3 tests/kermitpeer.py --emu '$(EMU)' --disk $(KERMIMG) \
		--mode recv --out $(KERMBACK) --as KTEST.BIN \
		--max=$(EMUMAX) --log build/verify-kermit-in.log
	@tr -d '\r' < build/verify-kermit-out.log | grep -q 'KERMIT: done' \
		|| { echo "verify-kermit: FAIL -- the receiving guest did not finish"; \
		     exit 1; }
	@tr -d '\r' < build/verify-kermit-in.log | grep -q 'KERMIT: done' \
		|| { echo "verify-kermit: FAIL -- the sending guest did not finish"; \
		     exit 1; }
	@cmp $(KERMSRC) $(KERMBACK) \
		|| { echo "verify-kermit: FAIL -- the file did not come back intact"; \
		     exit 1; }
	@echo "verify-kermit: PASS -- 1024 bytes written to A: over the spare"
	@echo "               serial port and read back off it, byte for byte,"
	@echo "               against tests/kermitpeer.py (NOT an interop test)"

# ---- GET and PUT: console I/O redirected through the RSX chain ----
# src/cmd/get.c + src/cmd/getrsx.s, src/cmd/put.c + src/cmd/putrsx.s.
#
# What these two targets have to establish is that a module in the chain
# can SUPPLY a BDOS call's answer and can SWALLOW one, that it does so for
# the CCP as well as for a program -- which is the thing v3's GET needs
# and the thing this port could not do until the CCP became a transient
# (src/bdos/rsx.c) -- and that the [ECHO] option is real rather than
# parsed and dropped.
#
# THE ECHO LEG IS THE ONE THAT PROVES ANYTHING.  A GET that ran the file's
# commands with echoing on and a GET that ran them with echoing off look
# alike in every respect but one: the commands themselves appear in the
# first transcript and not in the second, while their OUTPUT appears in
# both.  So the pair of runs is the assertion, and neither run alone
# would catch a program that accepted [NO ECHO] and ignored it.
#
# THE FIXTURE IS THE COMMAND STREAM.  build/diska-gp/GCMDS.TXT holds
#   INITDIR A: / N / RSXT2 / TYPE GMARKER.TXT / GET CONSOLE / TYPE GNEVER.TXT
# and every line of it is load-bearing:
#   - INITDIR A: is read by the CCP through function 10, and its Y/N
#     question is read by INITDIR through function 1 (src/cmd/initdir.c
#     askchar), so one fixture exercises both of the functions GET serves.
#     The answer is N: nothing is written to any disk.
#   - RSXT2 prints the chain, so the module is seen to be linked, at its
#     own address, under its own name, while it is doing the work.
#   - TYPE GMARKER.TXT prints a string that is nowhere else on the disk.
#   - GET CONSOLE stops the redirection, and TYPE GNEVER.TXT is the line
#     AFTER it: its string must NOT appear, which is the only way to tell
#     a GET that stopped from a GET that merely ran out of file.
#
# THE TYPE-AHEAD AT THE END is not decoration either.  `TYPE GMARK2.TXT'
# is typed at the CONSOLE, and it can only run if console input came back
# -- so its string appearing is the proof that the module let go.  It is
# fed with the emulator's \i type-ahead and released by --input-mark,
# because while the file is feeding the CCP the ordinary prompt-paced
# input would be handed over during the file's own session and eaten;
# and it is preceded by two bare CRs because the byte after a mark can
# still land in an output path that is polling for ^S.
GPIMG	= build/gptest.bin
GPLOG	= build/verify-gp
# One sacrificial pair of CRs, then the command, all as type-ahead.
GPBACK	= \i\r\i\r\iT\iY\iP\iE\i \iG\iM\iA\iR\iK\i2\i.\iT\iX\iT\i\r
GPMARK	= Getting console input from console
GETIN1	= $(OSSEL)GET FILE GCMDS.TXT\r$(GPBACK)
GETIN2	= $(OSSEL)GET FILE GCMDS.TXT [NO ECHO]\r$(GPBACK)
GETIN3	= $(OSSEL)GET\rGET FILE NOSUCH.TXT\rGET CONSOLE\rGET FILE GMARKER.TXT [PROGRAM]\rGET FILE GMARKER.TXT [RAW]\rGET FILE GMARKER.TXT ZZZ\r

.PHONY: verify-get
verify-get: all $(CPMAGP)
	for n in 1 2 3; do \
		$(MKDISK) $(GPIMG) $(CPMSYS) $(CPMAGP); \
		case $$n in \
		1) in='$(GETIN1)';; 2) in='$(GETIN2)';; 3) in='$(GETIN3)';; esac; \
		{ $(EMUCD) && ./c900 --disk=$(abspath $(GPIMG)) \
			--input="$$in" --input-mark='$(GPMARK)' \
			--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(GPLOG))-get$$n.log; $(EMUOK); done
#	--- 1: echoing on.  The file drives the session.
	@grep -q 'Getting console input from file: GCMDS.TXT \[ECHO\]' $(GPLOG)-get1.log \
		|| { echo "verify-get: FAIL -- GET did not report that it had taken the file"; exit 1; }
	@grep -q 'rsxt2: mod E400 GET ' $(GPLOG)-get1.log \
		|| { echo "verify-get: FAIL -- GET.RSX is not in the chain at its own link address under its own name while it is serving the file"; exit 1; }
	@test "`grep -c 'TYPE GMARKER.TXT' $(GPLOG)-get1.log`" = 1 \
		|| { echo "verify-get: FAIL -- the command read out of the file was not echoed to the console exactly once"; exit 1; }
	@grep -q 'GET-FIXTURE-RAN' $(GPLOG)-get1.log \
		|| { echo "verify-get: FAIL -- the command read out of the file did not RUN: function 10 was not served from the file"; exit 1; }
	@grep -q '(Y/N)?  N' $(GPLOG)-get1.log \
		|| { echo "verify-get: FAIL -- INITDIR's function-1 question was not answered from the file"; exit 1; }
	@grep -q 'INITDIR TERMINATED' $(GPLOG)-get1.log \
		|| { echo "verify-get: FAIL -- INITDIR did not act on the answer the file gave it"; exit 1; }
	@grep -q 'Getting console input from console' $(GPLOG)-get1.log \
		|| { echo "verify-get: FAIL -- GET CONSOLE, read out of the file itself, did not run"; exit 1; }
	@test "`grep -c 'GET-PAST-STOP' $(GPLOG)-get1.log`" = 0 \
		|| { echo "verify-get: FAIL -- the line AFTER 'GET CONSOLE' in the file was executed: the redirection did not stop"; exit 1; }
	@grep -q 'CONSOLE-CAME-BACK' $(GPLOG)-get1.log \
		|| { echo "verify-get: FAIL -- a command typed at the console after GET CONSOLE did not run: console input did not come back"; exit 1; }
#	--- 2: echoing off.  Same file, same effects, and no commands seen.
	@grep -q 'Getting console input from file: GCMDS.TXT \[NO ECHO\]' $(GPLOG)-get2.log \
		|| { echo "verify-get: FAIL -- GET did not report [NO ECHO]"; exit 1; }
	@test "`grep -c 'TYPE GMARKER.TXT' $(GPLOG)-get2.log`" = 0 \
		|| { echo "verify-get: FAIL -- [NO ECHO] still echoed the commands it read: the option is parsed and ignored"; exit 1; }
	@grep -q 'GET-FIXTURE-RAN' $(GPLOG)-get2.log \
		|| { echo "verify-get: FAIL -- with [NO ECHO] the commands stopped running as well as stopped showing"; exit 1; }
	@grep -q 'INITDIR TERMINATED' $(GPLOG)-get2.log \
		|| { echo "verify-get: FAIL -- with [NO ECHO] the function-1 answer was lost"; exit 1; }
	@test "`grep -c 'GET-PAST-STOP' $(GPLOG)-get2.log`" = 0 \
		|| { echo "verify-get: FAIL -- [NO ECHO]: the line after GET CONSOLE was executed"; exit 1; }
	@grep -q 'CONSOLE-CAME-BACK' $(GPLOG)-get2.log \
		|| { echo "verify-get: FAIL -- [NO ECHO]: console input did not come back"; exit 1; }
#	--- 3: every refusal is by name.  An option this GET does not
#	    implement must say so, not be accepted and dropped.
	@grep -q 'usage: GET' $(GPLOG)-get3.log \
		|| { echo "verify-get: FAIL -- a bare GET did not print its usage"; exit 1; }
	@grep -q 'GET: no such file' $(GPLOG)-get3.log \
		|| { echo "verify-get: FAIL -- GET accepted a file that is not there"; exit 1; }
	@grep -q 'GET: no file is being read' $(GPLOG)-get3.log \
		|| { echo "verify-get: FAIL -- GET CONSOLE with nothing active said nothing"; exit 1; }
	@grep -q '\[PROGRAM\] is not implemented' $(GPLOG)-get3.log \
		|| { echo "verify-get: FAIL -- [PROGRAM] was accepted; it is not implemented and must be refused by name"; exit 1; }
	@grep -q '\[FILTERED\] and \[RAW\] are not implemented' $(GPLOG)-get3.log \
		|| { echo "verify-get: FAIL -- [RAW] was accepted; it is not implemented and must be refused by name"; exit 1; }
	@grep -q 'unknown keyword or option: ZZZ' $(GPLOG)-get3.log \
		|| { echo "verify-get: FAIL -- a word GET does not know was swallowed"; exit 1; }
	@test "`grep -c 'Getting console input from file' $(GPLOG)-get3.log`" = 0 \
		|| { echo "verify-get: FAIL -- one of the refused commands attached the module anyway"; exit 1; }
	@echo "verify-get: PASS -- functions 1 and 10 served from a file for the CCP and for a program, the module seen in the chain, [ECHO] and [NO ECHO] differing in the commands and not in their effects, GET CONSOLE stopping the feed, the console coming back, and six refusals by name"

# ---- PUT ----
# The mirror, and the same shape of proof: two runs of the same commands,
# one with echoing and one without, and what differs is whether the
# console saw the output -- the FILE has it either way, and the file is
# read back with TYPE inside the same session to show that.  So the
# marker string is counted, not grepped: twice with [ECHO] (once live,
# once read back) and once with [NO ECHO].
#
# THE [NO ECHO] RUN CANNOT BE DRIVEN BY THE ORDINARY PACED INPUT, and the
# reason is worth writing down: with echoing off the module swallows the
# CCP's prompt as well as everything else, and the emulator paces
# scripted input on the prompt characters it sees printed.  So the
# commands issued while the capture is running are fed as \i type-ahead,
# released by --input-mark on the echo of the command line that starts
# it.  Nothing is printed during that stretch, so nothing eats them --
# which is only true BECAUSE [NO ECHO] works.
PUTIN1	= $(OSSEL)PUT FILE POUT.TXT\rTYPE PMARKER.TXT\rRSXT2\rPUT CONSOLE\rTYPE POUT.TXT\r
PUTIN2	= $(OSSEL)PUT FILE POUT.TXT [NO ECHO]\i\r\iT\iY\iP\iE\i \iP\iM\iA\iR\iK\iE\iR\i.\iT\iX\iT\i\r\iP\iU\iT\i \iC\iO\iN\iS\iO\iL\iE\i\rTYPE POUT.TXT\r
PUTIN3	= $(OSSEL)PUT\rPUT CONSOLE\rPUT FILE PMARKER.TXT\rPUT FILE PNEW.TXT [PROGRAM]\rPUT PRINTER FILE PNEW.TXT\rPUT FILE PNEW.TXT QQQ\r
PUTMARK	= [NO ECHO]

.PHONY: verify-put
verify-put: all $(CPMAGP)
		$(MKDISK) $(GPIMG) $(CPMSYS) $(CPMAGP); \
		case $$n in \
		{ $(EMUCD) && ./c900 --disk=$(abspath $(GPIMG)) \
			--input="$$in" --input-mark='$(PUTMARK)' \
			--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(GPLOG))-put$$n.log; $(EMUOK); done
#	--- 1: echoing on.  The console and the file both get everything.
	@grep -q 'rsxt2: mod F200 PUT ' $(GPLOG)-put1.log \
		|| { echo "verify-put: FAIL -- PUT.RSX is not in the chain at its own link address under its own name while it is capturing"; exit 1; }
	@test "`grep -c 'PUT-FIXTURE-RAN' $(GPLOG)-put1.log`" = 2 \
		|| { echo "verify-put: FAIL -- with [ECHO] the captured text should appear twice: once as it was printed and once when the file is typed back"; exit 1; }
	@test "`grep -c 'Putting console output to file: POUT.TXT \[ECHO\]' $(GPLOG)-put1.log`" = 2 \
		|| { echo "verify-put: FAIL -- PUT's own message was not both printed and captured: the module was not live for the very next call"; exit 1; }
	@grep -q 'PUT completed for console' $(GPLOG)-put1.log \
		|| { echo "verify-put: FAIL -- PUT CONSOLE did not close the file cleanly"; exit 1; }
#	--- 2: echoing off.  The console sees none of it; the file has it all.
	@test "`grep -c 'PUT-FIXTURE-RAN' $(GPLOG)-put2.log`" = 1 \
		|| { echo "verify-put: FAIL -- with [NO ECHO] the captured text must appear ONCE, when the file is typed back; the console must not have had it"; exit 1; }
	@test "`grep -c 'Putting console output to file: POUT.TXT \[NO ECHO\]' $(GPLOG)-put2.log`" = 1 \
		|| { echo "verify-put: FAIL -- [NO ECHO] did not swallow PUT's own message, or did not write it to the file"; exit 1; }
	@grep -q 'PUT completed for console' $(GPLOG)-put2.log \
		|| { echo "verify-put: FAIL -- [NO ECHO]: PUT CONSOLE did not close the file, or the console did not come back afterwards"; exit 1; }
#	--- 3: the refusals.
	@grep -q 'usage: PUT' $(GPLOG)-put3.log \
		|| { echo "verify-put: FAIL -- a bare PUT did not print its usage"; exit 1; }
	@grep -q 'PUT: nothing is being written' $(GPLOG)-put3.log \
		|| { echo "verify-put: FAIL -- PUT CONSOLE with nothing active said nothing"; exit 1; }
	@grep -q 'already exists; erase it first' $(GPLOG)-put3.log \
		|| { echo "verify-put: FAIL -- PUT was willing to write over a file that is already there"; exit 1; }
	@grep -q '\[PROGRAM\] is not implemented' $(GPLOG)-put3.log \
		|| { echo "verify-put: FAIL -- [PROGRAM] was accepted; it is not implemented and must be refused by name"; exit 1; }
	@grep -q 'the printer is not implemented' $(GPLOG)-put3.log \
		|| { echo "verify-put: FAIL -- PUT PRINTER was accepted; function 5 is not intercepted and it must be refused by name"; exit 1; }
	@grep -q 'unknown keyword or option: QQQ' $(GPLOG)-put3.log \
		|| { echo "verify-put: FAIL -- a word PUT does not know was swallowed"; exit 1; }
	@test "`grep -c 'Putting console output to file' $(GPLOG)-put3.log`" = 0 \
		|| { echo "verify-put: FAIL -- one of the refused commands attached the module anyway"; exit 1; }

# ---- verify-local: the opt-in local medium carries what it is given ----
# `make cpmlocal' exists so the operator can boot a medium carrying programs
# of their own -- ones this project does not ship.  Two things about it have
# to keep working and neither shows up anywhere else in the suite: the
# refusal that keeps such a medium out of a checkout, and the promise that a
# supplied file arrives on drive A: under its own name with its own bytes.
# tests/localt.sh invents the files it stages, so this target proves the
# mechanism on a machine where no such program is present.  A host-side
# check: no emulator, and nothing in `all' depends on the target it drives.
.PHONY: verify-local
verify-local: all
	@sh tests/localt.sh $(MAKE)
