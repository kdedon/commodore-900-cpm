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
.PHONY: verify-driveb
verify-driveb: all
	$(MKDISK) $(DBIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DBIMG)) \
		--input="$(DBVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(DBLOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DBIMG)) \
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
.PHONY: verify-initdir-mutants
verify-initdir-mutants: all
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
		| python3 $(abspath tests/tstamp.py) \
		| tee $(abspath $(SELFDIR).1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TESTIMG)) \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| python3 $(abspath tests/tstamp.py) \
		| tee $(abspath $(SELFDIR).2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(TESTIMG)) \
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
		--max=$(NYBEFOREMAX) 2>$(abspath $(NYLOG))-before.err \
		> $(abspath $(NYLOG))-before.log
	@tail -1 $(NYLOG)-before.err
	@tr -d '\r' < $(NYLOG)-before.log | grep -q '^Mon 12/31/79 23:00:' \
		|| { echo "verify-rtc-newyear: FAIL -- the seeded 1979 clock did not read back"; exit 1; }
	@test "`sed -n 's/.*, leap \([0-9]*\),.*/\1/p' $(NYLOG)-before.err`" = 1 \
		|| { echo "verify-rtc-newyear: FAIL -- 1979 is a surplus of 3 and its code is 01"; exit 1; }
	$(EMUCD) && ./c900 --disk=$(abspath $(NYIMG)) \
		--rtc=1979-12-31T23:50:00 \
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
		| tee $(abspath $(V2LOG))
	@$(EMUOK)
	@grep -q 'fn 9 now stops at a hash' $(V2LOG) \
		|| { echo "verify-v2: FAIL -- an SCB byte set did not reach the BDOS"; exit 1; }
	@test "`grep -c ' BAD' $(V2LOG)`" = 0 \
		|| { echo "verify-v2: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'SCBTEST: PASS' $(V2LOG) \
		|| { echo "verify-v2: FAIL -- SCBTEST did not finish"; exit 1; }
	@echo "verify-v2: PASS"

# SDIR, SHOW, SUBMIT: CCP releases one line at a time, so one cold boot per session.
# SDIR pages by default (SCB page-length 0 = 24-line v3 fallback); [NOPAGE] overrides.
# SUBMIT is A:SUBMIT (resident builtin shadows bare name).
# DOL expands to one '$': SUBMIT's output file really is called $$$.SUB.
DOL	= $$
UTILIMG	= build/utiltest.bin
UTILLOG	= build/verify-util
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
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-2.img) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-3.img) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-3.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-4.img) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-4.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-5.img) \
		2>/dev/null; $(EMUSTAT); } | tee $(abspath $(SETBLOG)-5.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath build/setb-6.img) \
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
.PHONY: verify-stamp
verify-stamp: all
	$(MKDISK) $(STAMPBIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(STAMPBIMG)) \
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
		| tee $(abspath $(LBLLOG)-1.log)
	@$(EMUOK)
	@# a second COLD boot of the same image: the login scan has to find
	@# a label that was written above the last file entry, which is the
	@# high-water-mark rule this wave fixed.
	{ $(EMUCD) && ./c900 --disk=$(abspath $(NOLBLIMG)) \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } | tee $(abspath $(LBLLOG)-2.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PLAINIMG)) \
		| tee $(abspath $(LBLLOG)-3.log)
	@$(EMUOK)
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
		| tee $(abspath $(XFCBLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XFCBCTLIMG)) \
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
			$(EMUSTAT); } \
		| tee $(abspath $(RSX2LOG))-$$n.log; $(EMUOK); done
	$(MKDISK) $(RSX2IMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RSX2IMG)) \
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
CRSRIMG	= build/crsrtest.bin
CRSRLOG	= build/verify-crsr.log
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
