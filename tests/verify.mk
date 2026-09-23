# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# Emulator verification harness; paths are relative to the repository root.
# make verify-<name> runs one check; make verify-all runs the suite.
# make verify-zcc rebuilds the src/app programs on the machine with DRI's
# ZCC and runs those; it is opt-in and not in verify-all, being 20 minutes.
# verify-util asserts the development disk contents; verify-setb copies them.

# Runtime tests consume built emulator and kboot artifacts resolved by tools/deps.sh.
EMU	:= $(if $(EMU),$(EMU),$(shell sh tools/deps.sh emu))
# kboot and boot-medium settings are defined in mk/config.mk.
TESTIMG	= build/emutest.bin
EMUMAX	?= 600000000
# verify-conclk's first run cannot end on its own: the point of it is that
# nothing completes while the prompt is unanswered, so it coasts to this
# budget by design.  Small enough that doing so is cheap, large enough that
# a system which WOULD have completed has had every chance to.
CONCLMAX ?= 250000000
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
# Typed first by every target that needs a session on console 1.  Until D8
# the cold boot started one on every bound console (src/bdos/proc.c
# pcoldses); it now starts one only at a VIDEO console, on SCC-B, and the
# emulator's console is serial, so those targets start it themselves.  What
# they assert is unchanged: the same session exists, it is just asked for.
SESS1	= SESSION 1\r
# POSIX sh reports tee's status for a pipeline. Save the emulator status
# inside the group, then check it with EMUOK after tee drains the transcript.
# EMUCD validates the dependency before changing directories.
EMUCD = sh tools/deps.sh -n emu '$(EMU)' && cd $(EMU)/bin
# Named for the target that is running, because verify-all runs several of
# them at once and the status of one emulator is not the verdict of another.
EMUSTATUS = build/emu-$(notdir $@).status
EMUSTAT = echo $$? > $(abspath $(EMUSTATUS))
EMUOK = test "`cat $(EMUSTATUS)`" = 0 \
	|| { echo "*** the emulator exited `cat $(EMUSTATUS)`: what is above is not a session"; exit 1; }

# ---- the gate that says the loader broke ----
# The $(CPMDISK) rule is in the Makefile: `all' builds the release medium.
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
EDVERIFYFMT = $(OSSEL)ED TEST.TXT\r\\gi\rhello from ED on the C900\rsecond line of text\rthird line 42\r\032e\rTYPE TEST.TXT\rED TEST.TXT\r\043a\r-b\ri\rappended fourth line\r\032e\rTYPE TEST.TXT\rDIR *.TXT\r$(ENDIN)
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
# GENCMD sizes its work from the base page: it scans its whole data group
# once per output record, and that group is a full 64 KB.  1.9 million
# interpreted 8086 instructions in one of the five programs verify-i86
# runs is past the default budget.  The five together measure 1.553
# billion, and the ceiling is a ceiling -- the session stops when its
# input is consumed, so the slack costs nothing.
I86MAX ?= 1700000000

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
V1EDITFMT = $(OSSEL)XDIR M*.*\001\001\001\001\001\001\001\001\010\rDR M*.*\002\006I\r\027\rTYPE HELLO.TXTJUNK\001\001\001\001\013\r$(ENDIN)
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
# CONBRK.Z8K (src/tests/conbrk.c) is the guest half: `CONBRK P nnnn' prints
# nnnn "NNNN " tokens one character at a time through BDOS function 2 --
# conbrk()'s own path -- after a preamble that forces conbrk()'s poll
# counter to a known zero, so the first poll inside the measured loop is
# exactly its 8th character (CONBRK_POLL, conbdos.c) and not some
# unknowable offset left by CCP's own command-line echo.  `CONBRK R'
# reports the BDOS's program return code (function 108).
#
# HOW A KEYSTROKE REACHES A PROGRAM THAT IS PRINTING.  It could not,
# until the emulator grew a primitive for it, and an earlier revision had to strip
# the ^S/^Q/^C sessions out of this target for that reason: the
# emulator PACES --input, holding each byte until the guest looks ready
# to read it (a prompt printed, the console quiet, or the guest spinning
# on the receiver), because a byte handed over early is swallowed by
# whatever read the guest is actually in.  A guest in a print loop never
# looks ready -- so the ^S arrived only once CONBRK had finished, which
# is the one moment it means nothing.  The emulator now says this
# explicitly instead of inferring it:
#
#   \\i   marks ONE byte as type-ahead -- handed to the receiver the
#        moment it is free, with no pacing at all, the way a person
#        typing ahead of a running program delivers one.  The rest of
#        the script stays paced, which is why the ^C session's
#        following `CONBRK R' still waits for its A> prompt.
#   --input-mark=CONBRK-START holds those type-ahead bytes until the
#        guest has PRINTED that text.  "Send it the moment that
#        appears" is the one synchronisation this test can state
#        exactly, and unlike an instruction count it does not move when
#        the BDOS or the CCP is rebuilt.
#
# CONBRK-START is printed under CM_NOSTOP, with conbrk()'s counter
# forced to zero and no poll made, so the control byte is sitting in the
# receiver before the measured loop emits its first character -- the
# type-ahead conbrk.c's own header has always assumed.  \023/\021/\003
# are ^S/^Q/^C: unlike \\i they are printf OCTAL escapes and reach the
# guest as the real bytes.
#
# Four sessions, one `CONBRK P' invocation, differing only in what is
# typed at it:
#
#   plain log   nothing typed -- the control case, and the regression
#               the widening could have introduced silently: the widened
#               poll interval must not disturb an ORDINARY run.
#   halt log    ^S and nothing else ever -- proves ^S stops output, by
#               proving the session neither completes nor returns to a
#               prompt.
#   resume log  ^S then ^Q -- proves ^Q resumes it LOSSLESSLY, by
#               proving the session does complete with the same exact,
#               unbroken sequence the plain one produced.
#   ^C log      ^C -- proves ^C warm boots: the program is abandoned
#               where it stood, yet A> returns with nothing sent to
#               release it, which the halt session shows a ^S does not
#               do.  tests/conbrkcheck.py's header records why RC_CTLC
#               is NOT read back afterward (the CCP clears the return
#               code before every command it runs).
#
# The stopped sessions also carry the poll interval's own bound:
# whatever prefix of the pattern got out before the stop must be an
# exact, unbroken prefix no longer than CONBRK_POLL - 1.  conbrkcheck.py
# reads CONBRK_POLL out of src/bdos/conbdos.c rather than restating it.
#
# The halt session is the one run here that is SUPPOSED to hit its
# instruction budget -- a stopped guest never goes idle at a prompt, so
# there is nothing to stop the run -- and it pays that budget in full,
# so it gets its own smaller one.  CBRKHALTMAX is about three times the
# ~47M instructions a COMPLETE `CONBRK P 0200' session takes: long past
# where an unstopped run would have finished and gone quiet at A>.
CBRKTOKENS = 0200
CBRKHALTMAX = 150000000
CBRKMARK = CONBRK-START
CBRKFMT       = $(OSSEL)CONBRK P $(CBRKTOKENS)\rCONBRK R\r$(ENDIN)
CBRKHALTFMT   = $(OSSEL)CONBRK P $(CBRKTOKENS)\r\\i\023
CBRKRESUMEFMT = $(OSSEL)CONBRK P $(CBRKTOKENS)\r\\i\023\\i\021
CBRKCTLCFMT   = $(OSSEL)CONBRK P $(CBRKTOKENS)\r\\i\003CONBRK R\r
# CBRKIMG, not $(CPMDISK): CONBRK.Z8K is an exerciser (ATEST), stripped
# from the release medium $(CPMDISK) builds -- this needs the dev image.
CBRKIMG = build/conbrktest.bin
CBRKLOG = build/verify-conbrk.log
CBRKHALTLOG   = build/verify-conbrk-halt.log
CBRKRESUMELOG = build/verify-conbrk-resume.log
CBRKCTLCLOG   = build/verify-conbrk-ctlc.log
.PHONY: verify-conbrk
verify-conbrk: all
	$(MKDISK) $(CBRKIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CBRKIMG)) \
		--input="$$(printf '$(CBRKFMT)')" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } | tee $(abspath $(CBRKLOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CBRKIMG)) \
		--input="$$(printf '$(CBRKHALTFMT)')" --input-mark='$(CBRKMARK)' \
		--max=$(CBRKHALTMAX) --stop-on=none 2>/dev/null; \
		$(EMUSTAT); } | tee $(abspath $(CBRKHALTLOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CBRKIMG)) \
		--input="$$(printf '$(CBRKRESUMEFMT)')" --input-mark='$(CBRKMARK)' \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } | tee $(abspath $(CBRKRESUMELOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CBRKIMG)) \
		--input="$$(printf '$(CBRKCTLCFMT)')" --input-mark='$(CBRKMARK)' \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } | tee $(abspath $(CBRKCTLCLOG))
	@$(EMUOK)
	python3 tests/conbrkcheck.py $(CBRKTOKENS) $(CBRKLOG) $(CBRKHALTLOG) \
		$(CBRKRESUMELOG) $(CBRKCTLCLOG)

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
# ---- the segment pool's sizing, host side (src/bios/pgalloc.c) ----
# The emulator's RAM is 1 MB and nothing else, so pginit()'s arithmetic for
# 512 KB, 2560 KB and the ceiling runs here: slots served, their segments
# (the first seven must stay 0x28..0x2E), and the refusals.
build/pgtest: tests/pgtest.c src/bios/pgalloc.c src/bios/c900cfg.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -Isrc/bios -o $@ tests/pgtest.c
pgtest: build/pgtest
	build/pgtest
splitcheck: build/splitchk
	@bins=`ls $(DISKA)/*.Z8K 2>/dev/null`; \
	if [ -z "$$bins" ]; then \
		echo "splitcheck: no .Z8K binaries in $(DISKA) -- run \`make all' first"; \
		echo "splitcheck: refusing to report success over an empty set"; \
		exit 1; \
	fi; \
	echo "splitcheck: scanning `echo $$bins | wc -w` binaries"; \
	build/splitchk $$bins

# ---- CP/M-86 shim, host side (src/shim/i86*.c) ----
# i86test: the .CMD loader's refusals, an independently transcribed 8086
# length map, one execution test per implemented instruction class, a
# differential of the lazy flag scheme against a naive reference over every
# byte operand pair -- about 790,000 cases, a second and a half -- the INT
# 0E0h seam's calling convention, and then three of DRI's own binaries on
# top of all of it: PIP.CMD copying a file, SUBMIT.CMD writing a $$$.SUB,
# and GENCMD.CMD converting an .H86 into a .CMD this same shim then loads
# and runs.
#
# THAT LAST ONE IS CPM86-STAGE-ONE.md §5.3 STEP 1, and not the gate.  Step 1
# is "run PIP against it on the host, with the BDOS calls stubbed"; the gate
# is the same command on the emulator through the real BDOS, and §5.1's whole
# argument is that host C passing is not a target verdict.  What step 1 buys
# is the dynamic mix: which instructions PIP really executes, which BDOS
# functions it really calls (function 45, which the plan's "fns 0-40" scope
# did not have), and K2's flag-read rate.
#
# Host-only, and nothing else in the tree depends on it yet: the shim still
# has no target build.  What blocks one is not the seam -- that is done --
# but segment acquisition: a small-model .CMD needs two 64 KB segments, and
# a TPA program has no way to map one.  mapseg_ is resident (src/bios/crt.s
# :251), BDOS function 50's allow-list does not reach it (src/bdos/iosys.c
# bioscl), and the alternative is `soutb' after function 62 -- assembly,
# which §5.2 rule 2 puts after the gate and not before it.  Giving a TPA
# program a segment is a system decision, not a shim decision.
#
# It is wired now rather than later for the reason CPM86-STAGE-ONE.md §5.1
# gives -- splittest and splitchk were written after the code they test, and
# the split-I/D shim's three-day debugging tail is what that cost.
#
# It reads .CMD files from two places, and they answer different questions.
# src/shim/tests/i86corpus/ holds DRI's own CP/M-86 programs (PIP, ED, GENCMD,
# SUBMIT, ASM86, STAT, HELP, TOD, DDT86, from the cpm86pc drop, under the
# 9 July 2022 DRDOS, Inc. grant): real headers, and real prologues that
# execute to their first BDOS call.  They are TEST INPUT ONLY -- only the
# verify targets' own scratch disks stage them,
# and the shim is for foreign software, never a way to acquire a utility
# (CPM86-STAGE-ONE.md §2.4).  build/cmdfix/ holds what no real file can
# supply -- a nonzero A-Base, an oversized group, malformed headers -- and
# is generated by tools/mkcmdfix.py on the way in.  That generator also
# writes the two fixtures that are not .CMDs: I86HEX.H86, GENCMD's input,
# real DRI hex with real checksums around a hand-assembled 8086 program,
# and I86T.A86, ASM86's.
I86SRC = src/shim/i86dec.c src/shim/i86exec.c src/shim/i86load.c \
	 src/shim/i86bdos.c src/shim/gdpb.c src/shim/segclr.c
I86CORPUS = src/shim/tests/i86corpus
I86FIX = build/cmdfix
.PHONY: i86test
build/i86test: src/shim/tests/i86test.c $(I86SRC) src/shim/i86.h src/shim/gdpb.h src/shim/conmode.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -DHOSTCC -o $@ src/shim/tests/i86test.c $(I86SRC)
$(I86FIX)/MANIFEST: tools/mkcmdfix.py | $(OBJDIR)
	python3 tools/mkcmdfix.py $(I86FIX)
i86test: build/i86test $(I86CORPUS)/SOURCES $(I86FIX)/MANIFEST
	build/i86test $(I86CORPUS) $(I86FIX)

# ---- CP/M-80 shim, host side (src/shim/z80*.c) ----
# z80test: an independently transcribed 8080/Z80 length map, the whole
# 256-byte base map, a differential of the lazy flag scheme against a
# reference written from the part's own definitions over every byte
# operand pair -- 2,097,152 ALU cases plus every input to INR, DCR, DAA
# and the rotates -- one execution test per implemented class, an
# explicit refusal for every class that is NOT implemented, the .COM
# loader's refusals including the GENCOM 0xC9 prefix, the CALL 5 seam's
# calling convention, and then two of DRI's own CP/M 3 programs running
# on top of all of it.
#
# THOSE LAST TWO ARE Z80-STAGE-ONE.md §5.3 STEP 1, and not the gate.
# Step 1 is "run them against it on the host, with the BDOS calls
# stubbed"; the gate is the same command on the emulator through the
# real BDOS, and §5.1's whole argument -- inherited from the CP/M-86
# lane, which inherited it from the split-I/D shim's three-day
# debugging tail -- is that host C passing is not a target verdict.
#
# What step 1 bought here, in one run each:
#   - DUMP.COM's SECOND BDOS call is function 109, get/set console
#     mode, and its fourth is function 49, get/set SCB.  The map had
#     been written to 52 on the CP/M-86 lane's pattern.  A static
#     census counts instructions, not calls, and could not have seen
#     either; the fix is in z80bdos.c and it is the same finding the
#     CP/M-86 seam got from function 45.
#   - The SCB and the character control block are the two places where
#     a CP/M 3 structure holds an ADDRESS, and ours are 32-bit XADDRs
#     where the 8080's are two bytes (src/bdos/conbdos.c:423-427,
#     src/bdos/scb.h).  Both are handled explicitly rather than passed
#     through, which is a thing a design document can assert and only a
#     run can force.
#   - K2's flag-read rate: 13 % over a complete PIP run, 6 % over DUMP.
#     The lazy scheme pays and has not degraded to an eager one.
#
# Host-only, and nothing else in the tree depends on it: the shim has
# no target build yet, for the same reason the CP/M-86 one does not.
# What blocks it is not the seam -- that is done, and simpler here than
# there, because a CP/M-80 guest is ONE 64 KB space and there is no DMA
# base to rewrite -- but segment acquisition: the guest wants a whole
# host segment and a TPA program has no way to map one.  mapseg_ is
# resident (src/bios/crt.s:274), BDOS function 50's allow-list does not
# reach it (src/bdos/iosys.c bioscl), and the alternative is `soutb'
# after function 62, which is assembly, which §5.2 rule 2 puts after
# the gate and not before it.  Giving a TPA program a segment is a
# system decision, not a shim decision, and it is the SAME decision the
# CP/M-86 lane is waiting on -- one ruling unblocks both.
#
# src/shim/tests/z80corpus/ holds four of DRI's own CP/M 3 programs (PIP, DUMP,
# SUBMIT, SAVE, from the cpm3bin drop, under the 9 July 2022 DRDOS,
# Inc. grant).  They are TEST INPUT ONLY: DUMP.COM is staged onto
# verify-z80's OWN scratch medium below and onto nothing else -- not the
# release disk, not the development disk -- and the shim is for foreign
# software, never a way to acquire a utility we already have a faster
# native one of.  See that directory's SOURCES for what each of the four
# is here to answer.
Z80SRC = src/shim/z80dec.c src/shim/z80mnem.c src/shim/z80exec.c \
	 src/shim/z80load.c src/shim/z80bdos.c src/shim/gdpb.c src/shim/segclr.c
Z80CORPUS = src/shim/tests/z80corpus
.PHONY: z80test
build/z80test: src/shim/tests/z80test.c $(Z80SRC) src/shim/z80.h src/shim/gdpb.h src/shim/conmode.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -DHOSTCC -o $@ src/shim/tests/z80test.c $(Z80SRC)
z80test: build/z80test $(Z80CORPUS)/SOURCES
	build/z80test $(Z80CORPUS)

# ---- CP/M-80 shim, ON THE MACHINE: Z80-STAGE-ONE.md's step 2 ----
# The gate the whole shim was waiting on, and the reason BIOS function
# 25 exists.  Z80.Z8K (src/shim/z80.c) asks the BIOS for a 64 KB segment,
# is given one, loads DRI's own DUMP.COM into it, builds the guest's
# page zero there, dumps it back out, and runs the program through the
# REAL BDOS -- which is the whole difference between this and z80test
# above, where the BDOS is a stub in the same process.
#
# The known answer is the one the host test uses, deliberately: Z80IN.BIN
# is 128 bytes counting 0x00 up to 0x7F, so DUMP's own output has to
# begin "0000: 00 01 02 03" and end "0070: 70 71 72 73", and the ASCII
# column has to be there.  Identical input, identical expected text, two
# machines -- so a divergence is a target finding and not a difference of
# fixtures.  THE COUNTS ARE ASSERTED TOO, on verify-i86's pattern and
# for the reason verify-z80pip below gives: the transcript says DUMP
# printed the right bytes, and only the instruction, BDOS-call and
# flag-materialisation triple says it got there the same way.
#
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
# Its own medium, built here rather than by `all': DUMP.COM is DRI's and
# is test input (see the note above), Z80IN.BIN is generated, and neither
# belongs on a disk anyone ships.  The drive-A tree is $(DISKA) -- the
# development staging `all' just built, which is where Z80.Z8K is -- plus
# those two.
#
# THIS TARGET IS ALLOWED TO ANSWER "no segment".  On a 512 KB machine
# there is no free physical page and the gate cannot run at all; the
# emulator models 1 MB (its src/bus.c:18, segments 0x08-0x17), so under
# the emulator it does, and that is the machine this result covers.  A
# 512 KB run would print the refusal and this target would fail, which is
# correct: it would be reporting that the shim needs memory the machine
# has not got, not that anything is broken.
Z80DISK	= build/z80disk
Z80CPMA	= build/z80-cpma.img
Z80IMG	= build/z80test.bin
Z80LOG	= build/verify-z80.log
.PHONY: verify-z80
verify-z80: all
	@rm -rf $(Z80DISK)
	@mkdir -p $(Z80DISK)
	@cp $(DISKA)/* $(Z80DISK)/
	cp $(Z80CORPUS)/DUMP.COM $(Z80DISK)/DUMP.COM
	python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(128)))' 		> $(Z80DISK)/Z80IN.BIN
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) 		--label-mode $(LABELMODE) $(Z80CPMA) $(CPMA_BLOCKS) $(Z80DISK)
	$(MKDISK) $(Z80IMG) $(CPMSYS) $(Z80CPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(Z80IMG)) 		--input="$(OSSEL)$(SESS1)Z80 DUMP.COM Z80IN.BIN" 		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } 		| tee $(abspath $(Z80LOG))
	@$(EMUOK)
	@grep -q 'z80: guest segment 29, staging segment 2A' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- BIOS function 25 handed out no segment."; 		     echo "            That is the whole gate: without it the guest has"; 		     echo "            no 64 KB and nothing below this can run."; exit 1; }
	@grep -q 'z80: load: ok' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- DUMP.COM did not load"; exit 1; }
	@grep -q '0100: C3' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- the image is not in the guest segment"; exit 1; }
	@grep -q '0000: 00 01 02 03' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- DUMP did not print the file's own bytes"; exit 1; }
	@grep -q '0070: 70 71 72 73' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- DUMP stopped before the end of the record"; exit 1; }
	@grep -q '0123456789' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- DUMP printed no ASCII column"; exit 1; }
	@grep -q 'z80: 14314 instructions, 605 BDOS calls, 164 flag' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- the target run did not take the same path"; 		     echo "            through DUMP as the host run (14,314 instructions,"; 		     echo "            605 BDOS calls, 164 flag materialisations --"; 		     echo "            src/shim/tests/z80test.c t_dump()).  Read the divergence;"; 		     echo "            do not relax this."; exit 1; }
	@grep -q 'z80: the guest terminated' $(Z80LOG) 		|| { echo "verify-z80: FAIL -- the guest did not terminate cleanly"; exit 1; }
	@echo "verify-z80: PASS -- a TPA program was given a 64 KB segment and ran"
	@echo "            DRI's DUMP.COM in it through the real BDOS"


# ---- CP/M-80 shim, ON THE MACHINE, second program: PIP.COM ----
# verify-z80 above proves the SEAM: a segment was handed out, an image
# was placed in it, and DRI's DUMP.COM ran through the real BDOS.  What
# it cannot prove is that the shim COMPUTES the same thing on the two
# machines, because DUMP's answer is a transcript and a transcript is
# graded by grep.  This target is the other half, and it is the same
# argument verify-i86 makes for choosing PIP over ED or SUBMIT: PIP's
# answer is A COPY, and a copy is a known-answer test that `cmp'
# decides.
#
# THE KNOWN ANSWER IS THE HOST'S, byte for byte and instruction for
# instruction.  VERIFY.IN is the 1,024 bytes src/shim/tests/z80test.c t_pip()
# builds -- the identical formula the CP/M-86 lane's I86IN.TXT uses,
# printable so PIP's default ASCII mode meets no ^Z, and eight whole
# 128-byte records so a record-granular copy is byte-exact -- and the
# command tail is t_pip()'s own `VERIFY.OUT=VERIFY.IN'.  The host run is
# 13,024 instructions and 29 BDOS calls; the target run is asserted to be
# those same two numbers.  THAT ASSERTION IS THE POINT OF THIS TARGET.
# Two independent C compilations of the same interpreter -- one on a
# 64-bit host against a stub CP/M, one on a Z8001 against the real BDOS
# -- taking the identical path through eight and a half kilobytes of
# somebody else's 1982 code is the strongest statement this lane can
# make: it says the host's checks are checks about this machine.  A
# divergence here is a finding and wants reading, not relaxing.
#
# IT ALREADY EARNED ITS KEEP.  The first target run copied the file
# correctly and took 13,024 instructions and 29 BDOS calls against the
# host's then-15,763 and 74.  Nothing was wrong with the shim: the host
# stub in src/shim/tests/z80test.c had BDOS function 44, set multi-sector count,
# in the same `return 0' group as 13, 14, 28 and 45, so PIP's
# setmulti(8) was accepted and ignored and its eight-record transfers
# came back one record at a time.  The stub now implements
# src/bdos/bdosrw.c multio() and the two runs agree.  A grep on a
# transcript could not have found that; only asserting the pair could.
#
# Its own medium, for the reason verify-z80 gives: PIP.COM is DRI's and
# is test input (src/shim/tests/z80corpus/SOURCES), VERIFY.IN is generated, and
# neither belongs on a disk anyone ships.
#
# THIS TARGET IS ALLOWED TO ANSWER "no segment", same as verify-z80.
Z80PDISK = build/z80pipdisk
Z80PCPMA = build/z80pip-cpma.img
Z80PIMG	 = build/z80piptest.bin
Z80PLOG	 = build/verify-z80pip.log
Z80PIN	 = build/z80in.txt
.PHONY: verify-z80pip
verify-z80pip: all
	@rm -rf $(Z80PDISK)
	@mkdir -p $(Z80PDISK)
	@cp $(DISKA)/* $(Z80PDISK)/
	cp $(Z80CORPUS)/PIP.COM $(Z80PDISK)/PIP.COM
	python3 -c 'import sys; sys.stdout.buffer.write(bytes((0x20 + (k * 7 + (k >> 5)) % 0x5e) for k in range(1024)))' \
		> $(Z80PIN)
	cp $(Z80PIN) $(Z80PDISK)/VERIFY.IN
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(Z80PCPMA) $(CPMA_BLOCKS) $(Z80PDISK)
	$(MKDISK) $(Z80PIMG) $(CPMSYS) $(Z80PCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(Z80PIMG)) \
		--input="$(OSSEL)$(SESS1)Z80 PIP.COM VERIFY.OUT=VERIFY.IN" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(Z80PLOG))
	@$(EMUOK)
	@grep -q 'z80: guest segment 29, staging segment 2A' $(Z80PLOG) \
		|| { echo "verify-z80pip: FAIL -- BIOS function 25 handed out no segment."; exit 1; }
	@grep -q 'z80: load: ok' $(Z80PLOG) \
		|| { echo "verify-z80pip: FAIL -- PIP.COM did not load"; exit 1; }
	@grep -q '0080: 15 20 56 45 52 49 46 59' $(Z80PLOG) \
		|| { echo "verify-z80pip: FAIL -- the command tail did not reach 0x0080"; exit 1; }
	@grep -q 'z80: 13024 instructions, 29 BDOS calls, 1205 flag' $(Z80PLOG) \
		|| { echo "verify-z80pip: FAIL -- the target run did not take the same"; \
		     echo "            path through PIP as the host run (13,024"; \
		     echo "            instructions, 29 BDOS calls, 1205 flag"; \
		     echo "            materialisations -- src/shim/tests/z80test.c"; \
		     echo "            t_pip()).  Read the divergence; do not relax this."; exit 1; }
	@grep -q 'z80: the guest terminated' $(Z80PLOG) \
		|| { echo "verify-z80pip: FAIL -- PIP did not terminate cleanly"; exit 1; }
	@# --- the known answer: the copy, off the disk the machine wrote it to ---
	dd if=$(Z80PIMG) of=build/z80pip-after.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/z80pip-fs
	python3 tools/mkcpmfs.py --extract build/z80pip-after.img build/z80pip-fs
	@test -f build/z80pip-fs/VERIFY.OUT \
		|| { echo "verify-z80pip: FAIL -- PIP made no VERIFY.OUT"; exit 1; }
	cmp $(Z80PIN) build/z80pip-fs/VERIFY.OUT
	@echo "verify-z80pip: PASS -- DRI's PIP.COM ran on the machine in an"
	@echo "            allocated 64 KB segment through the real BDOS, took the"
	@echo "            same 13,024 instructions and 29 BDOS calls as the host"
	@echo "            run, and the copy it made is byte-identical to its input"


# ---- CP/M-80 shim, ON THE MACHINE, third program: SAVE.COM ----
# The one program in the corpus that IS an RSX.  DUMP and PIP prove the
# seam and the arithmetic; SUBMIT proves a module is placed, relocated
# and chained.  None of the three proves a module is ever ENTERED, and
# until this target nothing anywhere did: SAVE.COM's .COM half is a bare
# RET, so a shim that loads it and jumps to 0x0100 leaves immediately
# with the chain untouched.
#
# HOW THE MODULE IS REACHED.  A CP/M 3 CCP asks for an RSX-only command a
# second time, with function 59, and that second ask is what the module
# takes.  SAVE's takes it, points the guest's warm-boot vector at itself,
# and passes the call on down the chain; the warm boot that follows is
# the entry.  src/shim/z80load.c plants those three instructions -- MVI
# C,59 / CALL 5 / JMP 0 -- above the TPA and starts an RSX-only image
# there instead of at the RET.
#
# WHAT CARRIES THE VERDICT.  "CP/M 3 SAVE - Version 3.1" is printed by
# code inside the module, from an address the relocator computed: it
# cannot appear unless the module was placed, chained, entered and
# relocated correctly.  The saved file is the other half -- SAVE is
# asked for the guest's own first 256 bytes, so the file has to carry
# page zero as the loader built it, down to the BDOS vector naming the
# module (0xDF06) rather than the seam, which is the chain itself in a
# file the machine wrote.  A load that never entered the module produces
# no file at all.
#
# The session is the host run's, key for key (src/shim/tests/z80test.c t_save),
# so the instruction, BDOS-call and flag triple is asserted the way
# verify-z80's and verify-z80pip's are.
#
# THIS TARGET IS ALLOWED TO ANSWER "no segment", same as verify-z80.
Z80SDISK = build/z80savedisk
Z80SCPMA = build/z80save-cpma.img
Z80SIMG	 = build/z80savetest.bin
Z80SLOG	 = build/verify-z80save.log
.PHONY: verify-z80save
verify-z80save: all
	@rm -rf $(Z80SDISK)
	@mkdir -p $(Z80SDISK)
	@cp $(DISKA)/* $(Z80SDISK)/
	cp $(Z80CORPUS)/SAVE.COM $(Z80SDISK)/SAVE.COM
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(Z80SCPMA) $(CPMA_BLOCKS) $(Z80SDISK)
	$(MKDISK) $(Z80SIMG) $(CPMSYS) $(Z80SCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(Z80SIMG)) \
		--input="$(OSSEL)$(SESS1)Z80 SAVE.COM SAVED.BIN\r\gSAVED.BIN\r0000\r00FF\r" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(Z80SLOG))
	@$(EMUOK)
	@grep -q 'z80: guest segment 29, staging segment 2A' $(Z80SLOG) \
		|| { echo "verify-z80save: FAIL -- BIOS function 25 handed out no segment."; exit 1; }
	@grep -q 'z80: load: ok' $(Z80SLOG) \
		|| { echo "verify-z80save: FAIL -- SAVE.COM did not load"; exit 1; }
	@grep -q '0100: C9' $(Z80SLOG) \
		|| { echo "verify-z80save: FAIL -- the .COM half is not the RET an"; \
		     echo "                RSX-only image consists of"; exit 1; }
	@# --- the verdict: text that exists only inside the module ---
	@grep -q 'CP/M 3 SAVE - Version 3.1' $(Z80SLOG) \
		|| { echo "verify-z80save: FAIL -- the RSX was never entered.  The banner"; \
		     echo "                is printed from inside the relocated module, so"; \
		     echo "                a load that placed and chained it correctly and"; \
		     echo "                still never reached it looks exactly like this."; exit 1; }
	@grep -q 'Beginning hex address' $(Z80SLOG) \
		|| { echo "verify-z80save: FAIL -- the module was entered but did not run"; \
		     echo "                on to ask for a range"; exit 1; }
	@grep -q 'z80: 1520 instructions, 21 BDOS calls, 47 flag' $(Z80SLOG) \
		|| { echo "verify-z80save: FAIL -- the target run did not take the same"; \
		     echo "                path through SAVE as the host run (1,520"; \
		     echo "                instructions, 21 BDOS calls, 47 flag"; \
		     echo "                materialisations -- src/shim/tests/z80test.c t_save())."; \
		     echo "                Read the divergence; do not relax this."; exit 1; }
	@grep -q 'z80: the guest terminated' $(Z80SLOG) \
		|| { echo "verify-z80save: FAIL -- the guest did not terminate cleanly"; exit 1; }
	@# --- the known answer: page zero, off the disk the machine wrote it to ---
	dd if=$(Z80SIMG) of=build/z80save-after.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/z80save-fs
	python3 tools/mkcpmfs.py --extract build/z80save-after.img build/z80save-fs
	@test -f build/z80save-fs/SAVED.BIN \
		|| { echo "verify-z80save: FAIL -- the module wrote no file"; exit 1; }
	@python3 -c 'import sys; d = open("build/z80save-fs/SAVED.BIN","rb").read(); sys.exit(0 if len(d) == 256 and d[:0x5c] == b"\xc3\x03\xf2\x00\x00\xc3\x06\xdf" + bytes(0x54) else 1)' \
		|| { echo "verify-z80save: FAIL -- the saved bytes are not the guest's page"; \
		     echo "                zero: two records, the warm-boot JMP, and a BDOS"; \
		     echo "                vector naming the module at 0xDF06 rather than"; \
		     echo "                the seam at 0xE406."; exit 1; }
	@echo "verify-z80save: PASS -- DRI's SAVE.COM was loaded as the RSX-only"
	@echo "            image it is, its module was entered through the CCP's"
	@echo "            second function 59, and it wrote the guest's own page"
	@echo "            zero out to a file on the machine"

# ---- CP/M-80 shim, ON THE MACHINE: console polling ----
# POLL80.COM reads a key through the BIOS CONIN vector and echoes it, then
# 64 times writes a dot with function 6 and polls with E = 0FFh.  A poll
# must answer 0 at once: one that waits never reaches POLL80 DONE.
#
#	lhld 1 / lxi d,6 / dad d / lxi d,back / push d / pchl	; CONIN
#   back: mov e,a / mvi c,2 / call 5 / mvi b,64
#   loop: push b / mvi e,'.' / mvi c,6 / call 5
#	mvi e,0ffh / mvi c,6 / call 5 / pop b / ora a / jnz bad
#	dcr b / jnz loop / lxi d,done / jmp out
#   bad:  lxi d,key
#   out:  mvi c,9 / call 5 / jmp 0
Z80POLLHEX = 2a0100 110600 19 110c01 d5 e9 \
	5f 0e02 cd0500 0640 \
	c5 1e2e 0e06 cd0500 \
	1eff 0e06 cd0500 c1 b7 c23201 \
	05 c21401 113d01 c33501 \
	114b01 \
	0e09 cd0500 c30000
Z80POLLDISK = build/z80polldisk
Z80POLLCPMA = build/z80poll-cpma.img
Z80POLLIMG  = build/z80polltest.bin
Z80POLLLOG  = build/verify-z80poll.log
.PHONY: verify-z80poll
verify-z80poll: all
	@rm -rf $(Z80POLLDISK)
	@mkdir -p $(Z80POLLDISK)
	@cp $(DISKA)/* $(Z80POLLDISK)/
	python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex("$(Z80POLLHEX)") + b"\r\nPOLL80 DONE$$\r\nPOLL80 KEY$$")' \
		> $(Z80POLLDISK)/POLL80.COM
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(Z80POLLCPMA) $(CPMA_BLOCKS) $(Z80POLLDISK)
	$(MKDISK) $(Z80POLLIMG) $(CPMSYS) $(Z80POLLCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(Z80POLLIMG)) \
		--input="$(OSSEL)$(SESS1)Z80 POLL80.COM\r\gk" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(Z80POLLLOG))
	@$(EMUOK)
	@grep -q 'z80: load: ok' $(Z80POLLLOG) \
		|| { echo "verify-z80poll: FAIL -- POLL80.COM did not load"; exit 1; }
	@grep -q 'k\.' $(Z80POLLLOG) \
		|| { echo "verify-z80poll: FAIL -- BIOS CONIN did not return the key"; exit 1; }
	@grep -q 'POLL80 DONE' $(Z80POLLLOG) \
		|| { echo "verify-z80poll: FAIL -- a function 6 poll waited or saw a key"; exit 1; }
	@grep -q 'z80: the guest terminated' $(Z80POLLLOG) \
		|| { echo "verify-z80poll: FAIL -- the guest did not terminate cleanly"; exit 1; }
	@echo "verify-z80poll: PASS -- BIOS CONIN read a key and 64 function 6"
	@echo "            polls answered at once"

# ---- CP/M-80 shim, ON THE MACHINE: keys typed while the guest prints ----
# KEYQ80.COM announces itself, writes 200 characters with function 2, and
# then reads four keys through the BIOS CONIN vector, echoing each.  The
# four are typed AT THE OUTPUT: the mark releases the first the moment the
# announcement appears, and each of the rest as the receiver frees.
#
# The BDOS polls the console every eight output characters and KEEPS what
# it finds -- ^S and ^Q are swallowed, anything else is held in one byte
# per console -- so with the poll left on the four never reach the guest
# and its first CONIN waits for a key nobody will type again.  The shim
# turns the poll off for the length of a run.
#
#	lxi d,gomsg / mvi c,9 / call 5 / lxi h,200
#   dots: push h / mvi e,'.' / mvi c,2 / call 5 / pop h
#	dcx h / mov a,h / ora l / jnz dots / mvi b,4
#   keys: push b / lhld 1 / lxi d,6 / dad d / lxi d,back / push d / pchl
#   back: mov e,a / mvi c,2 / call 5 / pop b / dcr b / jnz keys
#	lxi d,donemsg / mvi c,9 / call 5 / jmp 0
KEYQ80HEX = 113f01 0e09 cd0500 \
	21c800 \
	e5 1e2e 0e02 cd0500 e1 2b 7c b5 c20b01 \
	0604 \
	c5 2a0100 110600 19 112901 d5 e9 \
	5f 0e02 cd0500 c1 05 c21c01 \
	114b01 0e09 cd0500 c30000
KEYQ80DISK = build/keyq80disk
KEYQ80CPMA = build/keyq80-cpma.img
KEYQ80IMG  = build/keyq80test.bin
KEYQ80LOG  = build/verify-z80keyq.log
.PHONY: verify-z80keyq
verify-z80keyq: all
	@rm -rf $(KEYQ80DISK)
	@mkdir -p $(KEYQ80DISK)
	@cp $(DISKA)/* $(KEYQ80DISK)/
	python3 -c 'import sys; sys.stdout.buffer.write(bytes.fromhex("$(KEYQ80HEX)") + b"\r\nKEYQ80 GO$$" + b"\r\nKEYQ80 DONE$$")' \
		> $(KEYQ80DISK)/KEYQ80.COM
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(KEYQ80CPMA) $(CPMA_BLOCKS) $(KEYQ80DISK)
	$(MKDISK) $(KEYQ80IMG) $(CPMSYS) $(KEYQ80CPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(KEYQ80IMG)) \
		--input="$(OSSEL)$(SESS1)Z80 KEYQ80.COM\r\ia\ib\ic\id" \
		--input-mark='KEYQ80 GO' \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(KEYQ80LOG))
	@$(EMUOK)
	@grep -q 'z80: load: ok' $(KEYQ80LOG) \
		|| { echo "verify-z80keyq: FAIL -- KEYQ80.COM did not load"; exit 1; }
	@tr -d '\r' < $(KEYQ80LOG) | grep -q '\.\{40\}abcd' \
		|| { echo "verify-z80keyq: FAIL -- the four keys typed during the guest's"; \
		     echo "            output did not all reach BIOS CONIN, in order."; \
		     echo "            The console poll ate them."; exit 1; }
	@grep -q 'KEYQ80 DONE' $(KEYQ80LOG) \
		|| { echo "verify-z80keyq: FAIL -- the guest never finished reading"; exit 1; }
	@grep -q 'z80: the guest terminated' $(KEYQ80LOG) \
		|| { echo "verify-z80keyq: FAIL -- the guest did not terminate cleanly"; exit 1; }
	@echo "verify-z80keyq: PASS -- four keys typed at 200 characters of"
	@echo "            guest output all reached BIOS CONIN, in order"


# ---- the two 8080/Z80 decoders must answer the same thing ----
# The machine runs the assembly decoder (src/shim/z80deca.s); src/shim/
# z80dec.c stays the portable reference and is what the host suite
# compiles.  ZDECT links both -- the C one renamed -- and sweeps every
# first byte against ten operand patterns at five program counters, two
# of which put the instruction across 0xFFFF, comparing the length and
# every decoded field.  THIS IS WHAT MAKES A BACKPORT MECHANICAL: fix
# z80dec.c without fixing z80deca.s and this target says so.
ZDECDISK = build/zdecdisk
ZDECCPMA = build/zdec-cpma.img
ZDECIMG	 = build/zdectest.bin
ZDECLOG	 = build/verify-zdec.log
.PHONY: verify-zdec
verify-zdec: all $(UZDEC)
	@rm -rf $(ZDECDISK)
	@mkdir -p $(ZDECDISK)
	@cp $(DISKA)/* $(ZDECDISK)/
	cp $(UZDEC) $(ZDECDISK)/ZDECT.Z8K
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(ZDECCPMA) $(CPMA_BLOCKS) $(ZDECDISK)
	$(MKDISK) $(ZDECIMG) $(CPMSYS) $(ZDECCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(ZDECIMG)) \
		--input="$(OSSEL)ZDECT\r$(ENDIN)" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(ZDECLOG))
	@$(EMUOK)
	@grep -q 'ZDECT: start' $(ZDECLOG) \
		|| { echo "verify-zdec: FAIL -- ZDECT did not run at all"; exit 1; }
	@grep -q 'ZDECT: FAIL' $(ZDECLOG) \
		&& { echo "verify-zdec: FAIL -- the assembly decoder and the C one"; \
		     echo "             disagree; the line above names the opcode,"; \
		     echo "             the program counter and the field."; exit 1; } || true
	@grep -q 'ZDECT: ok' $(ZDECLOG) \
		|| { echo "verify-zdec: FAIL -- ZDECT did not finish the sweep"; exit 1; }
	@echo "verify-zdec: PASS -- both decoders answer the same thing for every"
	@echo "             opcode, operand pattern and program counter swept"


# ---- the two 8086 decoders must answer the same thing ----
# The machine runs the assembly decoder (src/shim/i86deca.s); src/shim/
# i86dec.c stays the portable reference.  IDECT links both -- the C one
# renamed -- and sweeps every first byte against every second byte under
# each prefix run, at program counters across 0xFFFF, comparing the
# length and every decoded field.  Fix i86dec.c without fixing
# i86deca.s and this target says so.
IDECDISK = build/idecdisk
IDECCPMA = build/idec-cpma.img
IDECIMG	 = build/idectest.bin
IDECLOG	 = build/verify-idec.log
IDECMAX	?= 1500000000
.PHONY: verify-idec
verify-idec: all $(UIDEC)
	@rm -rf $(IDECDISK)
	@mkdir -p $(IDECDISK)
	@cp $(DISKA)/* $(IDECDISK)/
	cp $(UIDEC) $(IDECDISK)/IDECT.Z8K
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(IDECCPMA) $(CPMA_BLOCKS) $(IDECDISK)
	$(MKDISK) $(IDECIMG) $(CPMSYS) $(IDECCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(IDECIMG)) \
		--input="$(OSSEL)IDECT\r$(ENDIN)" \
		--max=$(IDECMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(IDECLOG))
	@$(EMUOK)
	@grep -q 'IDECT: start' $(IDECLOG) \
		|| { echo "verify-idec: FAIL -- IDECT did not run at all"; exit 1; }
	@grep -q 'IDECT: FAIL' $(IDECLOG) \
		&& { echo "verify-idec: FAIL -- the assembly decoder and the C one"; \
		     echo "             disagree; the lines above name the field,"; \
		     echo "             the bytes and where they sat."; exit 1; } || true
	@grep -q 'IDECT: ok' $(IDECLOG) \
		|| { echo "verify-idec: FAIL -- IDECT did not finish the sweep"; exit 1; }
	@echo "verify-idec: PASS -- both decoders answer the same thing for every"
	@echo "             opcode and second byte, prefix run and position swept"


# ---- CP/M-86 shim, ON THE MACHINE: CPM86-STAGE-ONE.md's step 2 ----
# The other half of what BIOS function 25 was built for, and the same
# gate as verify-z80 above with one more segment in it: a small-model
# .CMD is TWO groups, code and data, so CPM86.Z8K (src/shim/i86.c) asks
# for two segments for the guest and a third to stage the file in, and
# gives the third one straight back.  It then loads DRI's own PIP.CMD
# into them, builds the base page, and runs the program through the REAL
# BDOS -- which is the whole difference between this and i86test above,
# where the BDOS is a stub in the same process.
#
# WHY PIP LEADS.  src/shim/tests/i86corpus/ holds PIP, ED, GENCMD and SUBMIT, and
# three of the four run here.  ED is the one that does not: it is
# interactive and its answer is a transcript.  PIP's answer is A COPY,
# and a copy is a known-answer test a byte comparison decides -- which is
# exactly the reason src/shim/tests/i86corpus/SOURCES gives for PIP being in the
# corpus at all.  It is also the corpus's heaviest user of the seam: 14
# distinct BDOS functions in the host run, including the random-record
# pair (35 and 36) whose field order src/shim/i86bdos.c ranswap() had to
# correct.  So it is first, and the other two are here for what they do
# that it does not -- see their own comments in the recipe below.
#
# THE KNOWN ANSWER IS THE HOST'S, deliberately, exactly as on the Z80
# lane.  I86IN.TXT is the 1024 bytes src/shim/tests/i86test.c section 8b copies --
# same formula, printable so PIP's ASCII mode has no ^Z to stop on, and
# eight whole records so a record-granular copy is byte-exact.  Identical
# input, identical expected output, two machines: a divergence is a
# target finding and not a difference of fixtures.  The host run is
# 9,260 instructions and 59 BDOS calls, and the target run is asserted to
# be those same two numbers: it MEASURED them on the first attempt, which
# is the strongest statement this gate can make -- the interpreter took
# the identical path through DRI's code on both machines, so the host's
# 920 checks are checks about this machine.  A divergence here is a
# finding and wants reading, not relaxing.  The copy is the other half:
# a transcript can agree and still have copied the wrong bytes.
#
# Its own medium, built here rather than by `all': PIP.CMD is DRI's and
# is test input (see src/shim/tests/i86corpus/SOURCES), I86IN.TXT is generated,
# and neither belongs on a disk anyone ships.
#
# THIS TARGET IS ALLOWED TO ANSWER "no segment", for the reason
# verify-z80 gives above: on a 512 KB machine there are none, the
# emulator models 1 MB, and this result covers that machine.
I86DISK	= build/i86disk
I86CPMA	= build/i86-cpma.img
I86IMG	= build/i86test.bin
I86LOG	= build/verify-i86.log
I86IN	= build/i86in.txt
I86SUB	= build/i86sub.txt
# Five programs, one session, in an order the session itself fixes.
# PIP is the gate stage one closed.  GENCMD is second: it is the binary
# that reads its own base-page paragraph count and refuses to work
# against a small one, so it is the target-side evidence for
# src/shim/i86load.c galloc(), and its input is a file rather than a
# command line -- build/cmdfix/I86HEX.H86, real DRI hex around a
# hand-assembled 8086 program (tools/mkcmdfix.py p_hex()).  SUBMIT is
# LAST, and it has to be: the file it writes is A:$$$.SUB, so OUR CCP
# picks it up and runs DIR and STAT out of it, which makes the native
# CCP the grader of a file a DRI program wrote through the shim and also
# ends the session's command stream.  That is why there is no `cmp' for
# that half -- the answer is consumed by the thing that proves it.
#
# I86MG.CMD is third, and it is the only multi-group .CMD there is: five
# groups, so five 64 KB segments plus the staging one, which is six of
# the machine's seven.  Nothing DRI shipped declares an extra, a stack or
# an auxiliary group -- all 15 files in the drop are small model or 8080
# -- so it is SYNTHESISED by tools/mkcmdfix.py p_multi() and it checks
# the placement from inside the guest rather than trusting the
# transcript: distinct segments behind ES, SS and DS, and an auxiliary
# group reached only through the paragraph i86bpage() published for it.
# It must come before SUBMIT for the same reason GENCMD does -- SUBMIT
# ends the session.
#
# I86PL.CMD is fourth, and it is the only caller of BDOS function 59 --
# program load -- there is.  The function exists for a debugger: DDT86 is
# the one binary in the drop that calls it, and its answer is an
# interactive transcript, which is no gate.  So the caller is synthesised
# too (tools/mkcmdfix.py p_pload()) and the program it loads is DRI's own
# PIP.CMD, off the real file system through the real BDOS.  It checks the
# load from inside the guest: the base page the loader answers with names
# the groups, and PIP's own prologue must be at the code paragraph named.
I86VERIFYIN = $(OSSEL)$(SESS1)CPM86 PIP.CMD I86OUT.TXT=I86IN.TXT\rCPM86 GENCMD.CMD I86HEX\rCPM86 I86MG.CMD\rCPM86 I86PL.CMD PIP.CMD\rCPM86 SUBMIT.CMD I86SUB\r
# The host run's own $$$.SUB, written by src/shim/tests/i86test.c section 8c as it
# runs, so that the target's copy is compared against bytes a run produced
# and not against bytes someone typed out.
build/i86sub-host.bin: build/i86test $(I86CORPUS)/SOURCES $(I86FIX)/MANIFEST
	build/i86test $(I86CORPUS) $(I86FIX) > build/i86test.log
# The same run writes both host references; this says so, rather than
# running i86test a second time to produce the other one.
build/i86hex-host.cmd: build/i86sub-host.bin

.PHONY: verify-i86
verify-i86: all build/i86sub-host.bin build/i86hex-host.cmd
	@rm -rf $(I86DISK)
	@mkdir -p $(I86DISK)
	@cp $(DISKA)/* $(I86DISK)/
	cp $(I86CORPUS)/PIP.CMD $(I86DISK)/PIP.CMD
	cp $(I86CORPUS)/SUBMIT.CMD $(I86DISK)/SUBMIT.CMD
	cp $(I86CORPUS)/GENCMD.CMD $(I86DISK)/GENCMD.CMD
	cp $(I86FIX)/I86HEX.H86 $(I86DISK)/I86HEX.H86
	cp $(I86FIX)/I86MG.CMD $(I86DISK)/I86MG.CMD
	cp $(I86FIX)/I86PL.CMD $(I86DISK)/I86PL.CMD
	python3 -c 'import sys; sys.stdout.buffer.write(bytes((0x20 + (k * 7 + (k >> 5)) % 0x5e) for k in range(1024)))' \
		> $(I86IN)
	cp $(I86IN) $(I86DISK)/I86IN.TXT
	python3 -c 'import sys; b = b"DIR\r\nSTAT\r\n"; sys.stdout.buffer.write(b + b"\x1a" * (128 - len(b)))' \
		> $(I86SUB)
	cp $(I86SUB) $(I86DISK)/I86SUB.SUB
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(I86CPMA) $(CPMA_BLOCKS) $(I86DISK)
	$(MKDISK) $(I86IMG) $(CPMSYS) $(I86CPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(I86IMG)) \
		--input="$(I86VERIFYIN)" \
		--max=$(I86MAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(I86LOG))
	@$(EMUOK)
	@# ONE HIGHER SINCE C10.  A session runs on console
	@# 1 (SESSION 1, typed first: since D8 a serial boot starts none) and it takes the first page out of
	@# the pool, so every number handed out below it moves up by one.
	@# The numbers are still pinned rather than matched loosely: what
	@# they say is that fn 25 handed out THREE DIFFERENT consecutive
	@# segments, and that is the gate.
	@grep -q 'i86: code segment 29, data segment 2A, staging segment 2B' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- BIOS function 25 handed out no segments."; \
		     echo "            That is the whole gate: a small-model .CMD needs"; \
		     echo "            two 64 KB groups and nothing below this can run."; exit 1; }
	@grep -q 'i86: header: ok' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- PIP.CMD's header did not parse"; exit 1; }
	@grep -q 'i86: model small, 2 groups, entry 0000' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the loader did not read PIP's real header"; exit 1; }
	@grep -q 'i86: place: ok' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the groups were not bound to segments"; exit 1; }
	@grep -q '0000: FF FF 00 00 10 00 FF 87 00 00 20 00' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the base page's segment table is not in"; \
		     echo "            the guest's data segment.  Six bytes to an entry:"; \
		     echo "            a 24-bit last byte offset then the base paragraph,"; \
		     echo "            so code ends at 0x00FFFF at 0x1000 and data at"; \
		     echo "            0x0087FF at 0x2000 -- PIP's header asks for G-Max"; \
		     echo "            0 and 2176, and src/shim/i86load.c galloc() grants"; \
		     echo "            the ask, not the G-Min floor"; exit 1; }
	@grep -q '0080: 15 20 49 38 36 4F 55 54' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the command tail did not reach DS:0080"; exit 1; }
	@grep -q '0000: 9C 58 FA 8C D9 8E D1 8D 26 84 01' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- PIP's own prologue is not in the code segment"; exit 1; }
	@grep -q 'i86: 9248 instructions, 58 BDOS calls' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the target run did not take the same path"; \
		     echo "            through PIP as the host run (9,248 instructions,"; \
		     echo "            58 BDOS calls -- src/shim/tests/i86test.c section 8b)"; exit 1; }
	@grep -q 'i86: slow segment resolutions 0, refused 0' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- PIP wrote a segment register we never handed"; \
		     echo "            out; K3's static census said it would not"; exit 1; }
	@grep -q 'i86: the guest terminated' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- PIP did not terminate cleanly"; exit 1; }
	@# --- the second program: DRI's GENCMD.CMD, and the allocation ---
	@# THIS IS THE BINARY THAT READS ITS OWN PARAGRAPH COUNT AND ACTS ON
	@# IT.  Against the G-Min floor src/shim/i86load.c galloc() used to
	@# return, GENCMD prints "INSUFFICIENT MEMORY TO CREATE CMD FILE" and
	@# quits; against the ask it grants now, it converts I86HEX.H86 and
	@# writes I86HEX.CMD.  So this pair of greps is the target's evidence
	@# for the allocation change, and the cmp below is its answer graded
	@# by the host's -- not, as the comment above once said, by itself.
	@grep -q 'INSUFFICIENT MEMORY' $(I86LOG) \
		&& { echo "verify-i86: FAIL -- GENCMD could not build its output"; \
		     echo "            buffer out of the paragraph count in its base"; \
		     echo "            page.  src/shim/i86load.c galloc() grants"; \
		     echo "            min(G-Max, CMD_MAXPAR); this says it did not."; exit 1; } \
		|| true
	@grep -q 'i86: 1911524 instructions, 38 BDOS calls' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the target run of GENCMD did not"; \
		     echo "            take the same path as the host run (1,911,524"; \
		     echo "            instructions, 38 BDOS calls -- src/shim/tests/i86test.c"; \
		     echo "            section 8d)"; exit 1; }
	@grep -q 'RECORDS WRITTEN 04' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- GENCMD did not report writing the"; \
		     echo "            four records of I86HEX.CMD"; exit 1; }
	@# --- the third program: I86MG.CMD, the compact/large model path ---
	@# The one program here that is not DRI's, and it has to be: no CP/M-86
	@# file in this tree declares an extra, a stack or an auxiliary group,
	@# so the only way to run i86place()'s compact and large arms on the
	@# MACHINE is to synthesise a file that does (tools/mkcmdfix.py
	@# p_multi()).  It is the same file src/shim/tests/i86test.c loads from
	@# build/cmdfix, so the two ends are again the same fixture.
	@grep -q 'i86: model large, 5 groups, entry 0000' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the loader did not read I86MG.CMD's"; \
		     echo "            five-group header as the large model."; exit 1; }
	@# THE SEGMENTS, and this is what the task was about.  28, 29 and 2A
	@# are taken before the header is read, exactly as they always were;
	@# 2A is given back after the file is staged; three more come from
	@# BIOS function 25 for the extra, stack and auxiliary groups.  Six
	@# segments held at once out of a pool of seven (src/bios/pgalloc.c).
	@# EVERY NUMBER HERE MOVED UP BY ONE AT C10 and nothing else about
	@# the claim did: the console-1 session (SESSION 1 since D8) holds the
	@# first page (src/bdos/proc.c psession), so the pool this run draws
	@# on starts one higher.  Six segments held at once out of the
	@# seven, with one of the seven now permanently spoken for, is the
	@# tightest this target has ever run -- and it still fits.
	@# THE NUMBERS MOVED DOWN BY 0x10 AT F5, and again nothing else about
	@# the claim did: the pool used to be logical segments 0x38..0x3E,
	@# which included the ROM's two display planes 0x3A and 0x3B, so this
	@# very target was proof that BIOS function 25 hands out the
	@# framebuffer (first-release review P1 #9).  PGSEGLO is now 0x28.
	@grep -q 'i86: extra segments: 2C 2D 2E' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- BIOS function 25 did not supply the"; \
		     echo "            three further segments a five-group .CMD"; \
		     echo "            needs.  The pool is seven, one is the console"; \
		     echo "            1 session's, and this run holds six: 29 2A 2C"; \
		     echo "            2D 2E for the groups and 2B to stage in."; exit 1; }
	@# One line per group: form, the guest paragraph it was given, the
	@# physical segment behind it.  The auxiliary group (form 5) is the
	@# one with no segment register at all -- it is reachable only through
	@# the base page -- so it is the one asserted by name.
	@grep -q 'i86: group 5 at 5000:0000, segment 2E' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the auxiliary group got no segment"; \
		     echo "            of its own.  src/shim/i86load.c i86place()"; \
		     echo "            gives one to every declared group."; exit 1; }
	@# And the guest's own verdict, which is the only one that is not a
	@# transcript reading itself: it writes markers through ES, SS and the
	@# auxiliary group's paragraph and reads every one of them back.  One
	@# segment behind two groups prints BAD.
	@grep -q 'I86MG BAD' $(I86LOG) \
		&& { echo "verify-i86: FAIL -- the multi-group guest found two of"; \
		     echo "            its groups sharing a segment."; exit 1; } \
		|| true
	@grep -q 'I86MG OK' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the multi-group guest did not reach"; \
		     echo "            its own verdict.  See build/verify-i86.log."; exit 1; }
	@# --- the fourth program: I86PL.CMD, BDOS function 59 ---
	@# The only place program load runs behind the real BDOS.  The guest
	@# grades itself: BAD means 59 refused, or answered a paragraph whose
	@# base page does not describe the program, or put something other
	@# than PIP at the code paragraph it named.
	@grep -q 'I86PL BAD' $(I86LOG) \
		&& { echo "verify-i86: FAIL -- function 59 did not load PIP.CMD."; \
		     echo "            The guest read the base page it was answered"; \
		     echo "            with and PIP's prologue was not where it said."; \
		     exit 1; } \
		|| true
	@grep -q 'I86PL OK' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the function 59 guest did not reach"; \
		     echo "            its own verdict.  See build/verify-i86.log."; exit 1; }
	@# --- the fifth program: DRI's SUBMIT.CMD ---
	@grep -q 'i86: 2689 instructions, 9 BDOS calls' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- the target run of SUBMIT did not"; \
		     echo "            take the same path as the host run (2,689"; \
		     echo "            instructions, 9 BDOS calls -- src/shim/tests/i86test.c"; \
		     echo "            section 8c)"; exit 1; }
	@grep -q "Error On Line" $(I86LOG) \
		&& { echo "verify-i86: FAIL -- SUBMIT reported an error.  Every"; \
		     echo "            message it can print is one, and the one"; \
		     echo "            an empty default FCB produces is \"No 'SUB'"; \
		     echo "            File Present\" (i86load.c i86mkfcb)."; exit 1; } \
		|| true
	@# SUBMIT ENDS THE WAY THE PROGRAM MEANT TO.  Its PL/M-86 epilogue
	@# closes with `CS: JMP FAR [0059]' to <entry SS>:0000 -- CP/M-80's
	@# `JMP 0000' warm boot in 8086 spelling -- and src/shim/i86exec.c
	@# wboot() recognises it and terminates the guest.  The address is
	@# asserted, not just the fact: 1000:0054 is where SUBMIT's epilogue
	@# ends in the code segment the loader placed, and the host run stops
	@# on the same instruction (src/shim/tests/i86test.c section 8c).
	@grep -q 'i86: warm boot: a far transfer to the entry stack segment, cs:ip 1000:0054' $(I86LOG) \
		|| { echo "verify-i86: FAIL -- SUBMIT did not end where the host"; \
		     echo "            run ends: the far indirect JMP in its"; \
		     echo "            PL/M-86 epilogue, recognised as the warm"; \
		     echo "            boot.  See src/shim/tests/i86test.c section 8c."; exit 1; }
	@# --- the known answer: the copy, off the disk the machine wrote it to ---
	dd if=$(I86IMG) of=build/i86-after.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/i86-fs
	python3 tools/mkcpmfs.py --extract build/i86-after.img build/i86-fs
	@test -f build/i86-fs/I86OUT.TXT \
		|| { echo "verify-i86: FAIL -- PIP made no I86OUT.TXT"; exit 1; }
	cmp $(I86IN) build/i86-fs/I86OUT.TXT
	@# --- and SUBMIT's answer, against the HOST run's own copy of it ---
	@# The comparison is against build/i86sub-host.bin, which
	@# src/shim/tests/i86test.c section 8c writes as it runs, and not against a
	@# transcription: $$$.SUB carries bytes of SUBMIT's own uninitialised
	@# buffer after each command, so a byte-for-byte match is also a
	@# statement that the two environments zeroed the guest's data group
	@# identically.  It is the strongest form the host/target equality
	@# assertion takes anywhere in this lane.
	@test -f 'build/i86-fs/$$$$$$.SUB' \
		|| { echo "verify-i86: FAIL -- SUBMIT.CMD wrote no \$$\$$\$$.SUB"; exit 1; }
	cmp build/i86sub-host.bin 'build/i86-fs/$$$$$$.SUB'
	@# --- and GENCMD's answer, likewise against the host run's copy ---
	@# GENCMD's output is a .CMD, this lane's own input format, and that
	@# is why it was passed over for the gate: a loader grading a header
	@# it would have written itself proves nothing.  It is graded twice
	@# here instead, by neither of those means.  On the host,
	@# src/shim/tests/i86test.c section 8d LOADS AND RUNS the .CMD and reads
	@# "I86HEX OK" off the console.  On the machine, the file is compared
	@# byte for byte against the one the host run produced -- so the
	@# question is not "is this a good .CMD" but "did DRI's converter
	@# take the same path through our interpreter twice", which is the
	@# same question every other assertion in this target asks.
	@test -f build/i86-fs/I86HEX.CMD \
		|| { echo "verify-i86: FAIL -- GENCMD.CMD wrote no I86HEX.CMD"; exit 1; }
	cmp build/i86hex-host.cmd build/i86-fs/I86HEX.CMD
	@echo "verify-i86: PASS -- a TPA program was given two 64 KB segments and ran"
	@echo "            DRI's PIP.CMD in them through the real BDOS; the copy it"
	@echo "            made is byte-identical to its input.  DRI's SUBMIT.CMD"
	@echo "            ran in the same segments, took the same 2,689 instructions"
	@echo "            it takes on the host, ENDED through the warm boot its"
	@echo "            own PL/M-86 epilogue asks for, and the \$$\$$\$$.SUB it wrote is"
	@echo "            byte-identical to the one the host run wrote.  DRI's"
	@echo "            GENCMD.CMD converted a real .H86 in the same segments"
	@echo "            -- which it cannot do at all unless galloc() grants a"
	@echo "            group its G-Max -- and the .CMD it wrote is"
	@echo "            byte-identical to the host run's, which loads and runs"

# DRI's DDT86 under the shim: load PIP.CMD (BDOS fn 59), break at 000D
# (INT 3), show registers and memory, single-step (TF, INT 1), then ^C.
# The native BDOS's line editor warm boots on the ^C, so the CCP's prompt
# returns and ENDIN ends the run.  '-' is no prompt character, so the
# session runs gate-off (\\g), paced on the guest's RR0 poll streak, as
# verify-ed does.
I86DDTDISK = build/i86ddt
I86DDTCPMA = build/i86ddt-cpma.img
I86DDTIMG = build/i86ddt.bin
I86DDTLOG = build/verify-i86ddt.log
I86DDTMAX ?= 300000000
I86DDTFMT = $(OSSEL)$(SESS1)CPM86 DDT86.CMD\r\\gEPIP.CMD\rG,D\rX\rT\rD2000:0,F\r\003$(ENDIN)

.PHONY: verify-i86ddt
verify-i86ddt: all
	@rm -rf $(I86DDTDISK)
	@mkdir -p $(I86DDTDISK)
	@cp $(DISKA)/* $(I86DDTDISK)/
	cp $(I86CORPUS)/DDT86.CMD $(I86CORPUS)/PIP.CMD $(I86DDTDISK)/
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(I86DDTCPMA) $(CPMA_BLOCKS) $(I86DDTDISK)
	$(MKDISK) $(I86DDTIMG) $(CPMSYS) $(I86DDTCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(I86DDTIMG)) \
		--input="$$(printf '$(I86DDTFMT)')" \
		--max=$(I86DDTMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(I86DDTLOG))
	@$(EMUOK)
	@grep -q 'CS 2000:0000 2000:FFFF' $(I86DDTLOG) \
		&& grep -q 'DS 3000:0000 3000:87FF' $(I86DDTLOG) \
		|| { echo "verify-i86ddt: FAIL -- function 59 did not load PIP.CMD"; exit 1; }
	@grep -q '^\*2000:000D' $(I86DDTLOG) \
		|| { echo "verify-i86ddt: FAIL -- G,D did not stop at the breakpoint"; exit 1; }
	@# PIP's prologue: CX = SS = DS, SP = 0184.
	@grep -q -- '--------- F002 0000 3000 0000 0184 0000 0000 0000 2000 3000 3000 3000 000D' $(I86DDTLOG) \
		|| { echo "verify-i86ddt: FAIL -- X does not show PIP's prologue registers"; exit 1; }
	@grep -q '^\*2000:0112' $(I86DDTLOG) \
		|| { echo "verify-i86ddt: FAIL -- T did not step the JMP to 0112"; exit 1; }
	@grep -q '^2000:0000 9C 58 FA 8C D9 8E D1 8D 26 84 01 50 9D E9 02 01' $(I86DDTLOG) \
		|| { echo "verify-i86ddt: FAIL -- D does not show PIP's code group"; exit 1; }
	@grep -q '$(ENDMARK)' $(I86DDTLOG) \
		|| { echo "verify-i86ddt: FAIL -- ^C did not return to the CCP"; exit 1; }
	@echo "verify-i86ddt: PASS -- DDT86 loaded PIP.CMD, broke, stepped, dumped and exited"

# ---- CP/M-86 shim: ASM86, then GENCMD on its output, then the result ----
# The same chain src/shim/tests/i86test.c section 8h runs on the host, here
# through the real BDOS.  The .H86 and the .CMD are compared byte for byte
# with the host run's copies.  Not part of verify-i86's session: GENCMD
# alone is most of that budget.
#
# ASM86 polls console status while it works and takes any key waiting as
# a request to stop, and the emulator hands the next scripted byte to a
# guest that has gone quiet.  So ASM86 gets a boot of its own with nothing
# typed after it, and the rest runs in a second boot of the same disk.
# The two boots measure 0.26 and 1.46 billion instructions.
I86ADISK = build/i86adisk
I86ACPMA = build/i86a-cpma.img
I86AIMG	= build/i86asm.bin
I86ALOG	= build/verify-i86asm.log
I86AMAX	?= 1700000000
I86AIN1	= $(OSSEL)$(SESS1)CPM86 ASM86.CMD I86T\r
I86AIN2	= $(OSSEL)$(SESS1)CPM86 GENCMD.CMD I86T\rCPM86 I86T.CMD\r$(ENDIN)
build/i86asm-host.cmd build/i86asm-host.h86: build/i86sub-host.bin
.PHONY: verify-i86asm
verify-i86asm: all build/i86asm-host.cmd build/i86asm-host.h86
	@rm -rf $(I86ADISK)
	@mkdir -p $(I86ADISK)
	@cp $(DISKA)/* $(I86ADISK)/
	cp $(I86CORPUS)/ASM86.CMD $(I86CORPUS)/GENCMD.CMD $(I86FIX)/I86T.A86 $(I86ADISK)/
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(I86ACPMA) $(CPMA_BLOCKS) $(I86ADISK)
	$(MKDISK) $(I86AIMG) $(CPMSYS) $(I86ACPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(I86AIMG)) \
		--input="$(I86AIN1)" \
		--max=$(I86AMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(I86ALOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(I86AIMG)) \
		--input="$(I86AIN2)" \
		--max=$(I86AMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee -a $(abspath $(I86ALOG))
	@$(EMUOK)
	@grep -q 'END OF ASSEMBLY.  NUMBER OF ERRORS:   0.' $(I86ALOG) \
		|| { echo "verify-i86asm: FAIL -- ASM86 did not assemble I86T.A86 cleanly"; exit 1; }
	@grep -q 'RECORDS WRITTEN' $(I86ALOG) \
		|| { echo "verify-i86asm: FAIL -- GENCMD did not convert I86T.H86"; exit 1; }
	@grep -q '^HELLO FROM ASM86' $(I86ALOG) \
		|| { echo "verify-i86asm: FAIL -- the assembled program did not print its string"; exit 1; }
	dd if=$(I86AIMG) of=build/i86a-after.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/i86a-fs
	python3 tools/mkcpmfs.py --extract build/i86a-after.img build/i86a-fs
	cmp build/i86asm-host.h86 build/i86a-fs/I86T.H86
	cmp build/i86asm-host.cmd build/i86a-fs/I86T.CMD
	@echo "verify-i86asm: PASS -- DRI's ASM86 assembled a source file through the"
	@echo "            real BDOS, GENCMD converted its hex, both files match the"
	@echo "            host run's byte for byte, and the program ran"

# ---- CP/M-86 shim: STAT, HELP and TOD ----
# STAT sizes two files staged here from their directory entries, HELP
# finds its PIP topic by a random read of HELP.HLP, and TOD reads the
# seeded clock through function 49's block, sets it and reads it back.
# The set goes through our function 104, which keeps no seconds.
# HELP's "Press ENTER" polls with function 6, which the emulator's pacing
# does not see as waiting, so that ENTER is type-ahead aimed at the prompt;
# TOD's "Strike key" prints no prompt either, hence the \g before its key.
# The session measures 0.28 billion instructions.
I86UDISK = build/i86udisk
I86UCPMA = build/i86u-cpma.img
I86UIMG	= build/i86util.bin
I86ULOG	= build/verify-i86util.log
I86UMAX	?= 600000000
I86UIN	= $(OSSEL)$(SESS1)CPM86 STAT.CMD\rCPM86 STAT.CMD I86*.*\rCPM86 HELP.CMD PIP\r\\i\r\rCPM86 TOD.CMD\rCPM86 TOD.CMD 03/01/84 07:08:09\r\\gxCPM86 TOD.CMD\r$(ENDIN)
.PHONY: verify-i86util
verify-i86util: all
	@rm -rf $(I86UDISK)
	@mkdir -p $(I86UDISK)
	@cp $(DISKA)/* $(I86UDISK)/
	cp $(I86CORPUS)/STAT.CMD $(I86CORPUS)/HELP.CMD $(I86CORPUS)/HELP.HLP \
		$(I86CORPUS)/TOD.CMD $(I86UDISK)/
	python3 -c 'import sys; sys.stdout.buffer.write(b"x")' > $(I86UDISK)/I86ONE.TXT
	python3 -c 'import sys; sys.stdout.buffer.write(bytes(5000))' > $(I86UDISK)/I86TWO.DAT
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(I86UCPMA) $(CPMA_BLOCKS) $(I86UDISK)
	$(MKDISK) $(I86UIMG) $(CPMSYS) $(I86UCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(I86UIMG)) --rtc=$(RTCSEED) \
		--input="$(I86UIN)" --input-mark='Press ENTER' \
		--max=$(I86UMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(I86ULOG))
	@$(EMUOK)
	@test "`grep -c 'i86: the guest terminated' $(I86ULOG)`" = 6 \
		|| { echo "verify-i86util: FAIL -- not all six runs ended through the BDOS"; exit 1; }
	@grep -q 'A: RW, Free Space: ' $(I86ULOG) \
		|| { echo "verify-i86util: FAIL -- STAT did not report drive A:"; exit 1; }
	@tr -d '\r' < $(I86ULOG) | grep -q '^    1  *[0-9]*k    1 Dir RW        A:I86ONE  .TXT' \
		|| { echo "verify-i86util: FAIL -- STAT did not size I86ONE.TXT as one record"; exit 1; }
	@tr -d '\r' < $(I86ULOG) | grep -q '^   40  *[0-9]*k    1 Dir RW        A:I86TWO  .DAT' \
		|| { echo "verify-i86util: FAIL -- STAT did not size I86TWO.DAT as 40 records"; exit 1; }
	@grep -q 'PIP filespec{\[Gn\]}=filespec{\[O\]}' $(I86ULOG) \
		|| { echo "verify-i86util: FAIL -- HELP did not show its PIP topic"; exit 1; }
	@grep -q 'hex file drive' $(I86ULOG) \
		&& { echo "verify-i86util: FAIL -- HELP read the wrong record of HELP.HLP"; exit 1; } || true
	@tr -d '\r' < $(I86ULOG) | grep -q '^07/31/26       14:3' \
		|| { echo "verify-i86util: FAIL -- TOD did not read the seeded clock"; exit 1; }
	@grep -q 'Strike key to set time' $(I86ULOG) \
		|| { echo "verify-i86util: FAIL -- TOD refused the new time"; exit 1; }
	@test "`tr -d '\r' < $(I86ULOG) | grep -c '^03/01/84       07:08:0'`" = 2 \
		|| { echo "verify-i86util: FAIL -- the time TOD set did not come back"; \
		     echo "            through functions 104 and 105 and the clock"; exit 1; }
	@echo "verify-i86util: PASS -- DRI's STAT sized two files, HELP found its"
	@echo "            topic by a random read, and TOD read the clock, set it"
	@echo "            and read the new time back, all through the real BDOS"

# ---- CP/M-86 shim, ON THE MACHINE: console polling ----
# I86POLL.CMD (tools/mkcmdfix.py p_poll) reads a key through function 50's
# CONIN, then polls function 6 with E = 0FFh around its output.  A poll that
# waits never reaches I86POLL DONE.
I86PDISK = build/i86polldisk
I86PCPMA = build/i86poll-cpma.img
I86PIMG	 = build/i86polltest.bin
I86PLOG	 = build/verify-i86poll.log
.PHONY: verify-i86poll
verify-i86poll: all $(I86FIX)/MANIFEST
	@rm -rf $(I86PDISK)
	@mkdir -p $(I86PDISK)
	@cp $(DISKA)/* $(I86PDISK)/
	cp $(I86FIX)/I86POLL.CMD $(I86PDISK)/
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(I86PCPMA) $(CPMA_BLOCKS) $(I86PDISK)
	$(MKDISK) $(I86PIMG) $(CPMSYS) $(I86PCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(I86PIMG)) \
		--input="$(OSSEL)$(SESS1)CPM86 I86POLL.CMD\r\gk" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(I86PLOG))
	@$(EMUOK)
	@grep -q 'k\.' $(I86PLOG) \
		|| { echo "verify-i86poll: FAIL -- function 50 CONIN did not return the key"; exit 1; }
	@grep -q 'I86POLL DONE' $(I86PLOG) \
		|| { echo "verify-i86poll: FAIL -- a function 6 poll waited or saw a key"; exit 1; }
	@grep -q 'i86: the guest terminated' $(I86PLOG) \
		|| { echo "verify-i86poll: FAIL -- the guest did not terminate cleanly"; exit 1; }
	@echo "verify-i86poll: PASS -- function 50 CONIN read a key and 64"
	@echo "            function 6 polls answered at once"

# ---- CP/M-86 shim, ON THE MACHINE: keys typed while the guest prints ----
# I86KEYQ.CMD (tools/mkcmdfix.py p_keyq) announces itself, writes 200
# characters with function 2, and then reads four keys through function 50's
# CONIN, echoing each.  The four are typed AT THE OUTPUT: the mark releases
# the first the moment the announcement appears, and each of the rest as the
# receiver frees.
#
# The BDOS polls the console every eight output characters and KEEPS what
# it finds -- ^S and ^Q are swallowed, anything else is held in one byte
# per console -- so with the poll left on the four never reach the guest
# and its first CONIN waits for a key nobody will type again.  The shim
# turns the poll off for the length of a run.
I86KDISK = build/i86keyqdisk
I86KCPMA = build/i86keyq-cpma.img
I86KIMG	 = build/i86keyqtest.bin
I86KLOG	 = build/verify-i86keyq.log
.PHONY: verify-i86keyq
verify-i86keyq: all $(I86FIX)/MANIFEST
	@rm -rf $(I86KDISK)
	@mkdir -p $(I86KDISK)
	@cp $(DISKA)/* $(I86KDISK)/
	cp $(I86FIX)/I86KEYQ.CMD $(I86KDISK)/
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(I86KCPMA) $(CPMA_BLOCKS) $(I86KDISK)
	$(MKDISK) $(I86KIMG) $(CPMSYS) $(I86KCPMA) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(I86KIMG)) \
		--input="$(OSSEL)$(SESS1)CPM86 I86KEYQ.CMD\r\ia\ib\ic\id" \
		--input-mark='I86KEYQ GO' \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(I86KLOG))
	@$(EMUOK)
	@tr -d '\r' < $(I86KLOG) | grep -q '\.\{40\}abcd' \
		|| { echo "verify-i86keyq: FAIL -- the four keys typed during the guest's"; \
		     echo "            output did not all reach function 50 CONIN, in"; \
		     echo "            order.  The console poll ate them."; exit 1; }
	@grep -q 'I86KEYQ DONE' $(I86KLOG) \
		|| { echo "verify-i86keyq: FAIL -- the guest never finished reading"; exit 1; }
	@grep -q 'i86: the guest terminated' $(I86KLOG) \
		|| { echo "verify-i86keyq: FAIL -- the guest did not terminate cleanly"; exit 1; }
	@echo "verify-i86keyq: PASS -- four keys typed at 200 characters of"
	@echo "            guest output all reached function 50 CONIN, in order"

# CP/M 3 directory format (BDOS, mkcpmfs.py) must stay byte-compatible with
# cpmtools, a third-party reader/writer of it, driven by tests/cpmtools/diskdefs.
# Empty (not on $PATH) leaves each recipe to refuse by name.
CPMTOOLS := $(if $(CPMTOOLS),$(CPMTOOLS),$(shell sh tools/deps.sh cpmtools))
CPMTOOLSCHK = sh tools/deps.sh -n cpmtools '$(CPMTOOLS)' || exit 1
# cpmtools reads ./diskdefs, so it runs from the directory holding ours.
CPMT = cd tests/cpmtools && $(abspath $(CPMTOOLS))
# Naming a driver skips libdsk's geometry probe, which reads sector 0 -- our
# first directory entry -- as an Amstrad PCW superblock and takes the fifth
# filename byte for a sector-size shift, overrunning its 512-byte buffer.
# Every geometry value still comes from our diskdefs; the format name only
# satisfies the lookup.  Fixed in libdsk 1.5.18; Ubuntu ships 1.5.9.
CPMTDEV = -T raw,pcw180
.PHONY: dirfmt-check
dirfmt-check:
	@$(CPMTOOLSCHK)
	python3 tests/dirfmt-test.py build/dirfmt $(CPMTOOLS)

# --extract against a directory that lies.  The eleven name bytes of a CP/M
# directory entry are untrusted input and --extract turns them into a host
# path, so this target builds entries a well-behaved CP/M would never write --
# a name of `../OUT.TXT', a sparse allocation list, a file over 16 KiB whose
# first entry carries raw extent 1 -- and judges the host DISK, not the tool's
# exit status: the traversal case checks that the file it aimed at outside the
# destination still holds what it held.  Pure host work, no emulator.
.PHONY: verify-extract
verify-extract:
	python3 tests/extract-test.py build/extract
	@echo "verify-extract: PASS -- a decoded CP/M name is a leaf, holes stay holes, a big file keeps its stamp"

# The proof that matters for sequencing: a STAMPED drive A: booted by the
# CURRENT, unmodified CP/M.  The BDOS does not maintain stamps yet -- what
# this shows is that it does not break them and does not misreport space.
# DIR/TYPE/STAT/PIP run, PIP and the CCP create files, then the partition is
# pulled back out and the extensions must still be there, intact.
STAMPIMG = build/stamptest.bin
STAMPCPMA = build/cpma-stamped.img
STAMPVERIFYIN = $(OSSEL)DIR\rSTAT\rSTAT *.*\rTYPE HELLO.C\rPIP STAMP1.TXT=HELLO.C\rTYPE STAMP1.TXT\rDIR *.TXT\rSTAT\r$(ENDIN)
.PHONY: verify-stamped
verify-stamped: all
	@$(CPMTOOLSCHK)
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
	@# A signal says fsck.cpm died, not that it reached a verdict; glibc's
	@# heap check aborts it after a clean read on some builds.  Naming the
	@# two apart keeps a crash in its own tool from reading as our damage.
	@{ $(CPMT)/fsck.cpm -n -f c900a $(CPMTDEV) $(abspath build/stamp-cpma.img) \
		> $(abspath build/stamp-after-cpm.txt); } ; s=$$?; \
	  if [ $$s -ge 128 ]; then \
		cat $(abspath build/stamp-after-cpm.txt); \
		echo "verify-stamped: FAIL -- fsck.cpm died on signal $$((s - 128)) without reaching a verdict"; exit 1; \
	  elif [ $$s -ne 0 ]; then \
		cat $(abspath build/stamp-after-cpm.txt); \
		echo "verify-stamped: FAIL -- fsck.cpm finds the stamped directory damaged"; exit 1; \
	  fi
	@$(CPMT)/cpmls -f c900a $(CPMTDEV) $(abspath build/stamp-cpma.img) \
		> $(abspath build/stamp-after-cpm.txt)
	@grep -qx 'stamp1.txt' build/stamp-after-cpm.txt \
		|| { echo "verify-stamped: FAIL -- cpmtools does not see PIP's file"; exit 1; }
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
	@# directory.  B: is not carved out of it.
	@grep -q '81,920: 128 Byte Record Capacity' $(DBLOG) \
		|| { echo "verify-driveb: FAIL -- A: is no longer 10 MB"; exit 1; }
	@grep -q '65,536: 128 Byte Record Capacity' $(DBLOG) \
		|| { echo "verify-driveb: FAIL -- B: is not the 8 MB drive its own dpb declares"; exit 1; }
	@# BOTH drives report no reserved tracks, and that is the K1 change,
	@# not a regression: a drive's position on the device used to live in
	@# dpb.trk_off (B: reported 1,312 here) and now lives in the BIOS's
	@# per-drive base, selected by SELDSK, so a slot need not start on an
	@# 8 KB track boundary of one fixed origin.  What says the BDOS got
	@# B:'s OWN dpb and not A:'s is the capacity line above -- 65,536
	@# records against A:'s 81,920 -- which is the check that was always
	@# doing that work.  Where the bytes really are is driveb-check.py,
	@# at the end of this target, and that is the aliasing test proper.
	@test "`grep -c '        0: Reserved  Tracks' $(DBLOG)`" = 2 \
		|| { echo "verify-driveb: FAIL -- a drive does not start at track 0 of itself"; exit 1; }
	@# --- cross-visibility, from the running system ---
	@grep -q 'B: BONLY    TXT' $(DBLOG) \
		|| { echo "verify-driveb: FAIL -- the packed B: image is not readable"; exit 1; }
	@# The claim is that B: holds the two files this session wrote as well
	@# as the two the packer put there -- NOT that they land in one
	@# particular four-column line.  It used to grep for the literal
	@# `B: BONLY TXT : READMEB TXT : FROMA TXT : BNEW TXT', which made
	@# every addition to src/dist/disk-b a false failure: one more file
	@# shifts the grouping by a slot and the grep breaks while B: is
	@# perfectly correct.  So: the layout, then each name.  (Same fix as
	@# verify-util's SDIR [SHORT] check.)
	@# (The CCP puts a CR at the START of each listing line, so the log is
	@# stripped of them before the line is anchored.)
	@tr -d '\r' < $(DBLOG) \
		| grep -Eq '^B: ([A-Z0-9$$]+ +[A-Z0-9]+ : ){3}[A-Z0-9$$]+ +[A-Z0-9]+ *$$' \
		|| { echo "verify-driveb: FAIL -- DIR B: is not the four-per-line listing"; exit 1; }
	@for f in BONLY READMEB FROMA BNEW; do \
		tr -d '\r' < $(DBLOG) | grep -Eq "(^B: |: )$$f +TXT( :| *$$)" \
		|| { echo "verify-driveb: FAIL -- B: does not hold $$f.TXT"; exit 1; }; \
	done
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

# ---- the drive table comes from the MEDIUM (verify-bipart) ----
# verify-driveb proves B: is its own region of the disk.  It cannot prove
# that the BIOS learned WHERE that region is from the medium, because the
# medium puts B: exactly where the old compiled constant did -- both
# answers agree, so the test passes either way.
#
# So this target moves it.  The same medium is built with `--cpmb-base',
# which writes a different `part 9' into kboot.cfg AND blits drive B:'s
# bytes there, and then runs the session verify-driveb's own
# driveb-check.py judges -- against the NEW base.  Nothing in cpm.sys is
# rebuilt or patched: the only difference between this medium and
# verify-driveb's is four digits in a config file on the boot partition.
# If the BIOS were still using BTRKOFF, B: would be 8 MB of zeros at the
# old address and the first `PIP B:' would fail; if it read the table but
# ignored the base, the files would land in the old region and
# driveb-check.py's "appears NOWHERE else on the disk" half would fail.
#
# 67392 is the last 16384 blocks of the 83776-block device, so B: ends
# exactly at the end of the disk -- as far from 59136 as this geometry
# allows, and clear of boot, cpma and cpmboot (mkcpmdisk.py checks).
BPIMG	= build/biptest.bin
BPLOG	= build/verify-bipart.log
BPBASE	= 67392
# The names driveb-check.py looks for: AONLY.TXT written on A:, BNEW.TXT
# written on B: (from A:PIP, so the CCP finds the command on A: while B:
# is the drive it acts on), BONLY.TXT put there by the packer.
BPVERIFYIN = $(OSSEL)SHOW B:[DRIVE]\rPIP AONLY.TXT=HELLO.C\rB:\rA:PIP BNEW.TXT=BONLY.TXT\rDIR\rA:\r$(ENDIN)
.PHONY: verify-bipart
verify-bipart: all
	$(MKDISK) --cpmb-base=$(BPBASE) $(BPIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(BPIMG)) \
		--input="$(BPVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(BPLOG))
	@$(EMUOK)
	@# B: is the drive it always was -- the same 8 MB dpb, at a new base.
	@grep -q '65,536: 128 Byte Record Capacity' $(BPLOG) \
		|| { echo "verify-bipart: FAIL -- B: is not the 8 MB drive; the moved slot did not become a drive"; exit 1; }
	@grep -q 'B: BONLY    TXT' $(BPLOG) \
		|| { echo "verify-bipart: FAIL -- the packed B: image does not read at block $(BPBASE)"; exit 1; }
	@# ...and the bytes are there, and NOWHERE else on the disk.
	python3 tests/driveb-check.py $(BPIMG) $(CPMA_BASEBLK) $(CPMA_BLOCKS) \
		$(BPBASE) $(CPMB_BLOCKS) $(CPMBIMG)
	@# The old address must be untouched: a BIOS still using BTRKOFF would
	@# have put B:'s directory at block $(CPMB_BASEBLK), and this is the
	@# check that names which table the running system used.  Only as far
	@# as the new drive's base, because the two regions overlap from there
	@# on -- B: moved 8256 blocks, not clear of itself -- and the first 32
	@# blocks of the old address, where a directory would be, are well
	@# inside that.
	@dd if=$(BPIMG) bs=512 skip=$(CPMB_BASEBLK) \
		count=`expr $(BPBASE) - $(CPMB_BASEBLK)` \
		status=none | tr -d '\0' | wc -c | grep -qx 0 \
		|| { echo "verify-bipart: FAIL -- something was written at the OLD drive-B: base $(CPMB_BASEBLK): the BIOS is still using its compiled table"; exit 1; }
	@echo "verify-bipart: PASS -- B: moved to block $(BPBASE) by kboot.cfg alone"

# ---- and the medium that hands over NOTHING (verify-bifallback) ----
# The drive table comes from the loader when there is one.  When there is
# not -- an older kboot, a kboot that does not fill bi_part[], an `os'
# line that asks for no handoff -- cpm.sys must come up on the A: and B:
# it was built with, because that is every medium made before this
# change.  `mkcpmdisk.py --no-bootinfo' writes exactly the kboot.cfg this
# builder wrote then, so the medium under test is the old medium and not
# a simulation of one.
#
# Two runs on two media, and the second is what makes the first mean
# something:
#
#   1. B: at its usual base, nothing handed over.  The system must be
#      indistinguishable from today: B: is the 8 MB drive, its packed
#      files read, and a file written on it lands in its region.
#   2. B: MOVED, and still nothing handed over.  The system must NOT
#      follow it -- it has not been told, so it must read the compiled
#      base and find the empty region there.  Without this half, run 1
#      passes for a BIOS that ignores the handoff entirely, and so does
#      verify-bipart's medium for a BIOS that got lucky.
BFIMG	= build/biftest.bin
BFLOG	= build/verify-bifallback.log
BFMOVIMG = build/bifmove.bin
BFMOVLOG = build/verify-bifallback-moved.log
BFVERIFYIN = $(OSSEL)SHOW B:[DRIVE]\rPIP B:FROMA.TXT=HELLO.C\rDIR B:\r$(ENDIN)
.PHONY: verify-bifallback
verify-bifallback: all
	$(MKDISK) --no-bootinfo $(BFIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(BFIMG)) \
		--input="$(BFVERIFYIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(BFLOG))
	@$(EMUOK)
	@grep -q '65,536: 128 Byte Record Capacity' $(BFLOG) \
		|| { echo "verify-bifallback: FAIL -- with no handoff, B: is not the 8 MB drive cpm.sys was built with"; exit 1; }
	@grep -q 'B: BONLY    TXT' $(BFLOG) \
		|| { echo "verify-bifallback: FAIL -- with no handoff, the packed B: image does not read: an existing medium would not come up"; exit 1; }
	@tr -d '\r' < $(BFLOG) | grep -Eq "(^B: |: )FROMA +TXT( :| *$$)" \
		|| { echo "verify-bifallback: FAIL -- with no handoff, a write to B: did not take"; exit 1; }
	@# ...and it landed in B:'s COMPILED region, which is the statement
	@# about addresses that the transcript cannot make.
	@dd if=$(BFIMG) bs=512 skip=$(CPMB_BASEBLK) count=$(CPMB_BLOCKS) \
		status=none | grep -qa 'FROMA   TXT' \
		|| { echo "verify-bifallback: FAIL -- the file written on B: is not at the compiled base $(CPMB_BASEBLK)"; exit 1; }
	@# ---- the half that says it is the HANDOFF doing the work ----
	$(MKDISK) --no-bootinfo --cpmb-base=$(BPBASE) $(BFMOVIMG) $(CPMSYS) \
		$(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(BFMOVIMG)) \
		--input="$(OSSEL)DIR B:\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(BFMOVLOG))
	@$(EMUOK)
	@grep -q 'BONLY' $(BFMOVLOG) \
		&& { echo "verify-bifallback: FAIL -- B: followed the medium with NO handoff to tell it: verify-bipart proves nothing"; exit 1; }; \
		true
	@echo "verify-bifallback: PASS -- with nothing handed over, A:/B: are the"
	@echo "                   compiled ones, and a moved B: is not followed"

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
IDIN1FMT = $(OSSEL)SHOW B:[DRIVE]\rDIR B:\rTYPE B:ID3.TXT\rINITDIR B:\r\\gY\rDIR B:\rTYPE B:ID3.TXT\rTYPE B:ID5.TXT\rDIR B:ID4.TXT\r$(ENDIN)
# Boot 2: the two refusals.  With no drive on the command line INITDIR
# asks (INITDIR.PLI:288-321) instead of guessing -- answering that
# prompt needs the gate off too.  B: is now formatted, so the second run
# must stop at "Directory already re-formatted."; A: is then answered N,
# which must leave it alone.
IDIN2FMT = $(OSSEL)INITDIR\r\\gB\rY\rINITDIR A:\rN\r$(ENDIN)
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
	@# Track offset 0, and it is B:'s own dpb saying so: since K1 the
	@# drive's position on the device is the BIOS's per-drive base, not
	@# dpb.trk_off, so INITDIR's raw SETTRK numbers are drive-relative
	@# and its writes land on B: because SELDSK put the base there.  The
	@# proof that they did is the byte-for-byte oracle comparison above,
	@# taken from B:'s slice of the medium.
	@grep -q 'track offset: 0' $(IDLOG) \
		|| { echo "verify-initdir: FAIL -- B:'s dpb no longer reports a drive-relative track offset"; exit 1; }
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
# the pristine copies save took, and the restore is then CHECKED against
# those same bytes -- the ones that were actually there when the run
# started, local edits and all.
#
# The suite runs this in a tree of its own, a plain copy of the working
# tree made by tests/verifyrun.sh: the mutations are to src/ and the
# rebuild is of build/, so neither can be shared with anybody.
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
		if $(MAKE) --no-print-directory verify-initdir >build/idmut-$$m.log 2>&1; then \
			echo "verify-initdir-mutants: FAIL -- $$m PASSED verify-initdir;"; \
			echo "  whatever that mutant broke, nothing is asserting on it"; \
			rc=1; \
		else \
			echo "verify-initdir-mutants: $$m correctly failed verify-initdir"; \
		fi; \
		sh tests/initdir-mutate.sh restore >/dev/null || { rc=1; break; }; \
	done; \
	sh tests/initdir-mutate.sh restore >/dev/null 2>&1 || true; \
	sh tests/initdir-mutate.sh check >/dev/null \
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

# ---- src/app: the native applications ----
# `all' builds the five src/app programs on the host (mk/programs.mk,
# over src/lib/cpmsys.c) and stages them on drive A: beside their source.
# These two targets run THOSE binaries on the machine and check the
# answers they give.  Rebuilding the same sources ON THE MACHINE with
# DRI's ZCC.Z8K and LD8K.Z8K -- the question `selfhost' asks of a
# nine-line HELLO.C, asked of real programs -- is verify-zcc below, which
# is opt-in because it is the slow half: see its header.
#
# $(call APPRUN,IMAGE,INPUT,LOG,DIR) -- one cold boot of INPUT on IMAGE,
# then its drive A: extracted into DIR.
define APPRUN
	{ $(EMUCD) && ./c900 --disk=$(abspath $(1)) \
		--input="$(2)" --max=$(APPMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } \
		| python3 $(abspath tests/tstamp.py) \
		| tee $(abspath $(3))
	@$(EMUOK)
	rm -rf $(4); mkdir -p $(4)
	dd if=$(1) of=$(4)/cpma.img bs=512 skip=$(CPMA_START) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(4)/cpma.img $(4)
endef
# TOHEX writes 113K of hex through `>', which is emulated minutes.
APPMAX	= 2000000000

# Robert Heller's four utilities: tests/a3chk.py says what each answer
# must be and works it out independently.
A3IMG	= build/a3test.bin
A3DIR	= build/a3
A3LOG	= build/verify-a3
A3CMDS	= TOHEX SDB.Z8K >A3.HEX\rFROMHEX A3.HEX A3.BIN\rSORTFL <README.TXT >A3S.TXT\rKILLDU <A3S.TXT >A3K.TXT\r
A3RUNIN	= $(OSSEL)$(A3CMDS)$(ENDIN)
# The host-built run goes on to what only src/lib/cpmsys.c promises:
# wildcard arguments, `>>' and exact binary lengths (tests/a3chk.py --cpmsys).
A3SYSIN	= $(OSSEL)$(A3CMDS)CPMSYST ARGS A3?.TXT A3.* NOSUCH?.* PLAIN A:SDB.Z?K >B3W.TXT\rSORTFL <README.TXT >B3A.TXT\rKILLDU <A3S.TXT >>B3A.TXT\rKILLDU <A3S.TXT >>B3N.TXT\rCPMSYST ODD 1001 B3O.BIN\rTOHEX B3O.BIN >B3O.HEX\rFROMHEX B3O.HEX B3P.BIN\r$(ENDIN)
.PHONY: verify-a3
verify-a3: all
	$(MKDISK) $(A3IMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	$(call APPRUN,$(A3IMG),$(A3SYSIN),$(A3LOG).1.log,$(A3DIR).1)
	@python3 tests/a3chk.py --cpmsys $(A3LOG).1.log $(A3DIR).1 \
		|| { echo "verify-a3: FAIL -- the host-built programs"; exit 1; }
	@echo "verify-a3: PASS -- Robert Heller's four utilities, host-built,"
	@echo "           converted a 48K binary to hex and back, sorted a file"
	@echo "           and dropped its duplicate lines"

# ---- Gate A: SDB ----
# SDB is a 5,250-line relational DBMS with no terminal dependency at all:
# what it exercises is the BDOS file layer -- creatb, lseek, random-record
# read and write -- which is why the applications plan put it first.
#
# One cold boot runs the host-built SDB -- read the help file off the
# disk, create a relation, import three tuples from a text file, print it
# whole and then through a WHERE clause, export it back out.  The export
# lands in a file, and the file is pulled off the partition and checked
# host-side, so the answer is not just something that scrolled past on a
# transcript.  The same session run against the SDB that ZCC compiles on
# the machine is verify-zcc's second leg.
SDBIMG	= build/sdbtest.bin
SDBDIR	= build/sdb
SDBLOG	= build/verify-sdb
SDBSRC	= CMD COM CRE ERR IEX INT IO JUNK MTH SCN SDB SEL SRT TBL
# The session.  `create' names the attributes and their widths and the
# relation's tuple capacity; `import' reads SDBIN.TXT one attribute value
# per line; the `where' clause compares a num attribute, which in SDB is a
# digit STRING compared by MTH.C's own arithmetic, so it is a real test of
# the program and not only of the library under it.  The \" are for the shell: SDB wants
# real quotes around a file name, or it scans it as an identifier and
# appends .dat.
# SORT.DAT is copied onto the disk BEFORE SDB runs and is nothing to do
# with the database: it is an ordinary user file that happens to carry the
# name SRT.C's debugging trace used, and the `sort' statement at the end
# of the session is what used to destroy it.  SRT.C opened that name with
# mode "w" on every sort even though its `dns' flag is 0 in every shipped
# build, so the file was truncated by a command that had no business
# touching it.  The sort is last so that the export above it -- and the
# checks on what it produced -- are unaffected; what the sort produced is
# exported separately into SDBSRT.TXT and read back off the partition.
SDBRUNIN = $(OSSEL)PIP SORT.DAT=SDBIN.TXT\rSDB\rhelp\rcreate emp ( name char 10 dept char 6 sal num 6 ) 20\rimport \"SDBIN.TXT\" into emp\rprint * from emp ;\rprint * from emp where emp.sal > \"1500\" ;\rexport emp into \"SDBOUT.TXT\" ;\rsort emp by sal ;\rexport emp into \"SDBSRT.TXT\" ;\rexit\r$(ENDIN)
# SDB as ZCC and LD8K build it on the machine, from the source on drive A:
# a stock 0xEE03 binary, where `all' builds an 0xEE01 one.  verify-zcc
# runs it and feeds it from a file.  Fourteen compiles and a link, about
# ten minutes: the slowest thing in this file, and the reason verify-zcc
# is not in verify-all.  It is rebuilt when its source or the system's
# objects change; not on cpm.sys itself, which `all' deletes and relinks
# every time.
SDBZCCDIR = $(SDBDIR).zcc
SDBZCC	= $(SDBZCCDIR)/SDB.Z8K
$(SDBZCC): $(SDBSRC:%=src/app/%.C) src/app/SDB.H src/app/SDBIO.H $(OBJ) \
		tests/appbuild.sh tests/appchk.sh | $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	$(MKDISK) $(SDBZCCDIR).bin $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	sh tests/appbuild.sh $(SDBZCCDIR).bin $(SDBLOG).zcc.log SDB.Z8K $(SDBSRC)
	rm -rf $(SDBZCCDIR).d; mkdir -p $(SDBZCCDIR).d
	dd if=$(SDBZCCDIR).bin of=$(SDBZCCDIR).d/cpma.img bs=512 \
		skip=$(CPMA_START) count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(SDBZCCDIR).d/cpma.img $(SDBZCCDIR).d
	@sh tests/appchk.sh $(SDBLOG).zcc.log $(SDBZCCDIR).d SDB.Z8K
	mkdir -p $(SDBZCCDIR); cp $(SDBZCCDIR).d/SDB.Z8K $@

# $(call SDBZCCA,IMG,DIR,TREE) -- pack the drive A: image IMG in DIR from
# the staged TREE, with its SDB.Z8K replaced by $(SDBZCC).
define SDBZCCA
	rm -rf $(2); cp -r $(3) $(2); cp $(SDBZCC) $(2)/SDB.Z8K
	python3 tools/mkcpmfs.py --initdir --label $(LABEL) \
		--label-mode $(LABELMODE) $(1) $(CPMA_BLOCKS) $(2)
endef

.PHONY: verify-sdb
verify-sdb: all
	$(MKDISK) $(SDBIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	$(call APPRUN,$(SDBIMG),$(SDBRUNIN),$(SDBLOG).1.log,$(SDBDIR).1)
	@sh tests/sdbchk.sh $(SDBLOG).1.log $(SDBDIR).1 \
		|| { echo "verify-sdb: FAIL -- the host-built SDB"; exit 1; }
	@echo "verify-sdb: PASS -- SDB, host-built, created a relation,"
	@echo "            imported three tuples, selected two of them and"
	@echo "            exported all three back unchanged.  GATE A."

# ---- verify-zcc: the same sources through DRI's own compiler (opt-in) ----
# Everything above runs the programs `all' builds on the host.  This one
# builds them AGAIN on the emulated machine with DRI's ZCC.Z8K and
# LD8K.Z8K and runs what that produces: does the source still build on
# the target, and does what it builds work?  The two builds are different
# compilers and libraries, so their bytes are not compared.
#
# The on-target build ERASES each .Z8K first, or a failed compile leaves
# the host-built copy in place and the checks would run that instead.
# tests/appbuild.sh does the building, ONE COLD BOOT PER COMMAND, and its
# header says why that is not the extravagance it looks like: a single
# scripted session gets bytes eaten by ZCC's chained passes, and does it
# intermittently, which is the worst way for a verification target to be
# wrong.  The read-back and the assertions stay here.
#
# SDB alone is fourteen compiles and a link.  That is most of twenty
# minutes, which is why this target is NOT in verify-all -- the
# enumeration there skips it by name -- and is run deliberately:
#
#	make verify-zcc
#
# before a release, or whenever src/app or the compiler on drive A: moves.
# Three legs, one from each target it was split out of:
#
#   a3    the four utilities rebuilt on the machine, then run on the same
#         input verify-a3 gives the host-built ones (tests/a3chk.py)
#   sdb   SDB compiled from its own source, then verify-sdb's session
#   rsxn  GET FILE feeding that stock 0xEE03 SDB through functions 1 and
#         10.  verify-rsxn's other three legs need no ZCC build and stay
#         in the suite; this is the one that wants $(SDBZCC).
ZCCA3IMG  = build/zcc-a3.bin
ZCCA3DIR  = build/zcc-a3
ZCCSDBIMG = build/zcc-sdb.bin
ZCCSDBDIR = build/zcc-sdb
ZCCRSXIMG = build/zcc-rsxn.bin
ZCCLOG	= build/verify-zcc
.PHONY: verify-zcc
verify-zcc: all $(CPMAGP) $(SDBZCC)
	$(MKDISK) $(ZCCA3IMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	sh tests/appbuild.sh $(ZCCA3IMG) $(ZCCLOG).a3b1 SORTFL.Z8K SORTFL
	sh tests/appbuild.sh $(ZCCA3IMG) $(ZCCLOG).a3b2 KILLDU.Z8K KILLDU
	sh tests/appbuild.sh $(ZCCA3IMG) $(ZCCLOG).a3b3 TOHEX.Z8K TOHEX
	sh tests/appbuild.sh $(ZCCA3IMG) $(ZCCLOG).a3b4 FROMHEX.Z8K FROMHEX
	cat $(ZCCLOG).a3b1 $(ZCCLOG).a3b2 $(ZCCLOG).a3b3 $(ZCCLOG).a3b4 \
		> $(ZCCLOG).a3.log
	$(call APPRUN,$(ZCCA3IMG),$(A3RUNIN),$(ZCCLOG).a3run.log,$(ZCCA3DIR))
	@sh tests/appchk.sh $(ZCCLOG).a3.log $(ZCCA3DIR) \
		SORTFL.Z8K KILLDU.Z8K TOHEX.Z8K FROMHEX.Z8K \
		|| { echo "verify-zcc: FAIL -- the four utilities did not build"; exit 1; }
	@python3 tests/a3chk.py $(ZCCLOG).a3run.log $(ZCCA3DIR) \
		|| { echo "verify-zcc: FAIL -- the programs ZCC built"; exit 1; }
	$(call SDBZCCA,$(ZCCSDBDIR).cpma.img,$(ZCCSDBDIR).diska,$(DISKA))
	$(MKDISK) $(ZCCSDBIMG) $(CPMSYS) $(ZCCSDBDIR).cpma.img $(CPMBIMG)
	$(call APPRUN,$(ZCCSDBIMG),$(SDBRUNIN),$(ZCCLOG).sdb.log,$(ZCCSDBDIR))
	@sh tests/sdbchk.sh $(ZCCLOG).sdb.log $(ZCCSDBDIR) \
		|| { echo "verify-zcc: FAIL -- the SDB ZCC built"; exit 1; }
	$(call SDBZCCA,build/cpma-zccrsxn.img,build/diska-zccrsxn,$(DISKAG))
	$(MKDISK) $(ZCCRSXIMG) $(CPMSYS) build/cpma-zccrsxn.img
	{ $(EMUCD) && ./c900 --disk=$(abspath $(ZCCRSXIMG)) \
		--input="$(RSXNIN1)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(ZCCLOG)).rsxn.log
	@$(EMUOK)
	@grep -q 'Getting console input from file: NCMDS.TXT' $(ZCCLOG).rsxn.log \
		|| { echo "verify-zcc: FAIL -- GET did not take the file"; exit 1; }
	@grep -q 'SDB - version' $(ZCCLOG).rsxn.log \
		|| { echo "verify-zcc: FAIL -- SDB.Z8K did not run: the command line was not read out of the file"; exit 1; }
	@grep -q 'SDB> zzbogus' $(ZCCLOG).rsxn.log \
		|| { echo "verify-zcc: FAIL -- a line of the file did not reach SDB's own prompt: the gate is still declining a non-segmented caller"; exit 1; }
	@grep -q 'syntax error' $(ZCCLOG).rsxn.log \
		|| { echo "verify-zcc: FAIL -- SDB did not ACT on the line it was given"; exit 1; }
	@grep -q 'GET-DROVE-A-STOCK-BINARY' $(ZCCLOG).rsxn.log \
		|| { echo "verify-zcc: FAIL -- the exit line did not get SDB out and the next command did not run"; exit 1; }
	@echo "verify-zcc: PASS -- ZCC and LD8K rebuilt the four utilities and"
	@echo "            SDB on the machine from the source shipped beside"
	@echo "            them; those binaries converted a 48K binary to hex"
	@echo "            and back, sorted a file, dropped its duplicate"
	@echo "            lines, and ran a whole SDB session -- and the stock"
	@echo "            0xEE03 SDB took its session from a file through"
	@echo "            functions 1 and 10"

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
#          carry often, and DATE C -- the continuous form, which reads the
#          clock over and over -- run for a fixed instruction budget.  The
#          thousands of reads that produces sweep the whole second, so a
#          carry lands inside a thirteen-register read because there is
#          nowhere else for it to land, not because anything was aimed.
#          rtc900.c takes the image twice and retries until the two agree,
#          so every read must still print a valid time AND at least one
#          read must have been retried -- more register reads than the 26
#          a clean pair needs per line printed, and at least one carry
#          inside a /CS transaction (both counted by the emulator and
#          reported on stderr).  See the RTCRACEIPS block below for why
#          this leg no longer moves when resident text changes size.
#  noclock --rtc=none, a board with no module fitted: fn 23 must report
#          "no clock" rather than invent a time, and a set must say so.
RTCIMG	= build/rtctest.bin
RTCSEED	= 2026-07-31T14:32:10
# Instructions per emulated RTC second. DATE C continuously reads the clock
# until RTCRACEMAX, sampling carries without depending on boot alignment.
# The rate must allow rtcget's four retry pairs to converge while producing
# at least one retry (26 excess register reads) in the instruction budget.
# Use rtc-race-sweep to check that margin after changing the driver.
RTCRACEIPS = 20000
# The race leg cannot end on its own: DATE C loops until a key it will never
# be sent, which is the point -- the reads have to keep coming for the sweep
# to cover the second.  So it coasts to this budget by design, as
# verify-conclk does to $(CONCLMAX).  ~3,400 clock reads and ~2,900 carries,
# which is roughly 800 retried reads at $(RTCRACEIPS); a hundredth of that
# would still pass.  $(EMUIDLE) is still passed so that a leg which DID end
# early -- a give-up returns to the CCP -- fails fast instead of coasting.
RTCRACEMAX = 60000000
RTCLOG	= build/verify-rtc.log
RTCRACELOG = build/verify-rtc-race.log
RTCNONELOG = build/verify-rtc-noclock.log
RTCVERIFYIN = $(OSSEL)DATE\rDATE\rDATE 03/01/04 07:08:09\rDATE\rDATE 13/45/99 99:99:99\rDATE Q\rDATE 01/01/78 00:00:00\rDATE 12/31/77 12:34:56\rDATE\rDATE 07/04/05 12:34:56\r$(ENDIN)
RTCRACEIN = $(OSSEL)DATE C\r
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
	@echo "verify-rtc: race leg -- DATE C for $(RTCRACEMAX) instructions;"
	@echo "            thousands of clock lines, so this one is not tee'd:"
	@echo "            the transcript is $(RTCRACELOG)."
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RTCIMG)) --rtc=$(RTCSEED) \
		--rtc-ips=$(RTCRACEIPS) --input="$(RTCRACEIN)" --max=$(RTCRACEMAX) \
		$(EMUIDLE) 2>$(abspath $(RTCRACELOG)).err; $(EMUSTAT); } \
		> $(abspath $(RTCRACELOG))
	@$(EMUOK)
	@tail -2 $(RTCRACELOG)
	@tail -1 $(RTCRACELOG).err
	@grep -q 'No clock' $(RTCRACELOG) \
		&& { echo "verify-rtc: FAIL -- a read across a carry was not recovered"; exit 1; } || true
	@n=`tr -d '\r' < $(RTCRACELOG) | grep -cE '^(Sun|Mon|Tue|Wed|Thu|Fri|Sat) [0-9][0-9]/'`; \
	r=`sed -n 's/.*, \([0-9]*\) reads,.*/\1/p' $(RTCRACELOG).err`; \
	test "$$n" -ge 100 \
		|| { echo "verify-rtc: FAIL -- DATE C printed only $$n times: the continuous read did not loop, so nothing swept the second"; exit 1; }; \
	test "`expr $$r - 26 \* $$n`" -ge 26 \
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

# ---- rtc-race-sweep: the RTCRACEIPS band, measured ----
# This target measures; it does not tune.  RTCRACEIPS stopped being an aim
# when the race leg went to DATE C (see the block above), so there is no
# constant here to re-choose after a size change.  What tests/rtcsweep.sh
# still produces is the EVIDENCE: the two-stage table (a range on the base
# seed, survivors x six seeds) that shows where the give-up floor is, how
# much retry margin each rate has, and that the setting above sits in the
# middle of a wide flat band rather than on a spike.  That table is what the
# comment block above quotes.  Worth re-running when rtc900.c's own read
# loop changes length -- the one thing the floor depends on.
#
# Deliberately named without a `verify-' prefix: verify-all enumerates
# targets by matching `^verify-[a-z0-9-]*:' against this file, and a sweep
# is not a pass/fail test -- it has no verdict to fail the build on, and at
# 14-plus emulator boots for stage 1 alone (more for stage 2) it is far too
# slow to sit in the suite. `make all' does not reach it either: nothing in
# `all's dependency graph names it. Reached only by `make rtc-race-sweep'.
#
# Uses the SAME race medium, input and budget as verify-rtc (rebuilt fresh,
# since a stale $(RTCIMG) from some other target would measure a different
# system), so the band it reports is the band the leg actually runs in.
.PHONY: rtc-race-sweep
rtc-race-sweep: all
	$(MKDISK) $(RTCIMG) $(CPMSYS) $(CPMAIMG)
	@sh tools/deps.sh -n emu '$(EMU)'
	@EMU='$(EMU)' sh tests/rtcsweep.sh "$(abspath $(RTCIMG))" "$(RTCRACEMAX)" "$(EMUIDLE)" "$(RTCRACEIN)" "$(RTCSEED)"

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
$(RTCOBJ)/date.o: src/cmd/date.c src/lib/cpm.h | $(RTCOBJ)
	$(HOSTCC) -std=gnu89 -w -Isrc/lib -Dstatic= -Dmain=date_main -c $< -o $@
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

# ---- Function 46's wire form, and function 31's return value ----
# Two silent-wrong-answer bugs, both fixed by naming the bytes.
#
# Function 46 used to end free_sp() with `cpy_out(&records, dma, sizeof
# records)' on a LONG -- a 4-byte BIG-endian value on this Z8001 -- where
# CP/M 3 specifies THREE LITTLE-ENDIAN BYTES of 128-byte record count and
# a zero fourth byte.  A conforming v3 program read our top three bytes
# reversed and reported free space wrong by orders of magnitude, with no
# failure signal.  V3FREE therefore asserts the four bytes ONE AT A TIME
# against a count derived by hand from the drive B image (2042 free
# blocks * 32 records = 65,344 = 0x00FF40, so 40 FF 00 00) -- the only
# shape of test that separates the two forms.  Drive B is the medium
# because it is packed from src/dist/disk-b/ and nothing writes to it,
# so its free space is a constant of the build; $(CPMBIMG) is mounted
# here for exactly that reason.  V3FREE also checks fn 31, which used to
# deliver the DPB to the caller's buffer and then return 0, so a caller
# testing the answer concluded the call had failed.
#
# V3FREE.Z8K is named in $(ATEST), so it is stripped from the release
# medium by $(CPMARIMG) and reaches no shipped disk.
V3FIMG	= build/v3free.bin
V3FLOG	= build/verify-v3free.log
.PHONY: verify-v3free
verify-v3free: all
	$(MKDISK) $(V3FIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(V3FIMG)) \
		--input="$(OSSEL)V3FREE\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(V3FLOG))
	@$(EMUOK)
	@grep -q 'dma\[0\] (recs bits  0.. 7) -> 40 OK' $(V3FLOG) \
		|| { echo "verify-v3free: FAIL -- fn 46 byte 0 is not 40h"; exit 1; }
	@grep -q 'dma\[1\] (recs bits  8..15) -> FF OK' $(V3FLOG) \
		|| { echo "verify-v3free: FAIL -- fn 46 byte 1 is not FFh"; exit 1; }
	@grep -q 'dma\[2\] (recs bits 16..23) -> 00 OK' $(V3FLOG) \
		|| { echo "verify-v3free: FAIL -- fn 46 byte 2 is not 00h"; exit 1; }
	@grep -q 'dma\[3\] (must be zero)    -> 00 OK' $(V3FLOG) \
		|| { echo "verify-v3free: FAIL -- fn 46 byte 3 is not 00h"; exit 1; }
	@grep -q 'free records on B = 65344' $(V3FLOG) \
		|| { echo "verify-v3free: FAIL -- the count is not 65344"; exit 1; }
	@test "`grep -c ' BAD' $(V3FLOG)`" = 0 \
		|| { echo "verify-v3free: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'V3FREE: PASS' $(V3FLOG) \
		|| { echo "verify-v3free: FAIL -- V3FREE did not finish"; exit 1; }
	@echo "verify-v3free: PASS -- fn 46 writes 40 FF 00 00 (65344 records"
	@echo "               free on B), and fn 31 returns the buffer it filled"

# ---- CP/M 3 V3 wave: the return-value shapes fixed by G7-G12 ----
# (CPM3-V3-DELTA.md section 2).  V3RET does everything in one cold boot:
# it checks fn 32/38/39/41 and the default arm directly, builds and
# reopens its own user-0 fallback file for G12, then chains to itself
# (fn 47, E=0FFh) to check ccp$flgs bit 40h (G8) from the next program,
# since fn 47 never returns to its caller.  Two console lines therefore
# appear -- "V3RET" typed by us, "V3RET PHASE2" supplied by the chain,
# never typed -- and the run ends with one PASS/FAIL line from phase 2
# folding in phase 1's tally (parked across the warm boot in fn 108).
V3IMG	= build/v3rettest.bin
V3LOG	= build/verify-v3ret.log
.PHONY: verify-v3ret
verify-v3ret: all
	$(MKDISK) $(V3IMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(V3IMG)) \
		--input="$(OSSEL)V3RET\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(V3LOG))
	@$(EMUOK)
	@test "`grep -c ' BAD' $(V3LOG)`" = 0 \
		|| { echo "verify-v3ret: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'phase 2 (chained by fn 47)' $(V3LOG) \
		|| { echo "verify-v3ret: FAIL -- fn 47 did not chain to phase 2"; exit 1; }
	@grep -q 'V3RET: PASS' $(V3LOG) \
		|| { echo "verify-v3ret: FAIL -- V3RET did not finish"; exit 1; }
	@echo "verify-v3ret: PASS"

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
	@# Four entries to a line, asserted as a LAYOUT rather than as one
	@# hard-coded line of filenames.  It used to grep for the literal
	@# `A: ZCC Z8K : ZCC1 Z8K : ZCC2 Z8K : ZCC3 Z8K', which made every
	@# addition to drive A a false failure: landing Z80.Z8K shifted the
	@# grouping by one slot and the grep broke while SDIR was correct.
	@# What [SHORT] promises is four columns, so that is what is checked,
	@# plus that the compiler chain really is in the listing.
	@grep -Eq '^A: ([A-Z0-9$$]+ +[A-Z0-9]+ : ){3}[A-Z0-9$$]+ +[A-Z0-9]+[[:space:]]*$$' $(UTILLOG)-1.log \
		|| { echo "verify-util: FAIL -- SDIR [SHORT] four-per-line layout"; exit 1; }
	@for f in ZCC ZCC1 ZCC2 ZCC3; do \
		grep -Eq "(^|: )$$f +Z8K( :|$$| )" $(UTILLOG)-1.log \
		|| { echo "verify-util: FAIL -- SDIR [SHORT] lost $$f.Z8K"; exit 1; }; \
	done
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
	@# `SET *.SUB' and `SET HELLO.C HELLO.TXT' must reach BADP.SUB,
	@# TEST.SUB and HELLO.C.  Asserted per NAME, not as a total of 3: the
	@# total counts whatever *.SUB happens to match, so landing one more
	@# .SUB in src/dist/disk-a would fail this while SET was correct.
	@for f in BADP.SUB TEST.SUB HELLO.C; do \
		n=`echo $$f | sed 's/\..*//'`; t=`echo $$f | sed 's/.*\.//'`; \
		grep -Eq "A:$$n +\.$$t +set to directory \(DIR\), Read Write \(RW\)" \
			$(SETLOG)-1.log \
		|| { echo "verify-set: FAIL -- $$f was not covered by the wildcard or the two-name command"; exit 1; }; \
	done
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
	@# SETIN4's [PASSWORD=SECRET] names a FILE, which is BDOS function
	@# 103 -- and this drive's label does not arm passwords, so the
	@# BDOS refuses it (bdos30.asm:4975-4976) and SET says which of the
	@# two reasons it can be.  On an ARMED drive the same command works:
	@# verify-passfile is where that is shown, with the identical image
	@# one label bit apart as the control.
	@grep -q 'or protection not enabled for disk' $(SETLOG)-4.log \
		|| { echo "verify-set: FAIL -- [PASSWORD=] on a file of an unarmed drive was not refused"; exit 1; }
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
	@# The refusal is PIP's to REPORT, not the BDOS's.  With fn 12 saying
	@# 0x2031 PIP's HAS_RETERR is true (src/cmd/pip.c:423), so it opens
	@# with _ret_errors(0xff) and the BDOS hands the code back instead of
	@# printing: cpm3src/BDOS30.ASM:114, `lda error$$mode! inr a! cnz error'
	@# -- 0ffh+1 is zero, so the console message is skipped and rtn$$phy$$errs
	@# returns the code.  What must appear is therefore PIP's OWN text,
	@# error 22 + extended 2 (cpm3src/PIP.PLM:558 "CAN'T DELETE TEMP FILE",
	@# :590 "R/O DISK"), naming the scratch file it could not remove.  At
	@# 0x2022 the same refusal read "CP/M Disk change error on drive A"
	@# because the BDOS was the one talking; the write is refused either way.
	@grep -q 'ERROR: CAN.T DELETE TEMP FILE R/O DISK - A:RONEW[.][$$][$$][$$]' $(SETBLOG)-5.log \
		|| { echo "verify-setb: FAIL -- a write to the read-only drive was allowed, or PIP did not report the R/O DISK code the BDOS returned it"; exit 1; }
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
	@# EVERY name B:*.TXT matched must carry F4.  What is asserted is that
	@# equality -- marked == present -- and that the drive really is
	@# crowded past the old 128-entry table, rather than the literal 181:
	@# that number is 140+30+9 from setbfill.py plus whatever .TXT files
	@# src/dist/disk-b carries, so adding one there used to fail this
	@# while SET was correct.  The truncation bug fails the equality.
	@marked=`python3 tests/dirattr.py build/setb-4-b.img --user 0 | grep -c ' 4$$'`; \
	 present=`python3 tests/dirattr.py build/setb-4-b.img --user 0 | grep -c '\.TXT'`; \
	 test "$$marked" = "$$present" \
		|| { echo "verify-setb: FAIL -- only $$marked of $$present matching files got F4 (the expansion table truncated)"; exit 1; }; \
	 test "$$present" -gt 128 \
		|| { echo "verify-setb: FAIL -- only $$present names on B:, which does not reach past the old 128-entry table"; exit 1; }
	@echo "verify-setb: PASS -- SET on B: (stamped and not), user area 3, a page break answered, code 2 reported, every B:*.TXT name and 9 specs"

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
	@grep -q 'STAMPT: fn 102 on a wildcard -> 9/255' $(STAMPBLOG)-1.log \
		|| { echo "verify-stamp: FAIL -- fn 102 did not raise error 9 on a wildcard"; exit 1; }
	@grep -q '? in Filename' $(STAMPBLOG)-1.log \
		|| { echo "verify-stamp: FAIL -- error 9 printed no message"; exit 1; }
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
	@grep -q 'TRUNCT: fn 99 on a wildcard -> 9/255' $(TRUNCLOG) \
		|| { echo "verify-trunc: FAIL -- fn 99 did not raise error 9 on a wildcard"; exit 1; }
	@# the guest checks the code it got back; this checks the OTHER
	@# half of set$$aret -- that error mode 0FEh puts v3's own wording
	@# on the console (bdos30.asm:35 wildmsg, `? in Filename').
	@grep -q '? in Filename' $(TRUNCLOG) \
		|| { echo "verify-trunc: FAIL -- error 9 printed no message"; exit 1; }
	dd if=$(TRUNCIMG) of=build/trunc-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/trunc-fs
	python3 tools/mkcpmfs.py --extract build/trunc-cpma.img build/trunc-fs
	@test "`wc -c < build/trunc-fs/TRUNCT.TXT`" = 12800 \
		|| { echo "verify-trunc: FAIL -- the truncated file is not 100 records on disk"; exit 1; }
	@echo "verify-trunc: PASS -- fn 99 shortened the file and returned its blocks"

# ---- random read of an unwritten extent, then a random write there ----
# RANEXT writes 300 records (two directory entries), random-reads record
# 600 (error 4), then random-writes record 600 with the same FCB.  Error 4
# must restore the FCB's extent and module (bdos30.asm badseek), or the
# write lands in the blocks the FCB still maps and no entry is made.  The
# partition is pulled out afterwards so the host reads the file size too.
#
# It then opens the same file on extents 1, 2 and 21h.  Function 15 opens
# the extent it is handed -- v3 clears the module number and nothing else
# (bdos30.asm func15, and open$copy puts the caller's extent back after
# the directory entry is copied over it) -- so the first record read is
# that extent's own, 128 records in and 256 in.  Zeroing it, as this BDOS
# did, hands back extent 0 and a program that seeks past 16K by opening
# again reads the front of the file and calls it corrupt.
RANEXTIMG = build/ranext.bin
RANEXTLOG = build/verify-ranext.log
.PHONY: verify-ranext
verify-ranext: all
	$(MKDISK) $(RANEXTIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RANEXTIMG)) \
		--input="$(OSSEL)RANEXT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(RANEXTLOG))
	@$(EMUOK)
	@grep -q 'RANEXT: PASS' $(RANEXTLOG) \
		|| { echo "verify-ranext: FAIL -- see the BAD lines above"; exit 1; }
	dd if=$(RANEXTIMG) of=build/ranext-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/ranext-fs
	python3 tools/mkcpmfs.py --extract build/ranext-cpma.img build/ranext-fs
	@test "`wc -c < build/ranext-fs/RANEXT.TXT`" = 76928 \
		|| { echo "verify-ranext: FAIL -- the file is not 601 records on disk"; exit 1; }
	@echo "verify-ranext: PASS -- a random write after error 4 made its own extent,"
	@echo "               and an open on extent N started at record N*128"

# ---- the six refusals: check$wild and file$exists ----
# CP/M 3 refuses an ambiguous FCB on the four functions that name ONE
# file -- open (ref/cpm3/bdos30.asm:3924), make (:4241), rename, both
# names (:1803, :1817), and set file attributes (:4445) -- with error 9
# through check$wild (:1769), and refuses a make or a rename onto a name
# that already exists with error 8 through file$exists (:4371-4372,
# reached at :4248-4258 and :1820-1821).  This BDOS made none of the six
# until now: it opened, created, renamed and attributed against the
# first match, and let a second directory entry be written under a name
# that already had one -- the only gap in the v3 delta that can write a
# directory the format's own rules forbid.
#
# WILDT asks for all six under error mode 0FEh, which asks for both
# halves of set$aret at once: the guest checks the code it was handed,
# and the two greps below check the OTHER half -- that the console got
# v3's own wording (bdos30.asm:32 fxstsmsg `File Exists', :35 wildmsg
# `? in Filename').  Its last section is the part a refusal can break:
# an ordinary open, make, rename and set-attributes on the same drive,
# immediately afterwards, must all still work.
#
# Then the partition is taken apart host-side.  mkcpmfs --list groups a
# file's directory entries under its name, so `1 entry' on each of the
# survivors is the independent statement that the refused make and the
# refused rename left no duplicate behind -- which is the damage the
# whole exercise is about, and which no in-guest check can see.
WILDIMG = build/wildtest.bin
WILDLOG = build/verify-wild.log
.PHONY: verify-wild
verify-wild: all
	$(MKDISK) $(WILDIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(WILDIMG)) \
		--input="$(OSSEL)WILDT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(WILDLOG))
	@$(EMUOK)
	@grep -q 'WILDT: PASS' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'WILDT: fn 15 on a wildcard -> 9/255' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- fn 15 accepted a wildcard"; exit 1; }
	@grep -q 'WILDT: fn 22 on a wildcard -> 9/255' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- fn 22 accepted a wildcard"; exit 1; }
	@grep -q 'WILDT: fn 30 on a wildcard -> 9/255' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- fn 30 accepted a wildcard"; exit 1; }
	@grep -q 'WILDT: fn 23 wildcard 1st name -> 9/255' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- fn 23 accepted a wildcard in the source name"; exit 1; }
	@grep -q 'WILDT: fn 23 wildcard 2nd name -> 9/255' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- fn 23 accepted a wildcard in the destination name"; exit 1; }
	@grep -q 'WILDT: fn 22 on an existing name -> 8/255' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- fn 22 made a duplicate name"; exit 1; }
	@grep -q 'WILDT: fn 23 onto an existing name -> 8/255' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- fn 23 renamed onto an existing name"; exit 1; }
	@# the console half of set$$aret, in v3's own words
	@grep -q '? in Filename' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- error 9 printed no message"; exit 1; }
	@grep -q 'File Exists' $(WILDLOG) \
		|| { echo "verify-wild: FAIL -- error 8 printed no message"; exit 1; }
	dd if=$(WILDIMG) of=build/wild-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --list build/wild-cpma.img > build/wild-after.txt
	@grep 'WILDT' build/wild-after.txt
	@grep -qE '^ 0 WILDT\.TXT .* 1 entry' build/wild-after.txt \
		|| { echo "verify-wild: FAIL -- WILDT.TXT is not a single directory entry"; exit 1; }
	@grep -qE '^ 0 WILDT2\.TXT .* 1 entry' build/wild-after.txt \
		|| { echo "verify-wild: FAIL -- WILDT2.TXT is not a single directory entry"; exit 1; }
	@grep -qE '^ 0 WILDT4\.TXT .* 1 entry' build/wild-after.txt \
		|| { echo "verify-wild: FAIL -- the accepted rename did not land"; exit 1; }
	@if grep -q 'WILDT3.TXT' build/wild-after.txt; then \
		echo "verify-wild: FAIL -- WILDT3.TXT survived the rename"; exit 1; fi
	@echo "verify-wild: PASS -- six refusals made, and the unambiguous cases still work"

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
	@# STAT is built here from DRI source now (Makefile $(USTAT)), and the
	@# cpm8k13 source prints no sign-on -- its only version string is the
	@# usage text values() writes (src/cmd/stat.c:1188), which `STAT
	@# HELLO.TXT' never reaches.  All this assertion has to establish is
	@# that STAT ran to completion and so warm-booted, so it looks for the
	@# report it was asked for: the row naming the file on the command line.
	@grep -q 'A:HELLO   .TXT' $(SIGNLOG) \
		|| { echo "verify-signon: FAIL -- STAT did not run, or printed no row for HELLO.TXT"; exit 1; }
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
#
# The suite runs this in a tree of its own, a plain copy of the working
# tree made by tests/verifyrun.sh: it edits src/bdos/bdosmisc.c and
# rebuilds, so neither src/ nor build/ can be shared with anybody.
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
PWLBLCPMA = build/cpma-pwlabel.img
PWLBLIMG = build/lblnew-pwlabel.bin
LBLLOG = build/verify-lblnew
.PHONY: verify-lblnew
verify-lblnew: all
	python3 tools/mkcpmfs.py --initdir $(NOLBLCPMA) $(CPMA_BLOCKS) $(DISKA)
	python3 tools/mkcpmfs.py $(PLAINCPMA) $(CPMA_BLOCKS) $(DISKA)
	python3 tools/mkcpmfs.py --initdir --label C900P \
		--label-mode create,update,password \
		$(PWLBLCPMA) $(CPMA_BLOCKS) $(DISKA)
	@python3 tools/mkcpmfs.py --entries $(NOLBLCPMA) | grep -c ' label ' \
		| grep -qx 0 \
		|| { echo "verify-lblnew: FAIL -- the test image already has a label"; exit 1; }
	@# the password-bit image is only worth booting if the bit really
	@# is on the medium: fn 101's mask is what is under test, and a
	@# label without the bit would pass the guest check for nothing.
	@python3 tools/mkcpmfs.py --entries $(PWLBLCPMA) \
		| grep -q 'label C900P .*mode 0xb1 \[password,update,create,exists\]' \
		|| { echo "verify-lblnew: FAIL -- the password-label image has no password bit"; exit 1; }
	$(MKDISK) $(NOLBLIMG) $(CPMSYS) $(NOLBLCPMA)
	$(MKDISK) $(PLAINIMG) $(CPMSYS) $(PLAINCPMA)
	$(MKDISK) $(PWLBLIMG) $(CPMSYS) $(PWLBLCPMA)
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
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PWLBLIMG)) \
		--input="$(OSSEL)LBLNEW PWLABEL\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(LBLLOG)-4.log)
	@$(EMUOK)
	@grep -q 'LBLNEW: PASS' $(LBLLOG)-1.log \
		|| { echo "verify-lblnew: FAIL -- see the BAD lines above"; exit 1; }
	@grep -q 'LBLNEW: fn 100 made a label -> 00  fn 101 now f1' $(LBLLOG)-1.log \
		|| { echo "verify-lblnew: FAIL -- fn 100 did not make the label"; exit 1; }
	@grep -q 'Label for drive A:' $(LBLLOG)-2.log \
		|| { echo "verify-lblnew: FAIL -- the new label is invisible after a cold boot"; exit 1; }
	@grep -q 'C900N' $(LBLLOG)-2.log \
		|| { echo "verify-lblnew: FAIL -- SHOW [LABEL] found a different label"; exit 1; }
	@grep -q 'LBLNEW   TXT .* 06/14/85 12:34  06/14/85 12:34' $(LBLLOG)-2.log \
		|| { echo "verify-lblnew: FAIL -- SDIR [DATE] did not list the stamped file"; exit 1; }
	@grep -q 'LBLNEW: PASS' $(LBLLOG)-3.log \
		|| { echo "verify-lblnew: FAIL -- the no-SFCB image, see the BAD lines above"; exit 1; }
	@grep -q 'LBLNEW: fn 101 on a password-labelled drive -> b1' $(LBLLOG)-4.log \
		|| { echo "verify-lblnew: FAIL -- fn 101 hid the password bit of a label it did not write"; exit 1; }
	@grep -q 'LBLNEW: PASS' $(LBLLOG)-4.log \
		|| { echo "verify-lblnew: FAIL -- the password-label image, see the BAD lines above"; exit 1; }
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

# ---- passwords: enforced, and not enforced ----
#
# THE FAILURE THIS TARGET EXISTS TO CATCH is a password check that fires
# on a medium nobody armed, which would turn every disk this project has
# ever built unreadable.  So the two sessions below run the same program
# over the same three password XFCBs, the same three passwords and the
# same three files, and differ in ONE BIT: 80h of byte 12 of the
# directory label.  Everything the armed run is refused, the unarmed run
# must be allowed -- that is what PASST's `report' does, and it is why
# the control is not a formality.
#
# Three sessions, because mkcpmfs.py can put an XFCB on a finished image
# but cannot put a FILE on one:
#   1  PASST MAKE on a plain image, to create the three files
#   2  the same image, extracted, given a password label and three XFCBs
#   3  the same image again, given a label WITHOUT the password bit and
#      the identical three XFCBs
#
# The modes are one each so that all three are covered: 80h read, 40h
# write, 20h delete.  The passwords are ordinary words -- unlike
# verify-xfcb's, which is chosen for what its bytes decode to as block
# numbers, these are never read as anything but a password.
PASSMKCPMA = build/cpma-passmk.img
PASSMKIMG = build/passmk.bin
PASSARMCPMA = build/cpma-passarm.img
PASSCTLCPMA = build/cpma-passctl.img
PASSARMIMG = build/passarm.bin
PASSCTLIMG = build/passctl.bin
PASSLOG = build/verify-pass
PASSXFCB = --xfcb PASSR.TXT:0x80:RSECRET \
	   --xfcb PASSW.TXT:0x40:WSECRET \
	   --xfcb PASSD.TXT:0x20:DSECRET
.PHONY: verify-pass
verify-pass: all
	cp $(CPMAIMG) $(PASSMKCPMA)
	$(MKDISK) $(PASSMKIMG) $(CPMSYS) $(PASSMKCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PASSMKIMG)) \
		--input="$(OSSEL)PASST MAKE\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PASSLOG)-0.log)
	@$(EMUOK)
	@grep -q 'PASST: made' $(PASSLOG)-0.log \
		|| { echo "verify-pass: FAIL -- the three test files were not created"; exit 1; }
	dd if=$(PASSMKIMG) of=$(PASSARMCPMA) bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	cp $(PASSARMCPMA) $(PASSCTLCPMA)
	python3 tools/mkcpmfs.py --label C900P \
		--label-mode create,update,password $(PASSXFCB) $(PASSARMCPMA)
	python3 tools/mkcpmfs.py --label C900P \
		--label-mode create,update $(PASSXFCB) $(PASSCTLCPMA)
	@# the one bit really is the only difference, and it really is set:
	@# without this the armed run could pass by not being armed
	@python3 tools/mkcpmfs.py --entries $(PASSARMCPMA) \
		| grep -q 'label C900P .*mode 0xb1 \[password,update,create,exists\]' \
		|| { echo "verify-pass: FAIL -- the armed image has no password bit"; exit 1; }
	@python3 tools/mkcpmfs.py --entries $(PASSCTLCPMA) \
		| grep -q 'label C900P .*mode 0x31 \[update,create,exists\]' \
		|| { echo "verify-pass: FAIL -- the control image IS armed"; exit 1; }
	@test "`python3 tools/mkcpmfs.py --entries $(PASSARMCPMA) | grep -c ' xfcb '`" = 3 \
		|| { echo "verify-pass: FAIL -- the armed image does not carry three XFCBs"; exit 1; }
	@test "`python3 tools/mkcpmfs.py --entries $(PASSARMCPMA) | grep ' xfcb '`" \
	    = "`python3 tools/mkcpmfs.py --entries $(PASSCTLCPMA) | grep ' xfcb '`" \
		|| { echo "verify-pass: FAIL -- the two images' XFCBs differ"; exit 1; }
	$(MKDISK) $(PASSARMIMG) $(CPMSYS) $(PASSARMCPMA)
	$(MKDISK) $(PASSCTLIMG) $(CPMSYS) $(PASSCTLCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PASSARMIMG)) \
		--input="$(OSSEL)PASST\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PASSLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PASSCTLIMG)) \
		--input="$(OSSEL)PASST NONE\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PASSLOG)-2.log)
	@$(EMUOK)
	@grep -q 'PASST: PASS' $(PASSLOG)-1.log \
		|| { echo "verify-pass: FAIL -- the armed run, see the BAD lines above"; exit 1; }
	@grep -q 'PASST: PASS' $(PASSLOG)-2.log \
		|| { echo "verify-pass: FAIL -- THE UNARMED RUN.  A password check is firing on a medium nobody armed; see the BAD lines above"; exit 1; }
	@# the other half of error mode 0FEh: the console gets DRI's own
	@# wording (bdos30.asm:24-32) and the caller gets the code
	@grep -q 'Password Error' $(PASSLOG)-1.log \
		|| { echo "verify-pass: FAIL -- no 'Password Error' on the console"; exit 1; }
	@grep -q 'Password Error' $(PASSLOG)-2.log \
		&& { echo "verify-pass: FAIL -- the unarmed run reported a password error"; exit 1; } || true
	@echo "verify-pass: PASS -- three modes enforced on an armed drive,"
	@echo "        and the identical directory unenforced on an unarmed one"

# ---- SET's drive PASSWORD and PROTECT, which are function 100 ----
#
# The two forms this port can build.  [PROTECT=ON] is the label's
# password-enable bit -- the switch every check in verify-pass hangs off,
# reached through set$extent and wrlbl (set.plm:1149-1151, 332-333) -- and
# [PASSWORD=xxx] is the label's own password, bit 0 of the mode byte
# handed to function 100 plus the new password in the SECOND eight bytes
# of the DMA (:1035-1048, bdos30.asm:4918-4922).
#
# The second command is the interesting one.  Once the label has a
# password, function 100 refuses a label write that does not produce it,
# so `SET [UPDATE=OFF]' -- an option with nothing to do with passwords --
# is refused with error 7 and SET asks `Password ? '.  That prompt is
# answered by the next line of the script.  If it were not asked, the
# word SESAME would land at the A> prompt as an unknown command instead,
# and the [UPDATE=OFF] assertion below would fail.
#
# The FILE forms are function 103 and stay refused; verify-set greps for
# that message.
PASSSETCPMA = build/cpma-passset.img
PASSSETIMG = build/passset.bin
PASSSETLOG = build/verify-passset
# \g turns the input gate off before the answer, for the reason
# verify-initdir needs it (IDIN1FMT above): SET's "Password ? " is not a
# character the gate latches on, so without it the answer is never
# released and the session ends on idle at the prompt.
PASSSETIN = $(OSSEL)SET [PROTECT=ON,PASSWORD=SESAME]\rSET [UPDATE=OFF]\r\\gSESAME\r$(ENDIN)
.PHONY: verify-passset
verify-passset: all
	cp $(CPMAIMG) $(PASSSETCPMA)
	@python3 tools/mkcpmfs.py --entries $(PASSSETCPMA) \
		| grep -q 'label C900A .*mode 0x31 \[update,create,exists\]' \
		|| { echo "verify-passset: FAIL -- the image does not start unarmed"; exit 1; }
	$(MKDISK) $(PASSSETIMG) $(CPMSYS) $(PASSSETCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PASSSETIMG)) \
		--input="$(PASSSETIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PASSSETLOG).log)
	@$(EMUOK)
	@grep -q 'Passwds' $(PASSSETLOG).log \
		|| { echo "verify-passset: FAIL -- SET printed no label table"; exit 1; }
	@grep -q 'Password ? ' $(PASSSETLOG).log \
		|| { echo "verify-passset: FAIL -- a label write did not ask for the label's password"; exit 1; }
	@grep -q 'Wrong Password' $(PASSSETLOG).log \
		&& { echo "verify-passset: FAIL -- the password SET had just set was not accepted back"; exit 1; } || true
	@# the disk is the witness that does not share a BDOS with SET
	dd if=$(PASSSETIMG) of=build/passset-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --entries build/passset-cpma.img | grep ' label '
	@python3 tools/mkcpmfs.py --entries build/passset-cpma.img \
		| grep -q 'label C900A .*mode 0x91 \[password,create,exists\]' \
		|| { echo "verify-passset: FAIL -- the label did not end up armed with update stamping off"; exit 1; }
	@# 0x91: the image starts 0x31 (update, create, exists), the first
	@# command adds 0x80 and the second clears 0x20.  The `exists' bit
	@# surviving is the point of set.c's `mode &= 0xf0': in the byte
	@# handed to function 100 that bit means "assign a new password",
	@# so a label write that carried it back in from the label it read
	@# would silently re-assign the password every time.
	@echo "verify-passset: PASS -- [PROTECT=ON] armed the drive, [PASSWORD=] took,"
	@echo "        and the next label write had to produce it"

# ---- SET's FILE password: functions 103 and 106, there and back ----
#
# The round trip this pair of functions exists for, in four commands:
#
#	SET HELLO.TXT [PASSWORD=OPENUP,PROTECT=READ]	function 103
#	TYPE HELLO.TXT					refused
#	SET [DEFAULT=OPENUP]				function 106
#	TYPE HELLO.TXT					allowed
#
# The second and fourth commands are the same command, run by the same
# program on the same file, and the only thing between them is eight
# bytes in the BDOS.  TYPE is the witness on purpose: it is a CCP
# built-in that knows nothing about passwords and puts nothing at its DMA
# address, which is every program on this disk except PIP.  If the
# default password did not work, function 103 would be a way to lock a
# file that nothing could then open -- which is why 106 was built first.
#
# The control is verify-pass's control: the same commands on an image
# whose label differs in ONE BIT.  There function 103 refuses (the drive
# does not arm passwords, bdos30.asm:4975-4976), no XFCB is written, and
# both TYPEs print the file.  That is also the check that this target
# cannot pass by accident: if the armed run's refusal came from something
# other than the password, the control run would refuse too.
#
# The disk is the third witness, and it is the only one that does not
# share a BDOS with the program under test: the armed image ends with an
# XFCB for HELLO.TXT that it did not start with, and the control image
# ends with none.
PFCPMA	 = build/cpma-passfile.img
PFCTLCPMA = build/cpma-passfilectl.img
PFIMG	 = build/passfile.bin
PFCTLIMG = build/passfilectl.bin
PFLOG	 = build/verify-passfile
PFIN	 = $(OSSEL)SET HELLO.TXT [PASSWORD=OPENUP,PROTECT=READ]\rTYPE HELLO.TXT\rSET [DEFAULT=OPENUP]\rTYPE HELLO.TXT\rSET HELLO.C [PASSWORD=OPENUP,PROTECT=DELETE]\rSET HELLO.C [PROTECT=OFF]\rTYPE HELLO.C\rSDIR HELLO.*[XFCB]\rSDIR HELLO.*[NONXFCB]\r$(ENDIN)
.PHONY: verify-passfile
verify-passfile: all
	cp $(CPMAIMG) $(PFCPMA)
	cp $(CPMAIMG) $(PFCTLCPMA)
	python3 tools/mkcpmfs.py --label C900PF \
		--label-mode create,update,password $(PFCPMA)
	python3 tools/mkcpmfs.py --label C900PF \
		--label-mode create,update $(PFCTLCPMA)
	@python3 tools/mkcpmfs.py --entries $(PFCPMA) \
		| grep -q 'label C900PF .*mode 0xb1 \[password,update,create,exists\]' \
		|| { echo "verify-passfile: FAIL -- the armed image has no password bit"; exit 1; }
	@python3 tools/mkcpmfs.py --entries $(PFCTLCPMA) \
		| grep -q 'label C900PF .*mode 0x31 \[update,create,exists\]' \
		|| { echo "verify-passfile: FAIL -- the control image IS armed"; exit 1; }
	@test "`python3 tools/mkcpmfs.py --entries $(PFCPMA) | grep -c ' xfcb '`" = 0 \
		|| { echo "verify-passfile: FAIL -- the image already carries an XFCB"; exit 1; }
	$(MKDISK) $(PFIMG) $(CPMSYS) $(PFCPMA)
	$(MKDISK) $(PFCTLIMG) $(CPMSYS) $(PFCTLCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PFIMG)) \
		--input="$(PFIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PFLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PFCTLIMG)) \
		--input="$(PFIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PFLOG)-2.log)
	@$(EMUOK)
	@# ---- the armed run: SET took it, the first TYPE was refused and
	@# the second one, after [DEFAULT=], was not
	@grep -q 'Protection = READ, Password = OPENUP' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- SET did not report the file protected"; exit 1; }
	@grep -q 'Password Error' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- a protected file was TYPEd with no password"; exit 1; }
	@grep -q 'Default password = OPENUP' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- [DEFAULT=] was not accepted"; exit 1; }
	@test "`tr -d '\r' < $(PFLOG)-1.log | grep -c 'If you can TYPE this'`" = 1 \
		|| { echo "verify-passfile: FAIL -- the two TYPEs did not answer differently: exactly one of them must print the file"; exit 1; }
	@# ---- and back out again, on a second file.  Removing a password
	@# is an empty function-103 write followed by an XFCB-ONLY delete
	@# (f5', bdos30.asm:1602-1607); without that bit the same command
	@# would erase HELLO.C, so TYPEing it afterwards is the check that
	@# it did not.
	@grep -q 'Protection = DELETE, Password = OPENUP' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- [PROTECT=DELETE] did not take"; exit 1; }
	@grep -q 'Protection = NONE' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- [PROTECT=OFF] did not remove the protection"; exit 1; }
	@grep -q 'Hello from zcc' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- HELLO.C did not survive having its password removed"; exit 1; }
	@# ---- SDIR's [XFCB] and [NONXFCB], which became implementable when
	@# the directory started carrying XFCBs anyone could write.  The two
	@# listings partition the two files, and the Prot column -- driven
	@# by the SFCB byte function 103 maintains -- says READ for the one
	@# that has a password and None for the one that no longer does.
	@grep -q 'HELLO    TXT .*Read' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- SDIR [XFCB] did not list the protected file with its mode"; exit 1; }
	@grep -q 'HELLO    C .*None' $(PFLOG)-1.log \
		|| { echo "verify-passfile: FAIL -- SDIR [NONXFCB] did not list the unprotected file"; exit 1; }
	@test "`tr -d '\r' < $(PFLOG)-1.log | grep -c 'HELLO    TXT'`" = 1 \
		|| { echo "verify-passfile: FAIL -- SDIR [NONXFCB] listed the file that HAS an XFCB"; exit 1; }
	@# ---- the control run: one bit less, and nothing is refused
	@grep -q 'protection not enabled for disk' $(PFLOG)-2.log \
		|| { echo "verify-passfile: FAIL -- function 103 wrote an XFCB on a drive that arms nothing"; exit 1; }
	@grep -q 'Password Error' $(PFLOG)-2.log \
		&& { echo "verify-passfile: FAIL -- THE UNARMED RUN refused a TYPE"; exit 1; } || true
	@test "`tr -d '\r' < $(PFLOG)-2.log | grep -c 'If you can TYPE this'`" = 2 \
		|| { echo "verify-passfile: FAIL -- the unarmed run did not TYPE the file both times"; exit 1; }
	@grep -q 'Hello from zcc' $(PFLOG)-2.log \
		|| { echo "verify-passfile: FAIL -- the unarmed run lost HELLO.C"; exit 1; }
	@grep -q 'No File' $(PFLOG)-2.log \
		|| { echo "verify-passfile: FAIL -- SDIR [XFCB] found an XFCB on a disk that has none"; exit 1; }
	@# ---- and the disk
	dd if=$(PFIMG) of=build/passfile-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	dd if=$(PFCTLIMG) of=build/passfilectl-cpma.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --entries build/passfile-cpma.img | grep ' xfcb '
	@python3 tools/mkcpmfs.py --entries build/passfile-cpma.img \
		| grep -q 'xfcb  *HELLO *\.TXT .*mode 0x80' \
		|| { echo "verify-passfile: FAIL -- no XFCB for HELLO.TXT on the armed disk"; exit 1; }
	@test "`python3 tools/mkcpmfs.py --entries build/passfile-cpma.img | grep -c ' xfcb '`" = 1 \
		|| { echo "verify-passfile: FAIL -- HELLO.C's XFCB was not erased, or an extra one was written"; exit 1; }
	@python3 tools/mkcpmfs.py --list build/passfile-cpma.img | grep -q 'HELLO.C ' \
		|| { echo "verify-passfile: FAIL -- HELLO.C IS GONE: the XFCB-only delete erased the file"; exit 1; }
	@test "`python3 tools/mkcpmfs.py --entries build/passfilectl-cpma.img | grep -c ' xfcb '`" = 0 \
		|| { echo "verify-passfile: FAIL -- the unarmed disk grew an XFCB"; exit 1; }
	@echo "verify-passfile: PASS -- SET locked a file with function 103, TYPE could not"
	@echo "        open it, [DEFAULT=] supplied the password and the same TYPE could,"
	@echo "        and one bit less on the label makes all of it inert"

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
# ASZ8K, XCON, XDUMP, AR8K, NMZ8K and SIZEZ8K used to die here with a
# privileged-instruction TRAP; fixing the split-I/D fast path they
# fault through, all six run, and tests/legacychk.sh asserts that
# strictly rather than recording it.
# PIP and STAT are NO LONGER the vendor's binaries -- both are built from
# DRI source in src/cmd and staged over the vendor .Z8K ($(UPIP),
# $(USTAT) in the Makefile) -- so what is asserted about those two is
# their behaviour, not their identity.  legacychk.sh's header says which
# is which.  It also prints the dated summary line that is this row's
# retention record (see the script header for how to keep one).
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
# RCCFAIL/RCCPASS/RCCSTAT are F10's: they test `IF ERROR' against a
# TRANSIENT PROGRAM'S OWN exit status rather than one of the CCP's own
# failures, which IFERR/IFOK already cover.  The distinction is the whole
# of P1 #20: the CCP reaches its own failures through ccp_seterr()
# (src/ccp/ccpext.c), but a C command's status has to travel main() ->
# src/lib/cstart.c _setrc() -> BDOS fn 108 -> ccp_err(), and until F10
# nothing on that path wrote fn 108 at all.  Three commands, three exit
# shapes: DATE's `return (1)', MHELLO's `return (0)', and STAT's
# _exit(1) (src/cmd/pipmain.c), which is the only way out PIP and STAT
# have.  Each submit file types a DIFFERENT marker file per branch, so
# the transcript says which branch ran and not merely that one did.
#
# RCCSTAT GETS AN EMULATOR RUN OF ITS OWN, and the reason is a property of
# STAT rather than a convenience: every STAT message goes through print(),
# which is crlf(), which is new_ln() plus test_kbd_esc() (src/cmd/stat.c
# 400-425) -- so STAT eats one pending console character and aborts before
# it prints anything.  In the main session the character waiting is the
# first byte of the NEXT typed line, so STAT swallowed the `I' of `IFEX'
# and broke the test after it.  Alone, with its command line fully
# consumed and nothing else pending, STAT reaches its own invalid-
# assignment abort, which is the _exit(1) this asserts.  No ENDIN: the run
# stops on idle at the prompt the submit file leaves behind.
CCPVERIFYIN = $(OSSEL)U5HELLO PATHOK\rTYPE DEV:U5ONLY.TXT\rDEV:\rTYPE U5ONLY.TXT\rHOME:\rIFERR\rIFOK\rRCCFAIL\rRCCPASS\rIFEX\rBADCMD FOO\rSUBMIT TEST BUILTIN SECOND\r
CCPRCSTATIN = $(OSSEL)RCCSTAT\r
CCPRCSTATLOG = build/verify-ccp-rcstat.log
# F11: THE ASSEMBLY WITNESS, and the reason there is one.  RCERR.8KN and
# RCOK.8KN (src/dist/disk-ccp) set BDOS function 108 BY HAND -- three
# instructions and a warm boot, no C runtime underneath -- so they separate
# the two halves of the path F10's three commands exercise together.  If
# RCCFAIL fails and RCERR passes, src/lib/cstart.c is not publishing main's
# status; if both fail, the BDOS or the CCP is not carrying it.  Without
# that, a P1 #20 regression tells you a return code was lost and nothing
# about where.  Both were checked into the tree as fixtures for exactly this
# and NO TARGET RAN THEM; F10 having made the status actually arrive is what
# gives them something true to assert.
#
# They are assembled and converted ON TARGET, which nothing else in this
# suite does: verify-arx, verify-legacy and verify-rsx2 all stop at XCON or
# XDUMP and never load the result.  So this run also proves the devpack can
# produce a program the system will then RUN -- the last step of the
# self-hosting claim.  XCON's x.out is the command-file format, and the
# sources are position independent on purpose (no message, no jumps) because
# it emits no relocation records.
#
# ITS OWN RUN, for two reasons: the split tools go through the SC-trap shim
# and need ARXMAX rather than EMUMAX, and ASZ8K/XCON print through the same
# console the main session's type-ahead is queued in.  IFRC/IFRC0 are
# invoked bare, the way IFERR/IFOK are, and TYPE markers of their own so the
# transcript says which submit file and which branch -- the four RCIF4/
# RCELSE4/RCIF5/RCELSE5 files.  As shipped both fixtures typed HELLO.TXT
# and README.TXT, the same markers IFERR.SUB and IFOK.SUB use, which could
# not have been told apart in a transcript.
CCPRCASMIN = $(OSSEL)ASZ8K RCERR.8KN\rXCON -o RCERR.Z8K RCERR.OBJ\rASZ8K RCOK.8KN\rXCON -o RCOK.Z8K RCOK.OBJ\rDIR RC*.Z8K\rIFRC\rIFRC0\r
CCPRCASMLOG = build/verify-ccp-rcasm.log
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
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CCPTEST)) \
		--input="$(CCPRCSTATIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CCPRCSTATLOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CCPTEST)) \
		--input="$(CCPRCASMIN)" --max=$(ARXMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CCPRCASMLOG))
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
	@# F10: A TRANSIENT PROGRAM'S OWN EXIT STATUS, THROUGH `IF ERROR'.
	@# Both directions for all three, because the object here IS the
	@# return code: a test that only checks the IF branch was taken is
	@# passed by a system whose return code is always nonzero, and one
	@# that only checks the ELSE branch by a system where it is always
	@# zero.  Each assertion below names the branch that must NOT have
	@# run as well as the one that must.
	@grep -q 'RCC1: FAILING C COMMAND TOOK THE IF BRANCH' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- a C command's own failure (DATE Q, which"; \
		     echo "              returns 1 from main) did not reach \`IF ERROR'.  Either"; \
		     echo "              src/lib/cstart.c is not publishing main's status as BDOS"; \
		     echo "              function 108 or the CCP is not reading it (P1 #20)"; exit 1; }
	@grep -q 'RCC1: FAILING C COMMAND TOOK THE ELSE BRANCH' $(CCPLOG) \
		&& { echo "verify-ccp: FAIL -- the failing C command took the ELSE branch"; \
		     exit 1; } || true
	@grep -q 'RCC2: SUCCEEDING C COMMAND TOOK THE ELSE BRANCH' $(CCPLOG) \
		|| { echo "verify-ccp: FAIL -- a C command that returned 0 (MHELLO) did not"; \
		     echo "              take the ELSE branch, so the return code is not the"; \
		     echo "              program's: it is stuck nonzero"; exit 1; }
	@grep -q 'RCC2: SUCCEEDING C COMMAND TOOK THE IF BRANCH' $(CCPLOG) \
		&& { echo "verify-ccp: FAIL -- the succeeding C command took the IF branch"; \
		     exit 1; } || true
	@grep -q 'RCC3: STAT _exit(1) TOOK THE IF BRANCH' $(CCPRCSTATLOG) \
		|| { echo "verify-ccp: FAIL -- STAT's _exit(1) did not reach \`IF ERROR'."; \
		     echo "              _exit() (src/cmd/pipmain.c) is the ONLY exit PIP and"; \
		     echo "              STAT have; if it does not set fn 108 those two commands"; \
		     echo "              can never report failure"; exit 1; }
	@grep -q 'RCC3: STAT _exit(1) TOOK THE ELSE BRANCH' $(CCPRCSTATLOG) \
		&& { echo "verify-ccp: FAIL -- STAT's _exit(1) took the ELSE branch"; \
		     exit 1; } || true
	@# F11: the assembly witness.  Assert the OBJECTS first -- that the
	@# assembler and the converter actually produced a program -- because
	@# a missing RCERR.Z8K makes `RCERR' an unresolvable command, and the
	@# CCP's error handler would then leave the return code from the
	@# command BEFORE it, which `IF ERROR' would report as a pass.
	@# The DIRECTORY, not the echo of the XCON command line: a DIR listing
	@# writes the name padded to eight and the type with NO dot, so
	@# `RCERR' followed by spaces and `Z8K' can only come from the
	@# directory.  Matching `RCERR.Z8K' instead would have matched the
	@# echoed command and asserted nothing.
	@grep -qE 'RCERR +Z8K' $(CCPRCASMLOG) \
		|| { echo "verify-ccp: FAIL -- RCERR.Z8K is not in the directory, so"; \
		     echo "              ASZ8K + XCON did not produce a program and the"; \
		     echo "              IF ERROR assertions below cannot mean anything."; \
		     echo "              See $(CCPRCASMLOG)"; exit 1; }
	@grep -qE 'RCOK +Z8K' $(CCPRCASMLOG) \
		|| { echo "verify-ccp: FAIL -- RCOK.Z8K is not in the directory"; exit 1; }
	@grep -qi 'not a command\|CCP ERROR HANDLER' $(CCPRCASMLOG) \
		&& { echo "verify-ccp: FAIL -- something in the assemble/convert/run"; \
		     echo "              chain was not a command, so one of ASZ8K, XCON,"; \
		     echo "              RCERR or RCOK did not run"; exit 1; } || true
	@grep -q 'RCA1: ASM FN 108 = 7 TOOK THE IF BRANCH' $(CCPRCASMLOG) \
		|| { echo "verify-ccp: FAIL -- a hand-written function 108 = 7 did not"; \
		     echo "              reach \`IF ERROR'.  This path has no C runtime in"; \
		     echo "              it, so with RCCFAIL passing the fault is in the"; \
		     echo "              BDOS or the CCP, not in src/lib/cstart.c"; exit 1; }
	@grep -q 'RCA1: ASM FN 108 = 7 TOOK THE ELSE BRANCH' $(CCPRCASMLOG) \
		&& { echo "verify-ccp: FAIL -- function 108 = 7 took the ELSE branch"; \
		     exit 1; } || true
	@grep -q 'RCA2: ASM FN 108 = 0 TOOK THE ELSE BRANCH' $(CCPRCASMLOG) \
		|| { echo "verify-ccp: FAIL -- a hand-written function 108 = 0 did not"; \
		     echo "              take the ELSE branch: the return code is stuck"; \
		     echo "              nonzero, or RCOK's store did not land"; exit 1; }
	@grep -q 'RCA2: ASM FN 108 = 0 TOOK THE IF BRANCH' $(CCPRCASMLOG) \
		&& { echo "verify-ccp: FAIL -- function 108 = 0 took the IF branch"; \
		     exit 1; } || true
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
#	    extents into one entry (EXM 1, BLS 4096, src/bios/bios900.c:668-676),
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
# The htpa values are exact: FDF8 unfenced, EDF8 with a module at F000
# (RSXORG), so a fence that fails to move, or moves by the wrong amount,
# fails the check rather than merely looking different.  FDF8 is the top of
# the segment less the base page, the default stack and the segmented entry
# frame: the CCP's state is resident per-process storage now
# (src/ccp/ccpsv.h) and reserves nothing in the TPA, so the 0x600 that used
# to sit above this is the program's.
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
	@# FDF8: nothing of the system's is reserved in the TPA, so with no
	@# module resident the fence is the top of the segment and the program
	@# gets all of it, less only its own base page and stack.  This read
	@# F7F8 while the CCP's state page was reserved at 0xFA00; that state
	@# is resident per-process storage now (src/ccp/ccpsv.h) and the 1,536
	@# bytes came back to the program.  With a module at F000 the module
	@# still dominates and the number below is unchanged.
	@test "`grep -c 'rsxt: htpa=FDF8' $(RSXLOG)`" = 2 \
		|| { echo "verify-rsx: FAIL -- the TPA top is not the top of the segment with no module resident"; exit 1; }
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
#      first and UCASEL.RSX (UCASE linked low, src/tests/ucrsxl.s) under it,
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
#   PROT.RSX (src/cmd/ucrsx.s, src/tests/prsx.s).  With both resident, each
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
# PROT at E800, EDF8 for UCASE at F000, DDF8 for UCASEL at E000, FDF8
# for none -- nothing of the system's is reserved in the TPA, because the
# CCP's state is resident per-process storage (src/ccp/ccpsv.h).
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
#   (src/tests/ucrsx3.s) claims 0CAh instead, so with it at the HEAD and
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
	@# FDF8: with both temporary modules gone the fence goes back to the
	@# top of the segment, because nothing of the system's is reserved in
	@# the TPA -- the CCP's state is resident per-process storage now
	@# (src/ccp/ccpsv.h).  This read F7F8 while that state was a page at
	@# 0xFA00.  Same correction as verify-rsx.
	@grep -q 'rsxt2: htpa=FDF8' $(RSX2LOG)-5.log \
		|| { echo "verify-rsx2: FAIL -- the fence did not go back to the top of the segment"; exit 1; }
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
#      PROTN.RSX (src/tests/prsxn.s) is PROT.RSX with the bank flag set, which
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
	@# FDF8: "the whole TPA" is the whole segment again.  This read F7F8
	@# while the CCP's state page was reserved at 0xFA00; that state is
	@# resident per-process storage now (src/ccp/ccpsv.h), so the stripped
	@# program really does get all of it back.
	@grep -q 'rsxt2: htpa=FDF8' $(GCLOG)-1.log \
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
# CRSRDEMO's coordinates (src/tests/crsrdemo.c) are the contract: box corners
# at (4,10)/(4,60)/(14,10)/(14,60), labels inside it, the four one-step
# motions around the anchor at (2,40), an erase-to-end-of-line at (16,30)
# and an erase-to-end-of-screen at (19,20).  Row 0 carries the attribute
# and not-implemented sequences, row 15 the insert/delete-line pair, whose
# two halves cancel so the rest of the frame is left where it was.
# ---- directory hashing on/off A-B test (PLAN.md sec 9 rows 5 and 8) ----
# Row 5 (hashing/BCB) was CANNOT-VERIFY and row 8 (GENCPM) was FAIL for the
# same reason: nothing could turn hashing off, so there was no A-B to run.
# src/bdos/dskhash.c hashen[2] is that switch (see its own comment); this
# stamps a second cpm.sys with it turned off and compares the two on the
# EMULATOR's own instruction count (see tests/hashab.py's own docstring
# for why a wildcard `DIR' cannot show this and TYPE can).
#
# The two systems are the same link: only the config file differs, which is
# what makes this an A-B of the generator and not of two builds.
HASHOFFDIR = build/hashoff
HASHOFFSYS = $(HASHOFFDIR)/cpm.sys
HASHOFFDAT = $(HASHOFFDIR)/gencpm.dat
HASHABSRC  = build/hashab-src
HASHABB    = build/hashab-b.img
HASHABON   = build/hashab-on.bin
HASHABOFF  = build/hashab-off.bin
HASHABN    = 384

$(HASHOFFSYS): $(CPMSYS) $(GENCPMDAT) tools/gencpm.py
	@mkdir -p $(HASHOFFDIR)
	sed -e 's/^\(hash_[ab]\)[^=]*=.*/\1 = off/' $(GENCPMDAT) > $(HASHOFFDAT)
	cp $(CPMSYS) $@
	python3 tools/gencpm.py $(HASHOFFDAT) $@
	@python3 tools/gencpm.py --dump $@ | grep -c '^hash_[ab] .* 00$$' | grep -qx 2 \
		|| { echo "hashoff: the stamp left hashing on -- there is no B side to measure"; exit 1; }

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

$(HASHABOFF): $(HASHOFFSYS) $(CPMARIMG) $(HASHABB) $(wildcard $(KBOOT)) tools/mkcpmdisk.py
	$(MKDISK) $@ $(HASHOFFSYS) $(CPMARIMG) $(HASHABB)

.PHONY: verify-hash-ab
verify-hash-ab: $(HASHABON) $(HASHABOFF)
	python3 tests/hashab.py --emu $(EMU) --on $(abspath $(HASHABON)) \
		--off $(abspath $(HASHABOFF)) --n $(HASHABN)

# ---- tools/gencpm.py: the config file governs the shipped image ----
# A COPY of the linked system is stamped with settings that differ from the
# ones the port ships and booted: the CCP comes up on the configured drive
# and function 107 returns the configured serial, with nothing recompiled.
# The copy is what gets patched, so a concurrent target's build/cpm.sys is
# left alone.
#
# The build itself stamps build/cpm.sys from tools/gencpm.dat, so the first
# assertion is that the shipped settings still match what the sources were
# compiled with: the stamp report must say it changed nothing.  Without it a
# default build could quietly ship something other than its own sources.
GCSYS	= build/gencpm-cpm.sys
GCDAT	= build/gencpm-test.dat
GCBAD	= build/gencpm-bad.dat
GCBADSYS = build/gencpm-bad.sys
GCIMG	= build/gencpm.bin
GCLOG	= build/verify-gencpm.log
# CPM3FN prints the serial; it lives on A:, and the prompt under test is B:.
# STAT lays its columns out from the console width it reads back (SCB 1ah),
# so a narrow console drops the header to `Attrib' where 80 says `Attributes'.
GCIN	= $(OSSEL)A:\rCPM3FN\rSTAT CPM3FN.Z8K\r$(ENDIN)
# A second image, differing from the shipped settings in the page length
# alone: the BDOS pager is off at 0 and pauses every third line at 3.  The
# pager prompt is this run's stop mark, because nothing answers it -- the
# scripted input reaches the CCP, not a BDOS console read inside the pause.
GCPSYS	= build/gencpm-page.sys
GCPDAT	= build/gencpm-page.dat
GCPIMG	= build/gencpm-page.bin
GCPLOG	= build/verify-gencpm-page.log
GCPIN	= $(OSSEL)DIR\r
GCPMARK	= Press RETURN to Continue

.PHONY: verify-gencpm
verify-gencpm: all $(CPMAIMG) $(CPMBIMG)
	@grep -q 'unchanged from $(GENCPMDAT)' $(OBJDIR)/gencpm.txt \
		|| { cat $(OBJDIR)/gencpm.txt; echo "verify-gencpm: FAIL -- $(GENCPMDAT) no longer agrees with the compiled-in values, so a default build does not ship its own sources"; exit 1; }
	@cp $(CPMSYS) $(GCBADSYS)
	@i=0; for bad in \
	  'serial = C90001\ndefault_drive = A\nhash_a = on\nhash_b = on\nserail = C90002\n' \
	  'default_drive = A\nhash_a = on\nhash_b = on\n' \
	  'serial = TOOLONG\ndefault_drive = A\nhash_a = on\nhash_b = on\n' \
	  'serial = C90001\ndefault_drive = A\nhash_a = maybe\nhash_b = on\n'; do \
		i=`expr $$i + 1`; printf "$$bad" > $(GCBAD); \
		if python3 tools/gencpm.py $(GCBAD) $(GCBADSYS) 2>/dev/null; then \
			echo "verify-gencpm: FAIL -- rejected config $$i was accepted; a misspelt or bad setting would ship a value nobody chose"; exit 1; \
		fi; \
	done
	@cmp -s $(CPMSYS) $(GCBADSYS) \
		|| { echo "verify-gencpm: FAIL -- a refused config still altered the image"; exit 1; }
	cp $(CPMSYS) $(GCSYS)
	@printf 'serial = ZZZZZZ\ndefault_drive = B\nhash_a = on\nhash_b = on\ncon_width = 40\ncon_page = 0\n' \
		> $(GCDAT)
	python3 tools/gencpm.py $(GCDAT) $(GCSYS)
	$(MKDISK) $(GCIMG) $(GCSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(GCIMG)) \
		--input="$(GCIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(GCLOG))
	@$(EMUOK)
	@grep -q 'serial  -> ZZZZZZ' $(GCLOG) \
		|| { echo "verify-gencpm: FAIL -- fn 107 did not return the configured serial"; exit 1; }
	@grep -q 'B>A:' $(GCLOG) \
		|| { echo "verify-gencpm: FAIL -- the CCP did not come up on the configured drive"; exit 1; }
	@grep -q 'FCBs Attrib   Name' $(GCLOG) \
		|| { echo "verify-gencpm: FAIL -- STAT laid out for a wide console, so the configured width never reached the SCB"; exit 1; }
	cp $(CPMSYS) $(GCPSYS)
	@printf 'serial = C90001\ndefault_drive = A\nhash_a = on\nhash_b = on\ncon_width = 80\ncon_page = 3\n' \
		> $(GCPDAT)
	python3 tools/gencpm.py $(GCPDAT) $(GCPSYS)
	$(MKDISK) $(GCPIMG) $(GCPSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(GCPIMG)) \
		--input="$(GCPIN)" --max=$(EMUMAX) --stop-on=idle \
		--stop-mark='$(GCPMARK)' 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(GCPLOG))
	@$(EMUOK)
	@grep -q '$(GCPMARK)' $(GCPLOG) \
		|| { echo "verify-gencpm: FAIL -- the pager never paused, so the configured page length never reached the BDOS"; exit 1; }
	@echo "verify-gencpm: PASS -- the build stamps the shipped settings without changing them, and a restamped serial, default drive, console width and page length all reach a running system"

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
	@# a sequence this console does not implement leaves the screen
	@# merely without the attribute, never with stray text on it
	python3 tests/vt.py $(CRSRLOG) --cell 0 0 'REVNORMEND' --blank 0 10 20 \
		|| { echo "verify-crsr: FAIL -- an unimplemented escape left its bytes on the screen"; exit 1; }
	@for e in p q x '(' ')' 3 '@' F; do \
		grep -qF "`printf '\033'`$$e" $(CRSRLOG) \
		&& { echo "verify-crsr: FAIL -- ESC $$e reached the console raw"; exit 1; }; \
	done; true
	@grep -qF "`printf '\033[0;7m'`" $(CRSRLOG) \
		|| { echo "verify-crsr: FAIL -- reverse video did not reach the terminal as ANSI"; exit 1; }
	@# insert and delete line: row 15 was opened over the mark, and the
	@# rows below came back to where they started
	python3 tests/vt.py $(CRSRLOG) --cell 15 0 'INS' --blank 15 3 20 \
		|| { echo "verify-crsr: FAIL -- ESC L did not open a line over the mark"; exit 1; }
	@# nothing was broken for ordinary output: the sign-on still reads
	@# straight, and the CCP prompt is where the program left the cursor
	python3 tests/vt.py $(CRSRLOG) --cell 22 0 'CRSRDEMO done.' --cell 24 0 'A>' \
		|| { echo "verify-crsr: FAIL -- ESC M did not take it out again, or the CCP did not resume from where the program left the cursor"; exit 1; }
	@echo "verify-crsr: PASS -- addressing, motion, erase, line editing, attributes and pass-through, on the screen"

# The harness is K&R, like the source it includes.

build/crsrtest: tests/crsrtest.c src/bios/crsr.c | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -o $@ tests/crsrtest.c

# ---- the owned video-console keyboard, on the host (verify-kbd) ----
# src/bios/kbd900.h, the video console's keyboard, against COHERENT's own
# C900 keyboard driver, compiled unmodified as the oracle: the init writes, every scan
# code up and down in all 64 shift states, and the poll/ack path.  No
# emulator here has a keyboard, so this is the test.  COH_KBDDIR is the
# original driver's directory in the commodore-900-coh-kernel3 checkout,
# found as tools/deps.sh finds a sibling: walking outward from here, then in
# repos/.  Four parents, not deps.sh's three, so that a worktree nested at
# <repo>/.claude/worktrees/<id> still reaches the workspace.
COH_KBDREL = commodore-900-coh-kernel3/os/sys/z8001/rec
COH_KBDDIR ?= $(or $(patsubst %/kb.c,%,$(firstword $(wildcard $(foreach d,\
	.. ../.. ../../.. ../../../.. repos,$(abspath $(d))/$(COH_KBDREL)/kb.c)))),\
	$(abspath ..)/$(COH_KBDREL))

# The oracle is COHERENT source that is not ours to vendor, so it is only
# ever a sibling checkout.  Where it is absent -- CI, a fresh clone -- there
# is nothing to compare against and the target skips.
.PHONY: verify-kbd
verify-kbd:
	@if test -f $(COH_KBDDIR)/kb.c; then \
	  $(MAKE) --no-print-directory build/kbdtest && ./build/kbdtest; \
	else \
	  echo "verify-kbd: SKIP -- no COHERENT keyboard driver beside this checkout"; \
	fi

# kbtab.h has no include guard, so the driver and its table are two objects.
build/kbdtest: tests/kbdtest.c tests/kbdoracle.c tests/kbdorat.c \
		src/bios/kbd900.h | $(OBJDIR)
	@mkdir -p build/kbdstub
	@: > build/kbdstub/coherent.h; : > build/kbdstub/tty.h; : > build/kbdstub/io.h
	$(HOSTCC) -std=gnu89 -w -c -o build/kbdoracle.o -Ibuild/kbdstub \
		-I$(COH_KBDDIR) tests/kbdoracle.c
	$(HOSTCC) -std=gnu89 -w -c -o build/kbdorat.o -I$(COH_KBDDIR) tests/kbdorat.c
	$(HOSTCC) -std=gnu89 -w -o $@ tests/kbdtest.c build/kbdoracle.o build/kbdorat.o

# ---- two programs alive at once (verify-conc) ----
# The process descriptor and the cooperative switch: src/bdos/proc.c, BDOS
# function 144.  CONC.Z8K creates a second process out of CONCB.Z8K -- a
# whole other 64 KB image, on its own physical page behind its own Z8010
# descriptor -- and the two print loops then alternate, LINE FOR LINE,
# because the dispatcher runs at the SC #2 gate's return and each line is
# one function 9 call.
#
# THE ASSERTIONS BELOW ARE ORDERING ASSERTIONS AND THAT IS THE POINT.
# Counting the lines proves nothing: a program that chained to another
# one, or one program printing both sets of lines, produces the same
# multiset.  What only two live processes can produce is B's lines
# INTERLEAVED BETWEEN A's, so the check is on the sequence.  The second
# session is the control: CONCB run from the A> prompt on its own emits
# the same B lines with nothing between them.
#
# CONCB checks 4 KB of its own memory after printing.  Both programs are
# linked at the same address (UBASE 0x32000000, and nothing here
# relocates), so that buffer sits exactly where CONC's variables are; it
# survives only because segment 0x32 names a different physical page
# depending on which process is running, which is the whole memory model
# (src/bios/pgalloc.c pgtpaswap).
CONCIMG	= build/conctest.bin
CONCLOG	= build/verify-conc.log
.PHONY: verify-conc
verify-conc: all
	$(MKDISK) $(CONCIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCIMG)) \
		--input="$(OSSEL)$(SESS1)CONC\rCONCB\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCLOG))
	@$(EMUOK)
	@grep -q 'CONC A: two processes' $(CONCLOG) \
		|| { echo "verify-conc: FAIL -- CONC did not run at all"; exit 1; }
	@grep -q 'CONC: no second process' $(CONCLOG) \
		&& { echo "verify-conc: FAIL -- function 144 refused; the reason is in the transcript above."; \
		     echo "            A 512 KB machine has no free page and this cannot work there"; \
		     echo "            (src/bios/pgalloc.c) -- but the emulator models 1 MB."; exit 1; } || true
	@# THREE, not two, since C10: a session (SESSION 1, typed first since D8) runs on
	@# console 1, and it is live for the whole
	@# run.  The claim here is that fn 144's child is COUNTED, and that
	@# is the same claim at 3 as it was at 2 -- the pair of numbers this
	@# target reads, 3 while the child lives and 2 after it ends, still
	@# differ by exactly the one process it created.
	@grep -q 'CONC: live processes = 3' $(CONCLOG) \
		|| { echo "verify-conc: FAIL -- function 145 does not see three live processes"; exit 1; }
	@grep -q 'CONCB: B alive' $(CONCLOG) \
		|| { echo "verify-conc: FAIL -- the second image never ran"; exit 1; }
	@# THE INTERLEAVING, and it is asserted as three ORDERING properties
	@# rather than as one exact string.  The exact order depends on how
	@# many BDOS calls each program happens to make before its loop, which
	@# is a fact about these two files; what has to be true of the SYSTEM
	@# is weaker and stronger at the same time:
	@#
	@#   1. B printed while A was still printing -- the child got the
	@#      machine at function 144's return, so creation really did
	@#      switch, and a chained program cannot produce it;
	@#   2. B printed AGAIN after A had printed -- control came back to the
	@#      child a second time, which a chained program cannot do and a
	@#      one-shot hand-off cannot do either.  This is the round robin;
	@#   3. after B ended, every remaining line is A's, and there is at
	@#      least one -- A ran on alone, so the ended process really was
	@#      reclaimed.
	@#
	@# ALL THREE ARE NOW WRITTEN WITHOUT NAMING A LINE NUMBER, and S5 is
	@# why.  They used to read "^B01," and "A3,A4,A5,A6,$$", which is this
	@# claim plus an assumption about WHICH slice each program is in when
	@# it prints -- true of a scheduler that only ever switches at a BDOS
	@# call, and false the moment the tick can take the machine away
	@# (src/bios/trap.s ttick_).  Under preemption B does its 4 KB
	@# self-check while A prints, so B's first line can land after A's and
	@# its last can land after A's last.  The claims above are the same
	@# three claims; what has gone is the transcript they were read off.
	@#
	@# The first fifteen loop lines are the first session (twelve A, three
	@# B); the three after them are the control run of CONCB on its own.
	@grep -E '^  (A|B) [0-9]+' $(CONCLOG) | head -15 | tr -d ' \r' | tr '\n' ',' \
		> build/conc-order.txt
	@grep -qE 'B[0-9]+,A' build/conc-order.txt \
		|| { echo "verify-conc: FAIL -- the child never printed while the parent was"; \
		     echo "            still printing, so what happened looks like a chain, not"; \
		     echo "            a switch at function 144's return."; \
		     echo "            order was: `cat build/conc-order.txt`"; exit 1; }
	@# Claims 2 and 3 are checked on a SECOND reduction that also carries
	@# B's exit line, because "B ran again" and "B had ended" are both
	@# events the numbered-line alphabet above cannot see.  Sixteen events
	@# is the first session (fifteen loop lines and B's exit), which keeps
	@# the control run of CONCB out of the answer.
	@grep -E '^  (A|B) [0-9]+|CONCB: B done' $(CONCLOG) | head -16 \
		| sed -e 's/.*CONCB: B done.*/Bdone/' | tr -d ' \r' | tr '\n' ',' \
		> build/conc-order2.txt
	@grep -qE 'A[0-9]+,B' build/conc-order2.txt \
		|| { echo "verify-conc: FAIL -- the child never ran a SECOND time, so what"; \
		     echo "            happened is a hand-off, not a switch.  Two live processes"; \
		     echo "            taking turns is the whole claim of this target."; \
		     echo "            order was: `cat build/conc-order2.txt`"; exit 1; }
	@rest=`sed -e 's/.*Bdone,//' build/conc-order2.txt`; \
	  case "$$rest" in \
	  *B*)	echo "verify-conc: FAIL -- the child printed after it had finished"; \
		echo "            order was: `cat build/conc-order2.txt`"; exit 1;; \
	  *A*)	;; \
	  *)	echo "verify-conc: FAIL -- A did not run alone after B ended"; \
		echo "            order was: `cat build/conc-order2.txt`"; exit 1;; \
	  esac
	@grep -q 'CONCB: B done, 4096 bytes of my own intact' $(CONCLOG) \
		|| { echo "verify-conc: FAIL -- the second process's memory did not survive;"; \
		     echo "            the two images are sharing a page instead of each having one"; exit 1; }
	@grep -q 'CONC: A done, live processes = 2' $(CONCLOG) \
		|| { echo "verify-conc: FAIL -- the ended process was not reclaimed"; exit 1; }
	@# The control: CONCB alone, no interleaving, and the CCP came back
	@# after a background process had lived and died in a swapped page.
	@test "`grep -c 'CONCB: B alive' $(CONCLOG)`" = 2 \
		|| { echo "verify-conc: FAIL -- CONCB did not also run as an ordinary transient"; exit 1; }
	@echo "verify-conc: PASS -- two programs alive at once, alternating at the BDOS gate"

# ---- the prompt comes back while the job runs (verify-conc2) ----
# THE OTHER HALF OF verify-conc, and the thing verify-conc explicitly did
# not deliver: "`A>' returns while a long job continues".  CONC.Z8K could
# only interleave with CONCB while CONC ITSELF was still making BDOS
# calls, because the switch was taken only at the gate's return and the
# CCP waits for a command INSIDE a BDOS call.  src/bdos/proc.c pyield()
# takes a switch from in there, on a supervisor stack of the waiting
# process's own, and that is what this target measures.
#
# The session is one line of input and then one more:
#
#	A>CONCP		 CONCP starts CONCQ and EXITS
#	CONCP: P start
#	CONCQ: Q alive
#	CONCP: P done
#	A>		 the prompt, with no program of ours running
#	  Q 01		 ...and the job continuing behind it
#	  Q 02
#	A>CONCB		 the next command, typed while the job runs
#	CONCB: B alive
#	  Q 04
#	CONCQ: Q done, 4096 bytes of my own intact
#
# THE ASSERTIONS ARE ORDERING ASSERTIONS, for the reason verify-conc
# gives: a chained program produces the same multiset of lines, so
# counting proves nothing.  What only a yielding console read can produce
# is Q lines AFTER the prompt that follows CONCP's exit, and Q lines
# after a DIFFERENT program has been typed, loaded and run.
#
# CONCQ computes silently between lines on purpose (src/tests/concq.c says
# why): the emulator will not hand a keystroke to a guest that is busy
# printing, so without the gaps the second command could not be typed
# until the job had finished, and the run would prove only half of this.
CONC2IMG = build/conc2test.bin
CONC2LOG = build/verify-conc2.log
.PHONY: verify-conc2
verify-conc2: all
	$(MKDISK) $(CONC2IMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONC2IMG)) \
		--input="$(OSSEL)CONCPCONCB" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONC2LOG))
	@$(EMUOK)
	@grep -q 'CONCP: P start' $(CONC2LOG) \
		|| { echo "verify-conc2: FAIL -- CONCP did not run at all"; exit 1; }
	@grep -q 'CONCP: no second process' $(CONC2LOG) \
		&& { echo "verify-conc2: FAIL -- function 144 refused; the reason is in the transcript above."; \
		     echo "             A 512 KB machine has no free page and this cannot work there"; \
		     echo "             (src/bios/pgalloc.c) -- but the emulator models 1 MB."; exit 1; } || true
	@grep -q 'CONCQ: Q alive' $(CONC2LOG) \
		|| { echo "verify-conc2: FAIL -- the background job never ran"; exit 1; }
	@# The transcript reduced to the five things whose ORDER is the claim:
	@#   P  CONCP exited          Q  a line of the background job
	@#   >  a CCP prompt          B  the next command, running
	@#   E  the background job finished
	@# The prompt is matched loosely on purpose.  It is printed as
	@# characters through the BDOS like anything else, so the background
	@# job can and does land BETWEEN the `A' and the `>' -- which is
	@# itself the interleaving, but it means "the line that is the
	@# prompt" is not always one line.  The specific markers are
	@# substituted first, so a prompt character sharing a line with one
	@# of them is counted as the marker and not twice.
	@sed -e 's/\r//' $(CONC2LOG) \
		| sed -n -e 's/.*CONCP: P done.*/P/p' \
			 -e 's/.*CONCQ: Q done.*/E/p' \
			 -e 's/.*CONCB: B alive.*/B/p' \
			 -e 's/^  Q [0-9][0-9].*/Q/p' \
			 -e 's/.*A>.*/>/p' \
			 -e 's/^>.*/>/p' \
		| tr -d '\n' > build/conc2-order.txt
	@echo "verify-conc2: order was `cat build/conc2-order.txt`"
	@grep -qE 'P[Q>]*>Q' build/conc2-order.txt \
		|| { echo "verify-conc2: FAIL -- no background line came out AFTER the prompt that"; \
		     echo "             followed CONCP's exit.  That prompt is the CCP sitting in"; \
		     echo "             BDOS function 10 with nothing else of ours running, so a Q"; \
		     echo "             line there is only possible if the console read yielded"; \
		     echo "             (src/bdos/proc.c pyield, conbdos.c getch).  This is THE"; \
		     echo "             assertion of this target."; \
		     echo "             order was: `cat build/conc2-order.txt`"; exit 1; }
	@# `B[>]*Q' rather than `BQ', and S5 is why: the prompt that follows
	@# the typed command's exit can now fall between the command running
	@# and the background job's next line, because the job is sharing the
	@# machine by TIME and not by call and its gaps no longer line up with
	@# anyone's console I/O.  The claim is unchanged -- a background line
	@# after a different program was typed, loaded and run -- and the
	@# prompt in between is not evidence against it.
	@grep -qE 'B[>]*Q' build/conc2-order.txt \
		|| { echo "verify-conc2: FAIL -- the next command was typed and run, but no"; \
		     echo "             background line followed it, so the job was already over"; \
		     echo "             by then and \"you can type the next command WHILE it runs\""; \
		     echo "             is not what this transcript shows."; \
		     echo "             order was: `cat build/conc2-order.txt`"; exit 1; }
	@grep -q 'CONCB: B done, 4096 bytes of my own intact' $(CONC2LOG) \
		|| { echo "verify-conc2: FAIL -- the typed command did not complete"; exit 1; }
	@grep -q 'CONCQ: Q done, 4096 bytes of my own intact' $(CONC2LOG) \
		|| { echo "verify-conc2: FAIL -- the background job did not finish with its own"; \
		     echo "             4 KB intact: its page did not survive a warm boot, a CCP"; \
		     echo "             reload and another program running in the TPA"; exit 1; }
	@echo "verify-conc2: PASS -- the A> prompt came back while a long job kept running,"
	@echo "              and the next command was typed and run alongside it"

# ---- preemption: two compute-bound jobs, no console I/O (verify-conc3) ----
# THE ONE THING verify-conc AND verify-conc2 CANNOT TEST.  Both of those
# switch at a BDOS call: their programs print, and a print is a call, and
# the dispatcher runs at the call's return.  A program that only computes
# gave the machine away to nobody, ever, before S5 -- "whoever holds the
# CPU holds it until it calls the BDOS" is how src/bdos/proc.c's banner put
# it -- and that is the requirement's remaining half.
#
# CONCX creates CONCY (function 144) and both then compute with no console
# I/O and no BDOS call whatsoever until they are finished.  CONCY's loop is
# SIXTEEN TIMES the longer.  So the ORDER OF THE TWO `done' LINES IS THE
# SCHEDULER, and it is a discriminator rather than a threshold:
#
#   cooperative  the child takes the machine at function 144's gate return
#                and holds it for its whole computation -- `Y done' first,
#                then `X done', always, whatever the machine's speed;
#   preemptive   the tick takes it away mid-loop (src/bios/trap.s ttick_
#                calling src/bdos/proc.c pdisp, and ONLY when the
#                interrupted FCW says Normal mode), CONCX runs its much
#                shorter loop in the slices it gets -- `X done' FIRST.
#
# The printed tick counts are evidence, not an assertion: they are read
# through the RAW SC #3 BIOS gate (BIOS function 24), which does not
# dispatch, so reading the clock is not itself a switch point.  Both
# programs are on $(CPMACONC) rather than $(CPMAIMG) for the reason the
# Makefile's $(UCONCFS) comment gives: adding files to A: moves
# verify-rtc's alignment, and that image is left as it is.
CONC3IMG = build/conc3test.bin
CONC3LOG = build/verify-conc3.log
.PHONY: verify-conc3
verify-conc3: all $(CPMACONC)
	$(MKDISK) $(CONC3IMG) $(CPMSYS) $(CPMACONC) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONC3IMG)) \
		--input="$(OSSEL)CONCX\r" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONC3LOG))
	@$(EMUOK)
	@grep -q 'CONCX: X start' $(CONC3LOG) \
		|| { echo "verify-conc3: FAIL -- CONCX did not run at all"; exit 1; }
	@grep -q 'CONCX: no second process' $(CONC3LOG) \
		&& { echo "verify-conc3: FAIL -- function 144 refused; the reason is in the"; \
		     echo "              transcript above.  A 512 KB machine has no free page"; \
		     echo "              (src/bios/pgalloc.c); the emulator models 1 MB."; exit 1; } || true
	@grep -q 'CONCY: MEMORY CLOBBERED' $(CONC3LOG) \
		&& { echo "verify-conc3: FAIL -- the preempted process's 4 KB did not survive."; \
		     echo "              A switch at an arbitrary instruction moved a page under it."; \
		     exit 1; } || true
	@grep -q 'CONCX: X done' $(CONC3LOG) \
		|| { echo "verify-conc3: FAIL -- the FOREGROUND job never finished.  It computes"; \
		     echo "              without calling the BDOS, so if it never got the machine"; \
		     echo "              back, nothing preempted the child."; exit 1; }
	@grep -q 'CONCY: Y done' $(CONC3LOG) \
		|| { echo "verify-conc3: FAIL -- the background job never finished"; exit 1; }
	@tr -d '\r' < $(CONC3LOG) | sed -n -e 's/.*CONCX: X done.*/X/p' \
					  -e 's/.*CONCY: Y done.*/Y/p' \
		| tr -d '\n' > build/conc3-order.txt
	@echo "verify-conc3: order was `cat build/conc3-order.txt`, ticks `tr -d '\r' < $(CONC3LOG) | sed -n 's/.*ticks=\([0-9]*\).*/\1/p' | tr '\n' ' '`"
	@test "`cat build/conc3-order.txt`" = "XY" \
		|| { echo "verify-conc3: FAIL -- the short foreground job did not finish FIRST."; \
		     echo "              Both jobs compute with no BDOS call in the loop, and the"; \
		     echo "              background one is 16x the longer, so YX is the"; \
		     echo "              cooperative scheduler: the child held the machine from"; \
		     echo "              function 144's gate return until it was done.  Nothing"; \
		     echo "              took it away on a tick."; \
		     echo "              order was: `cat build/conc3-order.txt`"; exit 1; }
	@echo "verify-conc3: PASS -- two compute-bound jobs, no console I/O between them,"
	@echo "              and both progressed: the tick is what switched them"

# ---- WHAT AN IDLE SECOND CONSOLE COSTS (verify-conc5) ----
# C5's measurement, and the one number a user would actually feel.
#
# THE CLAIM.  Two compute-bound jobs with a second terminal attached and
# NOBODY AT IT must retire the same work in the same time as the same two
# jobs alone.  Not "roughly", not "the transcript looks reasonable": the
# target runs the identical program twice on the identical disk image, once
# with a console session on console 1 and once without, and prints the two
# tick counts side by side with the difference as a percentage.
#
# WHY IT WAS NOT TRUE BEFORE.  A session waiting for a keystroke blocked by
# SPINNING through pyield() (run/C2.md decision 1, taken deliberately to
# stay out of pnext() while C1 was in flight).  A spinning waiter keeps its
# place in the round robin, so three live processes with one idle meant the
# two real ones got two thirds of the machine between them -- and run/C3.md
# section 5 had to make SESSION a call rather than something the cold boot
# does, because starting one unconditionally would have cost EVERY existing
# target half its machine for nothing.  C5's wait list takes a blocked
# process out of pnext()'s rotation, and this target is what says so.
#
# WHAT THE NUMBERS ARE.  CONCZ times its own one-unit loop; CONCY, which it
# creates, times its sixteen-unit one.  Both read the clock through the raw
# SC #3 BIOS gate, so reading it is not itself a switch point (src/tests/
# concx.c has the argument).  Both counts are compared, because they fail
# in different ways: CONCZ is short and would show a startup cost, CONCY is
# long and would show a steady-state one.
#
# THE OTHER HALF OF THE CLAIM is that the session was really there.  CONCZ
# prints function 145's process count from inside the timed run: 2 alone
# (itself and CONCY), 3 with the session.  A WITH run printing 2 would be
# a session that failed to start and a comparison of nothing with nothing,
# so the target checks both counts before it compares any ticks.
#
# CONCZTOL is the tolerance in percent, applied to both jobs, and it was
# MEASURED against the thing it has to reject rather than guessed.
#
# Run/C3.md estimated the spin's cost at "half the machine" and that
# estimate is WRONG -- see run/C5.md.  C1's preemption is why: a spinning
# idle process that is handed the machine by the tick returns from its
# pyield() inside getch(), re-tests a console that is still empty and gives
# it straight back, so it costs two switches per tick and not a slice.
# Removing the `pd_wait == PW_RUN' test from pnext() (src/bdos/proc.c) and
# rebuilding turns this target's CONCY row from 0.6% into 7.4%, on the same
# tree, with everything else unchanged.  Those are the two numbers the
# tolerance sits between, so it is 3: comfortably above the 0.6% a correct
# wait list costs, and comfortably below the 7.4% the spin costs.
#
# It is a discriminator and not a calibration: the 0.6% is one console poll
# per dispatch (pwscan -> conststat) and does not grow with the work, while
# the 7.4% is a share of every tick and does.
CONCZTOL = 3
CONCZAIMG = build/conc5a.bin
CONCZBIMG = build/conc5b.bin
CONCZALOG = build/verify-conc5-alone.log
CONCZBLOG = build/verify-conc5-idle.log
.PHONY: verify-conc5
verify-conc5: all $(CPMACONCZ)
	$(MKDISK) $(CONCZAIMG) $(CPMSYS) $(CPMACONCZ) $(CPMBIMG)
	cp $(CONCZAIMG) $(CONCZBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCZAIMG)) \
		--input="$(OSSEL)$(SESS1)CONCZ\r" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCZALOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCZBIMG)) \
		--input="$(OSSEL)$(SESS1)CONCZ S\r" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCZBLOG))
	@$(EMUOK)
	@tr -d '\r' < $(CONCZALOG) > build/conc5a.txt
	@tr -d '\r' < $(CONCZBLOG) > build/conc5b.txt
	@grep -q 'CONCZ: no session' build/conc5b.txt \
		&& { echo "verify-conc5: FAIL -- function 142 refused the session; the reason"; \
		     echo "              is in the transcript above.  A machine the loader"; \
		     echo "              found no spare serial port on has one console"; \
		     echo "              (src/bios/bios900.c coninit)."; exit 1; } || true
	@grep -q 'CONCZ: no second process' build/conc5a.txt build/conc5b.txt \
		&& { echo "verify-conc5: FAIL -- function 144 refused; a 512 KB machine has no"; \
		     echo "              free page (src/bios/pgalloc.c).  The emulator is 1 MB."; \
		     exit 1; } || true
	@# The session was really there, and only in the second run.
	@# ONE MORE IN BOTH RUNS SINCE C10: SESSION 1, typed first since D8, starts a session
	@# on console 1, so the baseline is 3 and
	@# the idle-console run is 4.  Both runs carry it, so the DIFFERENCE
	@# -- which is the only thing this target measures -- is the session
	@# `CONCZ I' asks for and nothing else.
	@grep -q 'CONCZ: live=3' build/conc5a.txt \
		|| { echo "verify-conc5: FAIL -- the ALONE run did not see exactly three live"; \
		     echo "              processes, so it is not the baseline it claims to be."; \
		     grep 'CONCZ: live' build/conc5a.txt; exit 1; }
	@grep -q 'CONCZ: live=4' build/conc5b.txt \
		|| { echo "verify-conc5: FAIL -- the IDLE-CONSOLE run did not see four live"; \
		     echo "              processes.  Without the session there is nothing to"; \
		     echo "              measure the cost of and the comparison is empty."; \
		     grep 'CONCZ: live' build/conc5b.txt; exit 1; }
	@sed -n 's/^CONCZ: Z done, ticks=\([0-9][0-9]*\)$$/\1/p' build/conc5a.txt \
		> build/conc5-za.txt
	@sed -n 's/^CONCZ: Z done, ticks=\([0-9][0-9]*\)$$/\1/p' build/conc5b.txt \
		> build/conc5-zb.txt
	@sed -n 's/.*CONCY: Y done.*ticks=\([0-9][0-9]*\).*/\1/p' build/conc5a.txt \
		> build/conc5-ya.txt
	@sed -n 's/.*CONCY: Y done.*ticks=\([0-9][0-9]*\).*/\1/p' build/conc5b.txt \
		> build/conc5-yb.txt
	@for f in za zb ya yb; do test -s build/conc5-$$f.txt \
		|| { echo "verify-conc5: FAIL -- a job never printed its tick count ($$f)."; \
		     echo "              One of the two runs did not finish; the transcripts"; \
		     echo "              are above."; exit 1; }; done
	@# THE TABLE.  Printed, not eyeballed: the target computes the two
	@# percentages itself and then asserts on them.
	@awk -v tol=$(CONCZTOL) \
	     -v za=`cat build/conc5-za.txt` -v zb=`cat build/conc5-zb.txt` \
	     -v ya=`cat build/conc5-ya.txt` -v yb=`cat build/conc5-yb.txt` \
	  'BEGIN { \
	     printf "verify-conc5: an idle second console, measured\n"; \
	     printf "  %-22s %8s %8s %9s\n", "job", "alone", "+idle", "delta"; \
	     dz = (zb - za) * 100.0 / za; dy = (yb - ya) * 100.0 / ya; \
	     printf "  %-22s %8d %8d %8.1f%%\n", "CONCZ  1 unit", za, zb, dz; \
	     printf "  %-22s %8d %8d %8.1f%%\n", "CONCY 16 units", ya, yb, dy; \
	     printf "  processes live         %8d %8d\n", 2, 3; \
	     az = dz < 0 ? -dz : dz; ay = dy < 0 ? -dy : dy; \
	     if (az <= tol && ay <= tol) { \
	       printf "verify-conc5: PASS -- both jobs within %d%%: the idle console is\n", tol; \
	       printf "              out of the ready rotation and costs them nothing\n"; \
	       exit 0; } \
	     printf "verify-conc5: FAIL -- an idle console cost the two real jobs more\n"; \
	     printf "              than %d%%.  A third process spinning in a round robin\n", tol; \
	     printf "              of three costs a THIRD; a blocked one that pnext()\n"; \
	     printf "              (src/bdos/proc.c) skips costs one console poll per\n"; \
	     printf "              dispatch.  This says it is not being skipped.\n"; \
	     exit 1; }'

# ===========================================================================
# TWO PROCESSES INSIDE THE FILE SYSTEM AT ONCE.
#
# verify-conc and verify-conc2 prove that two processes exist and that one
# can be parked inside a BDOS call.  Neither touches a disk, so neither says
# anything about the question S4 actually raises: `snglthrd' is still
# defined (src/bdos/bdosdef.h), so every BDOS function still reads and
# writes the ONE static `struct stvars', and what is claimed to make that
# safe is that proc.c copies the whole structure out and the next process's
# copy in at every switch (psave/pload).
#
# THE REDUCTION these three targets are built on.  A process leaves the
# BDOS mid-call at exactly two places: pyield() called from plock(), and
# pyield() called from getch().  plock() only yields when SOMEBODY ELSE
# holds the file-system lock, and holding it while not running means being
# parked mid-call -- so by induction the root of all mid-call parking is
# getch().  TWO PROCESSES ARE INSIDE THE BDOS AT ONCE IF AND ONLY IF ONE IS
# BLOCKED ON A CONSOLE READ.  Ordinary console reads (functions 1, 6, 10)
# hold no file-system state; the only others are the operator prompts
# error() (src/bdos/bdosmisc.c) puts up, and those are reached from inside a
# directory scan.  That is a small enumerable set of paths rather than an
# unbounded interleaving, and these three targets are one per case.
# ===========================================================================

CONCDIMG = build/concdir.bin
CONCDLOG = build/concdir.log

# verify-concdir -- THE ERROR-FREE PATH, where the claim should hold.
#
# Two programs walk the directory with SEARCH_FIRST/SEARCH_NEXT.  A search
# carries state ACROSS BDOS calls -- GBL.srchpos, GBL.srchp, GBL.dmaadr,
# GBL.dirsecn and the 128-byte GBL.pdirbuf the entry is delivered from --
# and with two processes live the dispatcher switches at EVERY BDOS call
# return (proc.c pdisp), so the two interleave one call each all the way
# down.  Each must come out with its own whole, correct answer.
#
#   1. CONCD's wildcard walk of A: gives the same count and the same name
#      checksum concurrently as it did alone      (`clobbered=0')
#   2. CONCE's narrower walk prints the same numbers beside CONCD as it
#      does in the CONTROL run afterwards, alone -- which is what makes (1)
#      mean something rather than being self-consistent nonsense
#   3. CONCD's exact-name search never stops finding a file that is on the
#      disk                                       (`lostname=0')
#   4. CONCD really did overlap CONCE             (`rounds' >= 2)
verify-concdir: all $(CPMACONC)
	$(MKDISK) $(CONCDIMG) $(CPMSYS) $(CPMACONC) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCDIMG)) \
		--input="$(OSSEL)CONCD\rCONCE\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCDLOG))
	@$(EMUOK)
	@grep -q 'CONCD: D start' $(CONCDLOG) \
		|| { echo "verify-concdir: FAIL -- CONCD did not run at all"; exit 1; }
	@grep -q 'CONCD: no second process' $(CONCDLOG) \
		&& { echo "verify-concdir: FAIL -- function 144 refused; the reason is in the"; \
		     echo "                transcript above.  A 512 KB machine has no free page"; \
		     echo "                (src/bios/pgalloc.c); the emulator models 1 MB."; exit 1; } || true
	@test "`grep -c 'CONCE: E alive' $(CONCDLOG)`" = 2 \
		|| { echo "verify-concdir: FAIL -- CONCE did not run both beside CONCD and"; \
		     echo "                afterwards alone, so its answer has no control"; exit 1; }
	@tr -d '\r' < $(CONCDLOG) | grep '^CONCE: n=' | sort -u > build/concdir-e.txt
	@test "`wc -l < build/concdir-e.txt`" = 1 \
		|| { echo "verify-concdir: FAIL -- CONCE's directory walk gave a DIFFERENT"; \
		     echo "                answer beside another process than it gives alone."; \
		     echo "                That is a shared search state.  The lines were:"; \
		     cat build/concdir-e.txt; exit 1; }
	@grep -q 'wobble=0' build/concdir-e.txt \
		|| { echo "verify-concdir: FAIL -- CONCE's own walks disagreed with each other"; \
		     cat build/concdir-e.txt; exit 1; }
	@tr -d '\r' < $(CONCDLOG) | grep '^CONCD: rounds=' > build/concdir-d.txt \
		|| { echo "verify-concdir: FAIL -- CONCD never reported"; exit 1; }
	@grep -qE 'rounds=([2-9]|[1-9][0-9]+) ' build/concdir-d.txt \
		|| { echo "verify-concdir: FAIL -- CONCD got fewer than two walks in beside"; \
		     echo "                CONCE, so nothing was actually concurrent:"; \
		     cat build/concdir-d.txt; exit 1; }
	@grep -q 'clobbered=0' build/concdir-d.txt \
		|| { echo "verify-concdir: FAIL -- CONCD's directory walk gave a different"; \
		     echo "                answer while another process was walking.  The"; \
		     echo "                per-process search state is not per-process:"; \
		     cat build/concdir-d.txt; exit 1; }
	@grep -q 'lostname=0' build/concdir-d.txt \
		|| { echo "verify-concdir: FAIL -- an exact-name search stopped finding a"; \
		     echo "                file that is on the disk:"; \
		     cat build/concdir-d.txt; exit 1; }
	@grep -q 'CONCD: D done' $(CONCDLOG) \
		|| { echo "verify-concdir: FAIL -- CONCD did not finish"; exit 1; }
	@echo "verify-concdir: PASS -- two processes walked the directory a BDOS call"
	@echo "                apart and each got its own whole, correct answer"

CONCEIMG = build/concerr.bin
CONCELOG = build/concerr.log

# verify-concerr -- THE ERROR PATH WITH THE LOCK FREE, which is where the
# only real hole was.
#
# error(5) sits ABOVE the LOCK in delete() and truncit(), and above any
# lock at all in bdosrw.c:246, so a read-only file parks a process INSIDE a
# directory scan with the file system wide open to everybody else.  CONCF
# ERAses a 40 KB read-only file -- two directory entries on this geometry
# (BLS 4096, EXM 1: 32 KB to an entry) -- and function 19 is
# dirscan(delete, fcb, full), so the scan must still find the second entry
# after the prompt.  CONCG meanwhile makes the first reference to drive B:,
# and there is ONE directory signature table for the machine, which
# dhopen()/dhdone() (src/bdos/dskhash.c) rebuild for whichever drive is
# being logged in.
#
# THE ORDERING IS THE HARNESS'S, NOT THE SCHEDULER'S.  --input-mark holds
# the character that answers the prompt until CONCG has printed that it
# logged B: in, so "the table was repointed while the scan was parked" is
# arranged rather than hoped for, and the target is deterministic: it failed
# three times out of three before the fix and passes three out of three
# after it.
#
# The assertion is `after=0': every entry of the file was erased.  Nonzero
# is the second entry surviving an ERA that reported success, with its
# blocks still marked allocated.
# THE LAYOUT IS PART OF THE TEST (H7).  The bug is that filero()'s nested
# dirscan reads a SECOND directory record into the one directory buffer,
# leaving the delete() that called error(5) holding a pointer into the wrong
# one -- so it only bites when CONCTGT.TXT's two entries are in different
# records.  Which they are is decided by how many entries the rest of A:
# takes: 97 and 124 straddle, 95, 96 and 98 do not, and a session on a
# non-straddling disk passes having tested nothing.  tests/concpad.sh pins
# the count when the image is built; concfree.py refuses to run the session
# on a disk where the pin did not hold.
verify-concerr: all $(CPMACONC)
	python3 tests/concfree.py $(CPMACONC)
	$(MKDISK) $(CONCEIMG) $(CPMSYS) $(CPMACONC) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCEIMG)) \
		--input="$(OSSEL)CONCF\r\iC$(ENDIN)" --input-mark="CONCG: FLIPPED" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCELOG))
	@$(EMUOK)
	@grep -q 'CONCF: F start' $(CONCELOG) \
		|| { echo "verify-concerr: FAIL -- CONCF did not run at all"; exit 1; }
	@grep -q 'CONCF: no second process' $(CONCELOG) \
		&& { echo "verify-concerr: FAIL -- function 144 refused; see the transcript"; exit 1; } || true
	@grep -q 'CONCF: before=2' $(CONCELOG) \
		|| { echo "verify-concerr: FAIL -- the target is not a two-entry file, so the"; \
		     echo "                scan has no work left after the prompt and the test"; \
		     echo "                would pass without testing anything"; exit 1; }
	@grep -q 'is read-only' $(CONCELOG) \
		|| { echo "verify-concerr: FAIL -- no operator prompt came up, so no process"; \
		     echo "                was ever parked inside the directory scan"; exit 1; }
	@grep -q 'CONCG: FLIPPED' $(CONCELOG) \
		|| { echo "verify-concerr: FAIL -- CONCG never logged drive B: in, so the"; \
		     echo "                signature table was never repointed"; exit 1; }
	@# The mark holds the answer until CONCG has flipped the table, so the
	@# prompt must appear BEFORE that line in the transcript.
	@tr -d '\r' < $(CONCELOG) | grep -n 'is read-only\|CONCG: FLIPPED' \
		| head -2 | cut -d: -f2- > build/concerr-order.txt
	@head -1 build/concerr-order.txt | grep -q 'is read-only' \
		|| { echo "verify-concerr: FAIL -- CONCG logged drive B: in BEFORE the scan"; \
		     echo "                parked, so dhstart() declined to hash and nothing"; \
		     echo "                was tested.  Order was:"; cat build/concerr-order.txt; exit 1; }
	@grep -q 'CONCF: FAIL' $(CONCELOG) \
		&& { echo "verify-concerr: FAIL -- a directory scan parked at an operator"; \
		     echo "                prompt resumed against a signature table that had"; \
		     echo "                been repointed at another drive underneath it"; \
		     echo "                (src/bdos/dskhash.c dhstart/dhcand), and skipped"; \
		     echo "                entries it had to erase.  ERA reported success and"; \
		     echo "                left directory entries behind with their blocks"; \
		     echo "                still allocated."; exit 1; } || true
	@grep -q 'CONCF: F done' $(CONCELOG) \
		|| { echo "verify-concerr: FAIL -- CONCF did not finish"; exit 1; }
	@echo "verify-concerr: PASS -- a directory scan parked at an operator prompt"
	@echo "                survived another process logging a drive in underneath it"

CONCLIMG = build/conclk.bin
CONCLLOG = build/conclk.log

# verify-conclk -- THE ERROR PATH WITH THE LOCK HELD: wait, or deadlock?
#
# close() (src/bdos/fileio.c) is the one prompt that comes up under the
# lock: LOCK, merge the disk map, THEN error(5).  Everybody else is then
# shut out of the file system at their first do_phio(), because plock()
# finds the lock held by another process and yields to them.  Both
# processes end up calling pyield() at each other -- CONCH from getch()
# waiting for the answer, CONCI from plock() waiting for the lock -- and
# what has to still work is the console, because getch() re-tests kbchar
# and bconstat on every turn.
#
# Two runs of the same image decide it, and neither is a timing argument:
#
#   1. UNANSWERED.  The prompt is never answered, so the lock is never
#      released.  CONCI must stall between `I try 1' and `I got 1' for the
#      whole run.  If `I got 1' appears, the lock does not exclude.
#   2. ANSWERED.  Everything must finish.  If it does not, the wait was a
#      deadlock.
#
# The answer is fed as type-ahead (\i) rather than paced, and that is
# itself a finding worth keeping: with two processes spinning at each other
# the machine no longer LOOKS idle, so the emulator's ordinary input pacing
# never fires.  The wait is a busy spin, not a sleep.
verify-conclk: all $(CPMACONC)
	$(MKDISK) $(CONCLIMG) $(CPMSYS) $(CPMACONC) $(CPMBIMG)
	@echo "--- 1. the prompt is never answered: the lock must exclude"
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCLIMG)) \
		--input="$(OSSEL)CONCH\r" --max=$(CONCLMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCLLOG))-1.log
	@$(EMUOK)
	@grep -q 'CONCI: I alive' $(CONCLLOG)-1.log \
		|| { echo "verify-conclk: FAIL -- the second process never ran"; exit 1; }
	@grep -q 'is read-only' $(CONCLLOG)-1.log \
		|| { echo "verify-conclk: FAIL -- close() never reached its operator prompt,"; \
		     echo "               so nothing was parked holding the lock"; exit 1; }
	@grep -q 'I-try-1' $(CONCLLOG)-1.log \
		|| { echo "verify-conclk: FAIL -- CONCI never asked for the file system"; exit 1; }
	@grep -q 'I-got-1' $(CONCLLOG)-1.log \
		&& { echo "verify-conclk: FAIL -- CONCI completed a drive login while another"; \
		     echo "               process was parked at an operator prompt HOLDING the"; \
		     echo "               file-system lock.  LOCK/UNLOCK do not exclude."; exit 1; } || true
	@# and nothing may finish, because nothing can
	@grep -q 'CONCH: H done' $(CONCLLOG)-1.log \
		&& { echo "verify-conclk: FAIL -- the close returned without being answered"; exit 1; } || true
	@echo "--- 2. the prompt is answered: everything must finish"
	$(MKDISK) $(CONCLIMG) $(CPMSYS) $(CPMACONC) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCLIMG)) \
		--input="$(OSSEL)CONCH\r\iC" --max=$(CONCLMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCLLOG))-2.log
	@$(EMUOK)
	@grep -q 'CONCH: H done' $(CONCLLOG)-2.log \
		|| { echo "verify-conclk: FAIL -- the close never returned after the prompt"; \
		     echo "               was answered: the wait is a deadlock"; exit 1; }
	@grep -q 'CONCI: I done' $(CONCLLOG)-2.log \
		|| { echo "verify-conclk: FAIL -- the process that was waiting on the lock"; \
		     echo "               never got it back after the lock was released"; exit 1; }
	@echo "verify-conclk: PASS -- a prompt under the lock shuts the other process"
	@echo "               out completely, and lets it straight back in when answered"

# ---- the MP/M XDOS calls, one process (verify-xdos) ----
# src/bdos/xdos.c, BDOS functions 128-141 and 148/149/153.  XDOSM.Z8K makes
# every call whose ANSWER does not depend on a second process running: what
# the memory descriptor calls hand back and what the page pool says
# afterwards, what a console number round-trips to, what the conditional
# queue calls say about an empty and a full queue, what the flag calls
# refuse.  The blocking half is verify-xdos2.
#
# THE PROGRAM COUNTS ITS OWN CHECKS and prints the count.  That is the whole
# reporting protocol, and it is deliberate: a target that greps for each
# individual answer has to be edited every time a check is added, and a
# check that is silently dropped looks exactly like one that passed.  Here
# the transcript carries both numbers, so this target can assert that none
# failed AND that the expected number ran.
#
# Function 131 on device 0 (the console keyboard) is deliberately NOT among
# the checks: it blocks until a key arrives and a scripted session cannot
# promise one at a chosen moment.  xdosm.c says so where the other device
# numbers are checked, and verify-xdos2 measures the waiting mechanism it
# would have exercised.
XDOSIMG	= build/xdostest.bin
XDOSLOG	= build/verify-xdos.log
XDOSCHK	= 46
.PHONY: verify-xdos
verify-xdos: all $(CPMAXDOS)
	$(MKDISK) $(XDOSIMG) $(CPMSYS) $(CPMAXDOS) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XDOSIMG)) \
		--input="$(OSSEL)XDOSM\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(XDOSLOG))
	@$(EMUOK)
	@grep -q 'XDOSM: XDOS calls' $(XDOSLOG) \
		|| { echo "verify-xdos: FAIL -- XDOSM did not run at all"; exit 1; }
	@tr -d '\r' < $(XDOSLOG) | grep '^XDOSM: .* checks, ' > build/xdos-count.txt \
		|| { echo "verify-xdos: FAIL -- XDOSM never reported a count, so it did"; \
		     echo "             not reach the end of its checks"; exit 1; }
	@grep -q ' 0 failed' build/xdos-count.txt \
		|| { echo "verify-xdos: FAIL -- checks failed; each one named itself above:"; \
		     grep 'XDOSM: FAIL' $(XDOSLOG); exit 1; }
	@grep -q "^XDOSM: $(XDOSCHK) checks," build/xdos-count.txt \
		|| { echo "verify-xdos: FAIL -- $(XDOSCHK) checks were expected and the run"; \
		     echo "             reported `cat build/xdos-count.txt`."; \
		     echo "             A check that never ran is not a check that passed."; \
		     echo "             If checks were added or removed on purpose, XDOSCHK"; \
		     echo "             in tests/verify.mk is the number to change."; exit 1; }
	@echo "verify-xdos: PASS -- `cat build/xdos-count.txt`"

# ---- the XDOS calls that BLOCK (verify-xdos2) ----
# The other half, and the only half that says anything about concurrency.
# XDOSD blocks three times -- function 141 on a delay, 132 on a flag, 137 on
# an empty queue -- and XDOSE prints a numbered line every time it is given
# the machine.  XDOSE ends with function 143, MP/M's Terminate, instead of
# returning.
#
# THE ASSERTIONS ARE COUNTS BETWEEN MARKERS, and that is the point.  Under
# the round robin two processes alternate LINE FOR LINE at the BDOS gate
# (verify-conc measures exactly that), so a call that did not block leaves
# at most one E line between D's two markers.  A call that did block leaves
# many.  The threshold below is three: comfortably above what a non-blocking
# call can produce and far below what the delay actually yields.
#
# Each count is extracted with sed's range operator on D's own markers, so
# the check does not depend on how many lines E gets in -- only on there
# being more than a hand-off's worth.
XDOS2IMG = build/xdos2test.bin
XDOS2LOG = build/verify-xdos2.log
XDOSMIN	= 3
.PHONY: verify-xdos2
verify-xdos2: all $(CPMAXDOS)
	$(MKDISK) $(XDOS2IMG) $(CPMSYS) $(CPMAXDOS) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XDOS2IMG)) \
		--input="$(OSSEL)$(SESS1)XDOSD\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(XDOS2LOG))
	@$(EMUOK)
	@tr -d '\r' < $(XDOS2LOG) > build/xdos2-plain.txt
	@grep -q '^XDOSD: start' build/xdos2-plain.txt \
		|| { echo "verify-xdos2: FAIL -- XDOSD did not run at all"; exit 1; }
	@grep -q 'XDOSD: no second process' build/xdos2-plain.txt \
		&& { echo "verify-xdos2: FAIL -- function 144 refused; the reason is in the"; \
		     echo "              transcript above.  A 512 KB machine has no free page"; \
		     echo "              (src/bios/pgalloc.c); the emulator models 1 MB."; exit 1; } || true
	@# NOT anchored at the line start, and C1 already had to make the same
	@# re-reading twice (run/C1.md section 3, "verify-conc's claim 2 had to be
	@# re-read, not weakened").  The claim is THE SECOND PROCESS RAN.  Under
	@# preemption the switch happens at an arbitrary instruction, so E's first
	@# line can land in the middle of one of D's -- `XDOSD: live=XDOSE: alive'
	@# is what a tick between D's two printstr() calls looks like, and it is
	@# evidence FOR the claim, not against it.  A `^' here asserts that no
	@# switch fell in that gap, which is a scheduling coincidence and not
	@# something this target is about.
	@grep -q 'XDOSE: alive' build/xdos2-plain.txt \
		|| { echo "verify-xdos2: FAIL -- the second process never ran"; exit 1; }
	@grep -q 'FAIL' build/xdos2-plain.txt \
		&& { echo "verify-xdos2: FAIL -- a call returned the wrong thing:"; \
		     grep 'FAIL' build/xdos2-plain.txt; exit 1; } || true
	@# 141: E lines between "delay" and "delayed".  Nothing but a real
	@# sleep puts more than one there.
	@sed -n '/^XDOSD: delay$$/,/^XDOSD: delayed$$/p' build/xdos2-plain.txt \
		| grep -c '^  E ' > build/xdos2-delay.txt || true
	@test "`cat build/xdos2-delay.txt`" -ge $(XDOSMIN) \
		|| { echo "verify-xdos2: FAIL -- function 141 let the other process run"; \
		     echo "              `cat build/xdos2-delay.txt` times, not $(XDOSMIN) or more."; \
		     echo "              Either the delay returned at once (no tick: see"; \
		     echo "              src/bios/tick900.c) or it spun without yielding."; exit 1; }
	@# 132: E lines between "delayed" and "flag".
	@sed -n '/^XDOSD: delayed$$/,/^XDOSD: flag$$/p' build/xdos2-plain.txt \
		| grep -c '^  E ' > build/xdos2-flag.txt || true
	@test "`cat build/xdos2-flag.txt`" -ge $(XDOSMIN) \
		|| { echo "verify-xdos2: FAIL -- function 132 let the other process run"; \
		     echo "              `cat build/xdos2-flag.txt` times, not $(XDOSMIN) or more,"; \
		     echo "              so the flag wait did not block"; exit 1; }
	@grep -n '^XDOSE: set' build/xdos2-plain.txt > build/xdos2-set.txt
	@test "`sed -n 's/:.*//p' build/xdos2-set.txt`" -lt \
	      "`grep -n '^XDOSD: flag' build/xdos2-plain.txt | sed -n 's/:.*//p'`" \
		|| { echo "verify-xdos2: FAIL -- XDOSD woke from function 132 before XDOSE"; \
		     echo "              set the flag"; exit 1; }
	@# 137: E lines between "flag" and the message.
	@sed -n '/^XDOSD: flag$$/,/^XDOSD: msg/p' build/xdos2-plain.txt \
		| grep -c '^  E ' > build/xdos2-msg.txt || true
	@test "`cat build/xdos2-msg.txt`" -ge $(XDOSMIN) \
		|| { echo "verify-xdos2: FAIL -- function 137 let the other process run"; \
		     echo "              `cat build/xdos2-msg.txt` times, not $(XDOSMIN) or more,"; \
		     echo "              so the queue read did not block"; exit 1; }
	@grep -q '^XDOSD: msg HELLO' build/xdos2-plain.txt \
		|| { echo "verify-xdos2: FAIL -- the message did not survive the trip from"; \
		     echo "              one process's page to the other's"; exit 1; }
	@# 143: the child terminated itself and was reclaimed.
	@# THREE, not two, since C10: a session (SESSION 1, typed first since D8) runs on
	@# console 1, so a machine with one
	@# foreground program and one child has three live processes and
	@# two after the child terminates itself.  The claim -- that 143
	@# reclaimed exactly one -- is the difference between the two
	@# numbers and is unchanged.
	@grep -q '^XDOSD: live=3' build/xdos2-plain.txt \
		|| { echo "verify-xdos2: FAIL -- function 145 did not see three live processes"; exit 1; }
	@grep -q '^XDOSD: after=2' build/xdos2-plain.txt \
		|| { echo "verify-xdos2: FAIL -- after XDOSE called function 143 the process"; \
		     echo "              table still says more than one is live, so Terminate"; \
		     echo "              did not reclaim it"; exit 1; }
	@grep -q '^XDOSD: done' build/xdos2-plain.txt \
		|| { echo "verify-xdos2: FAIL -- XDOSD did not reach the end"; exit 1; }
	@echo "verify-xdos2: PASS -- 141/132/137 each blocked (`cat build/xdos2-delay.txt`,"
	@echo "              `cat build/xdos2-flag.txt`, `cat build/xdos2-msg.txt` lines from the other process"
	@echo "              while each was waiting), and 143 reclaimed the caller"

# ---- verify-con1: THE CONSOLE NUMBER SELECTS THE DEVICE (C3 steps 1-2) ----
# The only target in this file that drives TWO consoles, and the only one
# that cannot run under bare ./c900: console 1 is SCC channel A, which the
# emulator reaches as an AF_UNIX socket (--wire, src/wire.c), so something
# has to be listening on the far end.  tests/wirecon.py is that something --
# it listens, starts the emulator, collects console 1's output and types a
# character back once a given text appears there.  Console 0 is captured the
# ordinary way, from the emulator's stdout.
#
# WHAT IS ASSERTED IS A PROPERTY, NOT AN INTERLEAVING: each of the six lines
# CON1.Z8K prints must appear in ONE transcript and be absent from the other,
# and the character read on console 1 must be the one typed into the socket.
# Nothing routes between the two files, so a line in the wrong one is a
# driver fault.  No line number, count or ordering is involved.
#
# C10 ADDED A STEP AND DID NOT CHANGE THE CLAIM.  Console 1 now has an
# OWNER -- the session SESSION 1 starts there, typed first -- and CON1's function
# 148 no longer buys it the right to read: moving is not reading.  So CON1
# says 146 and waits, and this script types `CATT D' at console 1 to make
# that session let go before the `Z' is sent.  What is asserted below is
# exactly what it was; what changed is that the character now reaches CON1
# because the rule gave it the console, rather than because nothing was
# stopping it.
CON1IMG = build/con1test.bin
CON1C0	= build/verify-con1-c0.log
CON1C1	= build/verify-con1-c1.log
.PHONY: verify-con1
verify-con1: all $(CPMAXDOS)
	$(MKDISK) $(CON1IMG) $(CPMSYS) $(CPMAXDOS) $(CPMBIMG)
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(CON1IMG) \
		--input='$(SESS1)CON1\r$(ENDIN)' --max=$(EMUMAX) --stop-on=idle \
		--stop-mark='$(ENDMARK)' \
		--send-after='CON1: on 1' --send='USER 0\r' \
		--send-after='A>' --send='CATT D\r' \
		--send-after='CATT: gave 1' --send='Z' \
		--log $(CON1C0) --wire-log $(CON1C1)
	@tr -d '\r' < $(CON1C0) > build/con1-c0.txt
	@tr -d '\r' < $(CON1C1) > build/con1-c1.txt
	@grep -q 'CON1: FAIL' build/con1-c0.txt build/con1-c1.txt \
		&& { echo "verify-con1: FAIL -- CON1 refused to start:"; \
		     grep -h 'CON1: FAIL' build/con1-c0.txt build/con1-c1.txt; \
		     exit 1; } || true
	@grep -q '^CON1: start' build/con1-c0.txt \
		|| { echo "verify-con1: FAIL -- CON1 did not run at all"; exit 1; }
	@grep -q '^CON1: back 0' build/con1-c0.txt \
		|| { echo "verify-con1: FAIL -- CON1 never came back to console 0"; \
		     exit 1; }
	@grep -q 'CON1: on 1' build/con1-c0.txt \
		&& { echo "verify-con1: FAIL -- console 1's output reached console 0,"; \
		     echo "              so the console number is not selecting a device"; \
		     exit 1; } || true
	@grep -q 'CON1: got' build/con1-c0.txt \
		&& { echo "verify-con1: FAIL -- console 1's echo reached console 0"; \
		     exit 1; } || true
	@# Not anchored since C10: console 1 now has a session on it from the
	@# SESSION 1, so its `1A>' prompt is what this line prints after.
	@grep -q 'CON1: on 1' build/con1-c1.txt \
		|| { echo "verify-con1: FAIL -- nothing printed on console 1 reached"; \
		     echo "              SCC channel A (build/con1-c1.txt is what did)"; \
		     exit 1; }
	@grep -q 'CON1: got Z' build/con1-c1.txt \
		|| { echo "verify-con1: FAIL -- the character typed at console 1 did not"; \
		     echo "              reach the program: console input is not coming"; \
		     echo "              from the channel the console number names"; \
		     exit 1; }
	@grep -q 'CON1: start' build/con1-c1.txt \
		&& { echo "verify-con1: FAIL -- console 0's output reached console 1"; \
		     exit 1; } || true
	@grep -q 'CON1: back 0' build/con1-c1.txt \
		&& { echo "verify-con1: FAIL -- console 0's output reached console 1"; \
		     exit 1; } || true
	@echo "verify-con1: PASS -- console 0 has start/back, console 1 has on/got,"
	@echo "              neither has the other's, and the character read on"
	@echo "              console 1 came off SCC channel A"

# ---- verify-sess: A SECOND USER, ON THE SECOND CONSOLE (C3 step 4) ----
# `SESSION 1' (BDOS function 142, src/bdos/proc.c psession) starts a CCP on
# console 1 in a 64 KB page of its own, logged into user area 1 because a
# session's user area is its console number.  Three claims, none of them an
# ordering or a count:
#
#   1. the prompt on console 1 is `1A>' -- there IS a command processor
#      there, and the 1 in front of it is the user area, so "console N logs
#      into user area N" is read straight off the machine;
#   2. a transient started from console 1 runs and prints THERE;
#   3. a prompt comes back on console 1 after that transient ended -- the
#      session survived its own warm boot, which is what makes it a session
#      rather than a one-command process (proc.c pd_sess);
#
# and the mirror of all three on console 0, which must see none of it.
#
# The two lines typed at console 1 are PACED, one per prompt, for the reason
# tests/wirecon.py's banner gives: a CP/M console driver polls the keyboard
# while it prints, so a line typed into another line's output is eaten there.
# That is CP/M, not this port, and the emulator's own console feeder does the
# same thing for console 0.
#
# SESSMAX is larger than $(EMUMAX) because two processes are alive for the
# whole run and one of them is BLOCKED READING -- which under C2's blocking
# primitive is a spin through pyield(), so the machine does about twice the
# instructions per second of wall progress.  It is the cost this stage has,
# stated, not a threshold anything is tuned to.
SESSIMG	= build/sesstest.bin
SESSC0	= build/verify-sess-c0.log
SESSC1	= build/verify-sess-c1.log
SESSMAX	= 900000000
.PHONY: verify-sess
verify-sess: all $(CPMAXDOS)
	$(MKDISK) $(SESSIMG) $(CPMSYS) $(CPMAXDOS) $(CPMBIMG)
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(SESSIMG) \
		--input='SESSION 1\r' --max=$(SESSMAX) --stop-on=idle \
		--send-after='1A>' --send='USER 0\r' \
		--send-after='A>'  --send='CON1 P\r' \
		--log $(SESSC0) --wire-log $(SESSC1)
	@tr -d '\r' < $(SESSC0) > build/sess-c0.txt
	@tr -d '\r' < $(SESSC1) > build/sess-c1.txt
	@grep -q 'SESSION: refused' build/sess-c0.txt \
		&& { echo "verify-sess: FAIL -- function 142 refused:"; \
		     grep 'SESSION: refused' build/sess-c0.txt; \
		     echo "              A 512 KB machine has no free page"; \
		     echo "              (src/bios/pgalloc.c); the emulator models 1 MB."; \
		     exit 1; } || true
	@grep -q 'SESSION: console 1 up' build/sess-c0.txt \
		|| { echo "verify-sess: FAIL -- SESSION did not run at all"; exit 1; }
	@grep -q '1A>' build/sess-c1.txt \
		|| { echo "verify-sess: FAIL -- no \`1A>' prompt on console 1, so either"; \
		     echo "              no CCP is running there or it is not logged into"; \
		     echo "              user area 1 (build/sess-c1.txt is what it printed)"; \
		     exit 1; }
	@grep -q 'CON1: transient' build/sess-c1.txt \
		|| { echo "verify-sess: FAIL -- a transient started from console 1 did"; \
		     echo "              not run, or did not print there"; exit 1; }
	@sed -n '/CON1: transient/,$$p' build/sess-c1.txt | grep -q 'A>' \
		|| { echo "verify-sess: FAIL -- no prompt came back on console 1 after"; \
		     echo "              its transient ended, so the session died at the"; \
		     echo "              warm boot (src/bdos/proc.c pd_sess)"; exit 1; }
	@grep -q '1A>' build/sess-c0.txt \
		&& { echo "verify-sess: FAIL -- console 1's prompt reached console 0"; \
		     exit 1; } || true
	@grep -q 'CON1: transient' build/sess-c0.txt \
		&& { echo "verify-sess: FAIL -- console 1's transient printed on console 0"; \
		     exit 1; } || true
	@grep -q 'SESSION: console 1 up' build/sess-c1.txt \
		&& { echo "verify-sess: FAIL -- console 0's output reached console 1"; \
		     exit 1; } || true
	@echo "verify-sess: PASS -- a CCP on console 1 in user area 1, a transient"
	@echo "              run from it, and a prompt back afterwards; console 0"
	@echo "              saw none of it"

# ---- verify-kbcon: TYPE-AHEAD BELONGS TO ONE CONSOLE (F10) ----
# The P2 finding on src/bdos/conbdos.c: `kbchar' was ONE byte for the whole
# machine, so a keystroke typed at one console could be handed to a process
# reading another.  It is not a narrow race.  Two halves of the BDOS make it
# a certainty:
#
#   - conbrk() (conbdos.c) polls the keyboard of the PRINTING process every
#     CONBRK_POLL output characters, and parks anything that is not
#     ^C/^S/^Q/^P.  bconstat()/bconin() pass `concur', so the character
#     comes off the printing process's console;
#   - getch() (conbdos.c) hands a parked character to its caller BEFORE it
#     touches the BIOS, so the console it was typed at never enters into it.
#
# WHICH READER TAKES IT, and this is the timing the session has to build.
# A reader already parked in getch() cannot see a character parked after
# it got there: PW_CON is a wait the SCHEDULER polls, and what it polls is
# that console's own BIOS receiver, not this buffer (src/bdos/proc.c).  So
# the steal happens to a process that ENTERS getch() after the character
# is parked -- which is every CCP, on every command, since it re-enters
# getch() for each line it reads.  The session therefore has to make ONE
# console's reader come back while the OTHER console's program is still
# printing, and that is what the two token counts below do.
#
# THE SESSION, two consoles and one keystroke:
#
#   console 1   `CONBRK P 0400' -- the LONG print.  wirecon types the
#               single byte `M' there as soon as CONBRK-START appears on
#               the wire, so it arrives while CONBRK is printing and is
#               taken by a conbrk() poll on console 1 rather than by any
#               read.  Console 1's own CCP cannot come back for it until
#               token 0400, long after console 0's reader has had its
#               chance.
#   console 0   `CONBRK P 0100' -- the SHORT print, and nothing typed at
#               all.  It ends first, so console 0's CCP re-enters getch()
#               with console 1's keystroke sitting in the buffer.  That
#               is the steal, and console 0 is where it would show.
#
# So the two transcripts say which console read a character only one of
# them was typed at:
#
#   kept       console 1's CCP echoes the M at its own prompt after
#              CONBRK-DONE (`A>M'), and console 0's prompt stays bare;
#   stolen     console 0's CCP echoes it at its own `A>' after ITS
#              CONBRK-DONE, and console 1 never sees it.
#
# Each grep looks only at what follows that transcript's own CONBRK-DONE,
# so the command echoes before it cannot be mistaken for the keystroke.
# `USER 0' first on console 1: a session logs into the user area its
# console number names (verify-sess), and CONBRK.Z8K is in user 0.
#
# Both are asserted, in the transcript each belongs to.  So is the thing
# that makes the negative one mean something: BOTH programs must print
# CONBRK-DONE, because that is what says both CCPs really did come back
# and read.  Without it a run that ended early would pass by having no
# reader at all.
KBCONIMG = build/kbcontest.bin
KBCONC0	= build/verify-kbcon-c0.log
KBCONC1	= build/verify-kbcon-c1.log
KBCONC0TOK = 0100
KBCONC1TOK = 0400
# No ENDIN and no stop mark: in the correct case nothing is ever typed at
# console 0 after its own command, so the run ends where it runs out of
# things to do.
.PHONY: verify-kbcon
verify-kbcon: all
	$(MKDISK) $(KBCONIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG)
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(KBCONIMG) \
		--input='$(SESS1)CONBRK P $(KBCONC0TOK)\r' \
		--max=$(EMUMAX) --stop-on=idle \
		--send-after='1A>' --send='USER 0\r' \
		--send-after='A>' --send='CONBRK P $(KBCONC1TOK)\r' \
		--send-after='$(CBRKMARK)' --send='M' \
		--log $(KBCONC0) --wire-log $(KBCONC1)
	@tr -d '\r' < $(KBCONC0) > build/kbcon-c0.txt
	@tr -d '\r' < $(KBCONC1) > build/kbcon-c1.txt
	@# Console 0's command line, verbatim.  This is where a shared buffer
	@# shows first and worst: the CCP calls getch() once per CHARACTER, so
	@# a byte parked by console 1 is spliced into the middle of whatever
	@# console 0 is typing -- the measured symptom is `CMONBRK P 0100'.
	@grep -q 'A>CONBRK P $(KBCONC0TOK)' build/kbcon-c0.txt \
		|| { echo "verify-kbcon: FAIL -- console 0's command line is not what was typed"; \
		     echo "               at console 0.  A character typed at console 1 was"; \
		     echo "               spliced into it (src/bdos/conbdos.c's type-ahead is one"; \
		     echo "               buffer for the machine).  The line it read was:"; \
		     grep -m1 'A>' build/kbcon-c0.txt | tail -c 40; exit 1; }
	@grep -q 'CONBRK-DONE' build/kbcon-c0.txt \
		|| { echo "verify-kbcon: FAIL -- console 0's CONBRK never finished, so its CCP"; \
		     echo "               never came back to read and nothing was tested"; exit 1; }
	@grep -q 'CONBRK-DONE' build/kbcon-c1.txt \
		|| { echo "verify-kbcon: FAIL -- console 1's CONBRK never finished, so its CCP"; \
		     echo "               never came back to read and nothing was tested"; \
		     echo "               (raise EMUMAX, or check build/kbcon-c1.txt)"; exit 1; }
	@sed -n '/CONBRK-DONE/,$$p' build/kbcon-c1.txt | grep -q 'A>M' \
		|| { echo "verify-kbcon: FAIL -- the character typed at console 1 was never read"; \
		     echo "               THERE.  Either another console's reader took it --"; \
		     echo "               src/bdos/conbdos.c's type-ahead is one buffer for the"; \
		     echo "               machine again -- or it was dropped; build/kbcon-c0.txt"; \
		     echo "               says which"; exit 1; }
	@sed -n '/CONBRK-DONE/,$$p' build/kbcon-c0.txt | grep -q 'A>M' \
		&& { echo "verify-kbcon: FAIL -- console 0's CCP read a character typed at"; \
		     echo "               console 1 (it echoed it at its own prompt)"; \
		     exit 1; } || true
	@echo "verify-kbcon: PASS -- a keystroke typed at console 1 during a process's"
	@echo "               output was read by console 1's own next reader, and the"
	@echo "               reader that came back first on console 0 did not take it"

# ---- verify-conn: HOW MANY CONSOLES IS THE LOADER'S ANSWER (C4) ----
# The console count was the literal 2, in three files.  It is now built at
# cold start from bi_serial -- the bitmap of serial channels the loader
# probed and found (src/bios/bootinfo.h v4, bios900.c coninit()).  Three
# boots of THE SAME cpm.sys, differing only in the block kboot hands over,
# and CONN.Z8K (src/tests/conn.c) reads the count off the running machine
# twice: from the BIOS (function 29) and from what XDOS function 148 will
# accept.  What is asserted is a NUMBER PRINTED BY THE MACHINE -- no line
# position, no ordering, no count of lines.
#
#   1. NO HANDOFF.  `--no-bootinfo' is the medium every existing disk is:
#      the BIOS must use its compiled map, bits 0 and 1, and come up with
#      exactly C3's two consoles.  This is the half that says the change
#      cannot break a medium that was built before it.
#   2. A HANDOFF SAYING THREE CHANNELS.  ncon must be 3.  Without this,
#      run 1 passes for a BIOS that still has 2 compiled into it.
#   3. A HANDOFF SAYING SIXTEEN.  ncon must be CONMAX (4), not 16: the
#      ceiling is a stated one and the loader does not get to exceed it.
#
# HOW HONEST THIS IS.  The v4 block is SYNTHESISED by tests/bipatch.py,
# not written by kboot, and it has to be: the kboot in this tree is v3, so
# bootinfo.h's BI_ASK still asks for 3 (raising it would stop a v3 loader
# booting the medium at all -- kboot bmain.c bifill).  bipatch.py writes
# byte for byte what a v4 kboot will write, and the BIOS cannot tell the
# difference, but what is proved here is the CP/M side of the contract:
# the table is sized from the map, and the fallback is C3 exactly.
#
# WHAT IS NOT PROVED, and cannot be here.  Console 2 has nothing to talk
# to.  The emulator models SCC channels A and B and no more, and this BIOS
# has no I/O address for the LR board's further channels (bios900.c
# sccport() returns 0 for them and says why), so the third console is a
# real table entry bound to a channel that swallows output and never has a
# character waiting.  Traffic on a third DEVICE is task E4's to make
# testable; nothing below claims it.
CONNIN	= $(OSSEL)CONN\r
CONNFBIMG = build/conn-fallback.bin
CONNFBLOG = build/verify-conn-fallback.log
CONN3SYS  = build/conn-3chan.sys
CONN3IMG  = build/conn-3chan.bin
CONN3LOG  = build/verify-conn-3chan.log
CONNMAXSYS = build/conn-16chan.sys
CONNMAXIMG = build/conn-16chan.bin
CONNMAXLOG = build/verify-conn-16chan.log
.PHONY: verify-conn
verify-conn: all $(CPMAXDOS)
	@# ---- 1: no handoff -- the medium every existing disk is ----
	$(MKDISK) --no-bootinfo $(CONNFBIMG) $(CPMSYS) $(CPMAXDOS) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONNFBIMG)) \
		--input="$(CONNIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONNFBLOG))
	@$(EMUOK)
	@grep -q 'CONN: ncon=2' $(CONNFBLOG) \
		|| { echo "verify-conn: FAIL -- with no handoff the machine does not"; \
		     echo "             have the two consoles C3 shipped, so an existing"; \
		     echo "             medium does not come up as it did"; exit 1; }
	@grep -q 'CONN: agreed' $(CONNFBLOG) \
		|| { echo "verify-conn: FAIL -- the BDOS and the BIOS disagree about how"; \
		     echo "             many consoles there are with no handoff"; exit 1; }
	@# ---- 2: a handoff saying three serial channels ----
	python3 tests/bipatch.py --in=$(CPMSYS) --out=$(CONN3SYS) --serial=0x0007 \
		--part8=$(CPMA_BASEBLK),$(CPMA_BLOCKS) \
		--part9=$(CPMB_BASEBLK),$(CPMB_BLOCKS)
	$(MKDISK) --no-bootinfo $(CONN3IMG) $(CONN3SYS) $(CPMAXDOS) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONN3IMG)) \
		--input="$(CONNIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONN3LOG))
	@$(EMUOK)
	@grep -q 'CONN: ncon=3' $(CONN3LOG) \
		|| { echo "verify-conn: FAIL -- a handoff declaring three serial channels"; \
		     echo "             did not give three consoles: the table is not sized"; \
		     echo "             from the map"; exit 1; }
	@grep -q 'CONN: agreed' $(CONN3LOG) \
		|| { echo "verify-conn: FAIL -- the BDOS still has a console count of its"; \
		     echo "             own: 148 does not accept the consoles the BIOS built"; \
		     exit 1; }
	@# ---- 3: a handoff saying sixteen -- the ceiling holds ----
	python3 tests/bipatch.py --in=$(CPMSYS) --out=$(CONNMAXSYS) --serial=0xffff \
		--part8=$(CPMA_BASEBLK),$(CPMA_BLOCKS) \
		--part9=$(CPMB_BASEBLK),$(CPMB_BLOCKS)
	$(MKDISK) --no-bootinfo $(CONNMAXIMG) $(CONNMAXSYS) $(CPMAXDOS) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONNMAXIMG)) \
		--input="$(CONNIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONNMAXLOG))
	@$(EMUOK)
	@grep -q 'CONN: ncon=4' $(CONNMAXLOG) \
		|| { echo "verify-conn: FAIL -- a loader claiming sixteen channels did not"; \
		     echo "             stop at CONMAX (4): the ceiling is not being applied"; \
		     exit 1; }
	@grep -q 'CONN: agreed' $(CONNMAXLOG) \
		|| { echo "verify-conn: FAIL -- the BDOS and the BIOS disagree at the ceiling"; \
		     exit 1; }
	@echo "verify-conn: PASS -- no handoff = 2 consoles (C3 exactly), a map of"
	@echo "             three channels = 3, a map of sixteen = CONMAX 4, and"
	@echo "             the BDOS's function 148 agrees with the BIOS every time"

# ---- verify-rxov: THE RECEIVE RING KEEPS WHAT THE POLLED DRIVER LOSES (C8) ----
# C3 built the consoles polled and stopped before the interrupt receive rings,
# for a reason that was a premise and not effort: the emulator HELD the next
# wire byte until the guest had read the previous one, so a polled read lost
# exactly as many characters as a ring did -- none, at any speed -- and no
# test could tell the two apart (C3.md 6).  `--rx-overrun' ended that: a byte
# arriving on a wired port while the previous one is unread now overwrites it
# and latches RR1 D5, as the chip does.  This target is the discriminating
# test that option made possible, and it is the ONLY one that passes it.
#
# C10 ADDED ONE STEP IN FRONT OF IT.  Console 1 has an owner now -- the
# session SESSION 1 starts there, typed first -- and both bursts are read with
# function 6, which consumes.  So RXOV asks for the console (function
# 146) and this script types `CATT D' at console 1 to make the session
# let go, before either burst is sent.  Nothing about what is compared
# changed: the same program reads the same burst twice on the same
# channel, and now nobody else is reading it at the same time.
#
# ONE BOOT, TWO DRIVERS, THE SAME INPUT.  RXOV.Z8K (src/tests/rxov.c) runs the
# same burst twice on console 1, switching the channel between the ring and
# the polled path with BIOS function 30 (CONRX) in between, and each phase
# anchors on the GUEST's clock: it prints its mark, blocks until the burst's
# first character arrives, and only then goes busy.  So what is compared is
# two drivers under one machine, not a number remembered from another run.
#
# WHAT IS ASSERTED IS A PROPERTY: the ring receives EVERY character sent, in
# order, and the polled path receives fewer than were sent.  The counts are
# read out of the transcript rather than written here, so the burst can be
# resized in one place; nothing depends on a line position, an ordering
# between the two consoles, or an instruction count.
#
# WHY THE BYTES ARE PACED (--send-delay).  A burst written to the socket in
# one call is handed to the receiver a byte every 64 emulated instructions,
# which is faster than the interrupt can be taken, serviced and dismissed --
# so an unpaced burst would measure the emulator's write granularity and not
# the driver.  2 ms a byte is a wire, and it is still far shorter than the
# program's busy period, which is what makes the polled phase lose.
RXOVIMG	= build/rxovtest.bin
RXOVC0	= build/verify-rxov-c0.log
RXOVC1	= build/verify-rxov-c1.log
RXOVBURST = ABCDEFGHIJKLMNOP
.PHONY: verify-rxov
verify-rxov: all $(CPMAXDOS)
	$(MKDISK) $(RXOVIMG) $(CPMSYS) $(CPMAXDOS) $(CPMBIMG)
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(RXOVIMG) \
		--input='$(SESS1)RXOV\r' --max=$(EMUMAX) --stop-on=idle \
		--rx-overrun --send-delay=0.002 \
		--send-after='RXOV: on 1' --send='USER 0\r' \
		--send-after='A>' --send='CATT D\r' \
		--send-after='RXOV: burst 1' --send='$(RXOVBURST)' \
		--send-after='RXOV: burst 2' --send='$(RXOVBURST)' \
		--log $(RXOVC0) --wire-log $(RXOVC1)
	@tr -d '\r' < $(RXOVC0) > build/rxov-c0.txt
	@tr -d '\r' < $(RXOVC1) > build/rxov-c1.txt
	@grep -q 'RXOV: FAIL' build/rxov-c0.txt \
		&& { echo "verify-rxov: FAIL -- RXOV said so itself:"; \
		     grep -A1 'RXOV: FAIL' build/rxov-c0.txt; \
		     exit 1; } || true
	@grep -q '^RXOV: start' build/rxov-c0.txt \
		|| { echo "verify-rxov: FAIL -- RXOV did not run at all"; exit 1; }
	@grep -q '^RXOV: burst 1' build/rxov-c1.txt \
		|| { echo "verify-rxov: FAIL -- nothing RXOV printed on console 1"; \
		     echo "             reached SCC channel A, so no burst was ever"; \
		     echo "             typed at it"; exit 1; }
	@sent=`sed -n 's/^RXOV: sent=\([0-9][0-9]*\)$$/\1/p' build/rxov-c0.txt`; \
	 ring=`sed -n 's/^RXOV: ring=\([0-9][0-9]*\)$$/\1/p' build/rxov-c0.txt`; \
	 poll=`sed -n 's/^RXOV: polled=\([0-9][0-9]*\)$$/\1/p' build/rxov-c0.txt`; \
	 test -n "$$sent" -a -n "$$ring" -a -n "$$poll" \
		|| { echo "verify-rxov: FAIL -- RXOV did not report all three counts"; \
		     cat build/rxov-c0.txt; exit 1; }; \
	 test "$$ring" -eq "$$sent" \
		|| { echo "verify-rxov: FAIL -- the ring received $$ring of $$sent"; \
		     echo "             characters: an interrupt-driven receive that"; \
		     echo "             loses characters is not one"; exit 1; }; \
	 test "$$poll" -lt "$$sent" \
		|| { echo "verify-rxov: FAIL -- the polled path received $$poll of"; \
		     echo "             $$sent too, so this run distinguishes nothing."; \
		     echo "             Either --rx-overrun did not reach the emulator"; \
		     echo "             or the busy period no longer covers the burst"; \
		     exit 1; }; \
	 echo "verify-rxov: ring $$ring/$$sent, polled $$poll/$$sent"
	@grep -q 'RXOV: ring kept all, polled did not' build/rxov-c0.txt \
		|| { echo "verify-rxov: FAIL -- RXOV did not reach its own verdict"; \
		     exit 1; }
	@echo "verify-rxov: PASS -- with the receiver overrunning, the interrupt"
	@echo "             ring kept every character of the burst, in order, and"
	@echo "             the polled driver on the same channel did not"

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
# BLOCKS.  XDOSPOL.Z8K (src/tests/xdospol.c) creates CONCY.Z8K -- the same
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
		--input="$(OSSEL)$(SESS1)XDOSPOL\r" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(XPOLLALOG))
	@$(EMUOK)
	@# ---- phase 2: the poller parked in fn 131, wire silent throughout ----
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(XDOSPOLLDISK) \
		--input='$(OSSEL)$(SESS1)XDOSPOL P\r' --max=$(EMUMAX) --stop-on=idle \
		--send-after='XDOSPOL: asking' --send='USER 0\r' \
		--send-after='A>' --send='CATT D\r' \
		--log $(XPOLLPLOG) --wire-log $(XPOLLPWLOG)
	@# ---- phase 3: the same call, woken by a byte sent only after the
	@#      wire shows the process was already waiting for it ----
	@# THE SECOND `K' IS A RETRY, AND IT IS WHY THIS TARGET IS NO LONGER
	@# FLAKY (run/S4.md).  wirecon.py's own header says it: a CP/M console
	@# driver polls the keyboard while it is PRINTING (conbdos.c conbrk),
	@# and a byte that lands in that poll is eaten rather than queued.
	@# After `CATT D' hands console 1 over, the session that gave it up
	@# still has one thing left to print -- its own `A>' prompt -- and
	@# whether the first `K' lands before, during or after that print is a
	@# HOST-scheduling question: wirecon sees `XDOSPOL: waiting' and writes
	@# the byte while the guest runs on.  On a quiet machine it lands
	@# clear (10 runs, 10 passes); on a loaded one it lands inside the
	@# print and is eaten (3 of 4 runs under a 16-way load), and the run
	@# then coasts to idle with the poller still parked -- the "fn 131
	@# returned but did not read the byte" failure.
	@# The second `K' is sent after that prompt is on the wire, so the
	@# console is quiet when it arrives.  It CANNOT make this target fail
	@# in a way it would not have failed already: the first byte is still
	@# sent at exactly the same point, so if the first one wakes the
	@# poller the second lands on a console nobody is reading and nothing
	@# asserted below can see it; and if `--send-after' never matches, the
	@# retry is simply never typed.  What is proved is unchanged -- both
	@# bytes go out only AFTER the wire has shown `XDOSPOL: waiting', so
	@# neither could have been sitting there when fn 131 was called.
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(XDOSPOLLDISK) \
		--input='$(OSSEL)$(SESS1)XDOSPOL P\r' --max=$(EMUMAX) --stop-on=idle \
		--send-after='XDOSPOL: asking' --send='USER 0\r' \
		--send-after='A>' --send='CATT D\r' \
		--send-after='XDOSPOL: waiting' --send='K' \
		--send-after='A>' --send='K' \
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
	@# THREE since C10, in both runs, for the SESSION 1 session on
	@# console 1 (typed first since D8).  The claim is that the two
	@# runs see the SAME number -- fn 131 adds nobody -- and they do.
	@grep -q '^XDOSPOL: live=3' build/xdospoll-alone.txt \
		|| { echo "verify-xdospoll5: FAIL -- the ALONE run did not see exactly three live"; \
		     echo "             processes; it is not the baseline it claims to be."; exit 1; }
	@grep -q '^XDOSPOL: live=3' build/xdospoll-poll-c0.txt \
		|| { echo "verify-xdospoll5: FAIL -- the POLL run did not see exactly three live"; \
		     echo "             processes either -- fn 131 does not add a fourth."; exit 1; }
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
# ---- verify-all: every verify-* target above, VERIFYJOBS at a time ----
# Each target still runs in its own $(MAKE) and its own emulator, so a
# failure ends that target only; the rest still run, and the summary names
# every verdict.  Exit status is non-zero iff any target failed.  The list
# is read from this file at run time -- a target added above is picked up
# without registering it here -- and the pattern keeps hyphens
# (verify-hash-ab, verify-rtc-host).
# verify-zcc is skipped by name: it rebuilds src/app on the machine with
# DRI's ZCC, which is twenty minutes on its own.  `make verify-zcc' runs it.
# A test target, reached from nothing: `all' never runs an emulator.
#
# tests/verifyrun.sh is what makes running several at once safe; its header
# says how.  VERIFYJOBS=1 is the old one-after-another suite.
VERIFYALLLOG = build/verify-all.log
VERIFYJOBS ?= 4
.PHONY: verify-all
verify-all:
	@mkdir -p build
	@sh tests/verifyrun.sh $(VERIFYJOBS) '$(MAKE)' $(VERIFYALLLOG)

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
#     src/tests/paget.c at run time so that changing either one changes what
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
	@nl=`sed -n 's/^#define[ 	]*NLINES[ 	][ 	]*\([0-9][0-9]*\).*/\1/p' src/tests/paget.c`; \
	 pg=`sed -n 's/^#define[ 	]*PAGE[ 	][ 	]*\([0-9][0-9]*\).*/\1/p' src/tests/paget.c`; \
	 test -n "$$nl" -a -n "$$pg" \
		|| { echo "verify-page: FAIL -- NLINES/PAGE could not be read out of src/tests/paget.c, so this target does not know what to expect"; exit 1; }; \
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
# ---- IS A SYSTEM-MODE PROGRAM PREEMPTED? (verify-conc6) ----
# THE HOLE C1 LEFT AND NAMED.  src/bios/trap.s ttick_ took the switch only
# when the interrupted FCW's S/N bit was CLEAR, so a program that reached
# System mode through BDOS function 62 (src/bdos/bdosglue.s setsup) OWNED
# THE MACHINE for as long as it stayed there -- run/C1.md 6.1 says so in
# those words, and calls the fix "the running process's supervisor SP
# against its stack top".  That is the arm this target measures.
#
# THE METHOD IS verify-conc5's, because the question is the same shape:
# run the identical program twice on the identical image and print both
# numbers.  CONCS times a counted loop executed in System mode --
#
#   CONCS      with the machine to itself
#   CONCS C    with CONCY.Z8K (function 144) runnable for the whole of it
#
# -- and the two counts are a FACTOR, not a percentage.  If System mode is
# preempted the two processes halve the machine and the second count is
# about twice the first.  If it is not, the second count IS the first: a
# System-mode loop that makes no BDOS call gives the machine away to
# nobody, so the child cannot run at all until CONCS comes back out.
#
# MEASURED ON BOTH BUILDS, which is what makes CONCSMIN a discriminator
# rather than a calibration.  Same tree, same image, only ttick_'s new arm
# taken out (the S/N test left to decline every System-mode frame, as C1
# shipped it):
#
#           alone   with a second job   ratio
#   C1        210          210           1.00
#   C7        210          432           2.06
#
# CONCSMIN is 150 percent: far above the 100 the old scheduler produces
# and far below the 206 the new one does.
#
# THE OTHER HALF OF THE CLAIM, and without it this target would PASS for
# the wrong reason: a NORMAL-mode loop is preempted too, so a run in which
# function 62 quietly did nothing would show the same factor of two and
# prove nothing.  So sysmode.s reads the FCW it is actually running at
# with a System-mode-only LDCTL -- in Normal mode that is a privilege trap
# and the program dies instead of measuring -- and CONCS prints it.  D0xx
# is segmented, System and VIE: the three bits the claim is about.  The
# process counts (function 145) are checked for verify-conc5's reason,
# and the child's `done' line is required to come out AFTER the measured
# loop, because a child that had already finished would have left the tail
# of the loop running alone.
CONCSMIN = 150
CONCSAIMG = build/conc6a.bin
CONCSBIMG = build/conc6b.bin
CONCSALOG = build/verify-conc6-alone.log
CONCSBLOG = build/verify-conc6-both.log
.PHONY: verify-conc6
verify-conc6: all $(CPMACONCS)
	$(MKDISK) $(CONCSAIMG) $(CPMSYS) $(CPMACONCS) $(CPMBIMG)
	cp $(CONCSAIMG) $(CONCSBIMG)
	@# The ALONE leg only: CONCS runs by itself, so when the prompt comes
	@# back nothing is left running and $(ENDIN) ends the run there.  The
	@# `C' leg below must NOT have it -- CONCY prints its tick count AFTER
	@# that prompt, and that count is what this target compares.
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCSAIMG)) \
		--input="$(OSSEL)$(SESS1)CONCS\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCSALOG))
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCSBIMG)) \
		--input="$(OSSEL)$(SESS1)CONCS C\r" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCSBLOG))
	@$(EMUOK)
	@tr -d '\r' < $(CONCSALOG) > build/conc6a.txt
	@tr -d '\r' < $(CONCSBLOG) > build/conc6b.txt
	@grep -q 'CONCS: no second process' build/conc6b.txt \
		&& { echo "verify-conc6: FAIL -- function 144 refused; a 512 KB machine has no"; \
		     echo "              free page (src/bios/pgalloc.c).  The emulator is 1 MB."; \
		     exit 1; } || true
	@# It really was in System mode, in both runs.
	@grep -q 'CONCS: sysfcw=D0' build/conc6a.txt \
		|| { echo "verify-conc6: FAIL -- the ALONE run's loop did not run at FCW D0xx,"; \
		     echo "              so it was not in segmented System mode with VIE set and"; \
		     echo "              there is nothing here about System-mode preemption."; \
		     grep 'CONCS: sysfcw' build/conc6a.txt; exit 1; }
	@grep -q 'CONCS: sysfcw=D0' build/conc6b.txt \
		|| { echo "verify-conc6: FAIL -- the SECOND-JOB run's loop did not run at FCW"; \
		     echo "              D0xx: not segmented System mode with VIE set."; \
		     grep 'CONCS: sysfcw' build/conc6b.txt; exit 1; }
	@# ONE MORE IN BOTH RUNS SINCE C10: the session SESSION 1 starts on
	@# console 1 (typed first since D8) is live for the whole of
	@# each run, so the two numbers are 2 and 3 rather than 1 and 2.
	@# The claim is the difference -- the second job -- unchanged.
	@grep -q 'CONCS: live=2' build/conc6a.txt \
		|| { echo "verify-conc6: FAIL -- the ALONE run did not see exactly two live"; \
		     echo "              processes, so it is not the baseline it claims to be."; \
		     grep 'CONCS: live' build/conc6a.txt; exit 1; }
	@grep -q 'CONCS: live=3' build/conc6b.txt \
		|| { echo "verify-conc6: FAIL -- the SECOND-JOB run did not see three live"; \
		     echo "              processes.  There was nothing to be preempted FOR."; \
		     grep 'CONCS: live' build/conc6b.txt; exit 1; }
	@# The child outlived the measured loop.
	@sed -n -e 's/.*CONCS: S done.*/S/p' -e 's/.*CONCY: Y done.*/Y/p' \
		build/conc6b.txt | tr -d '\n' > build/conc6-order.txt
	@test "`cat build/conc6-order.txt`" = "SY" \
		|| { echo "verify-conc6: FAIL -- the child did not outlive the measured loop"; \
		     echo "              (order was `cat build/conc6-order.txt`), so part of that"; \
		     echo "              loop ran with the machine to itself after all and the"; \
		     echo "              factor below is not a factor of anything."; exit 1; }
	@sed -n 's/^CONCS: S done, ticks=\([0-9][0-9]*\)$$/\1/p' build/conc6a.txt \
		> build/conc6-sa.txt
	@sed -n 's/^CONCS: S done, ticks=\([0-9][0-9]*\)$$/\1/p' build/conc6b.txt \
		> build/conc6-sb.txt
	@for f in sa sb; do test -s build/conc6-$$f.txt \
		|| { echo "verify-conc6: FAIL -- a run never printed its tick count ($$f)."; \
		     echo "              The transcripts are above."; exit 1; }; done
	@awk -v mn=$(CONCSMIN) \
	     -v sa=`cat build/conc6-sa.txt` -v sb=`cat build/conc6-sb.txt` \
	  'BEGIN { \
	     printf "verify-conc6: a System-mode loop, timed twice\n"; \
	     printf "  %-26s %8s %8s %9s\n", "loop", "alone", "+job", "ratio"; \
	     r = sb * 100.0 / sa; \
	     printf "  %-26s %8d %8d %8.2f\n", "CONCS in System mode", sa, sb, r/100.0; \
	     printf "  processes live             %8d %8d\n", 1, 2; \
	     if (r >= mn) { \
	       printf "verify-conc6: PASS -- the second job took a share of a loop that made\n"; \
	       printf "              no BDOS call and ran in System mode: the tick preempted it\n"; \
	       exit 0; } \
	     printf "verify-conc6: FAIL -- the System-mode loop took the same time with a\n"; \
	     printf "              second runnable process as it did alone (ratio %.2f, needs\n", r/100.0; \
	     printf "              %.2f).  Nothing took the machine away from it, which is\n", mn/100.0; \
	     printf "              src/bios/trap.s ttick_ declining every System-mode frame.\n"; \
	     exit 1; }'

# ---- HOW LONG IS A SLICE?  THE QUANTUM, COUNTED (verify-conc7, C7) ----
# C1 shipped a tick that dispatched on EVERY tick, which is the degenerate
# time slice of one, and left `pd_prio' written by two places and read by
# none.  C7's second half gives the descriptor a slice length
# (src/bdos/proc.h PQBASE, pd_quant, proc.c pqfor()) and makes
# src/bios/trap.s ttick_ spend it.  THIS TARGET PRINTS THAT NUMBER.
#
# THE MEASUREMENT IS THE CLOCK AND NOTHING ELSE.  CONCV.Z8K creates
# CONCY.Z8K (16 units of pure computation, no BDOS call anywhere in it, so
# it is runnable for the whole measurement) and then reads the tick counter
# through the RAW SC #3 BIOS GATE in a tight loop.  SC #3 returns through
# `scret' and never dispatches (src/bdos/bdosglue.s), so reading the clock
# does not give the machine away -- if it went through the BDOS the run
# lengths below would measure CONCV's own call rate instead of the
# scheduler.  Consecutive reads differing by 1 are the same slice; a jump is
# the far side of a gap.  So the run lengths ARE the quantum, in ticks.
#
# WHAT IS ASSERTED: the MODE of the run lengths equals PQBASE, read out of
# src/bdos/proc.h at run time rather than duplicated here, and it must be
# the mode of a clear majority of the slices.  There is no threshold and no
# tolerance in that -- it is an integer against an integer.
#
# THE CONTROL, which is what makes this a measurement of the quantum rather
# than of the fact that two processes exist (run/C7q.md has both lists):
#
#   PQBASE   runs= (20 slices)                        mode  loop ticks
#   5        5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5 5    5      210
#   3        3 3 3 3 3 3 3 3 3 4 3 3 3 3 3 4 3 3 3 3    3      128
#   1        1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1    1       42
#
# Same tree, same image, only PQBASE changed and the tree rebuilt.  The
# number this target prints IS the constant in the header, three times over.
#
# The gaps are printed beside the runs as corroboration: a gap is PQBASE+1,
# because the tick that ends a slice is spent in the handler that switches
# and the outgoing process never gets to read it.  Measured gaps were 6, 4
# and 2 for the three rows above.
#
# THE TWO 4s IN THE MIDDLE ROW ARE NOT NOISE IN THE SCHEDULER, and the
# majority test below is what they are there for.  ttick_ spends a tick of
# the slice whether or not it can dispatch it, and it cannot dispatch one
# that landed while CONCV was inside the SC #3 gate (crt.s psa+24: the SC
# trap FCW is 0xD000, VIE still set, so the tick runs, is declined because
# the supervisor stack is not empty, and is dismissed).  A swallowed tick
# lengthens that one slice by one.  src/tests/concv.c's SPIN banner has the
# measurement that found this, and it is a fact about the port: a loop that
# is nothing but `sc 3' is very nearly non-preemptible.
CONCVQ  = $(shell sed -n 's/^#define[^A-Za-z]*PQBASE[^0-9]*\([0-9][0-9]*\).*/\1/p' src/bdos/proc.h)
CONCVMIN = 8
CONCVIMG = build/conc7.bin
CONCVLOG = build/verify-conc7.log
.PHONY: verify-conc7
verify-conc7: all $(CPMACONCV)
	$(MKDISK) $(CONCVIMG) $(CPMSYS) $(CPMACONCV) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCVIMG)) \
		--input="$(OSSEL)$(SESS1)CONCV\r" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCVLOG))
	@$(EMUOK)
	@tr -d '\r' < $(CONCVLOG) > build/conc7.txt
	@grep -q 'CONCV: no second process' build/conc7.txt \
		&& { echo "verify-conc7: FAIL -- function 144 refused; a 512 KB machine has no"; \
		     echo "              free page (src/bios/pgalloc.c).  The emulator is 1 MB."; \
		     exit 1; } || true
	@# THREE since C10: the SESSION 1 session on console 1 is live too
	@# (typed first since D8).  It is blocked on its own console for
	@# the whole run, so it is out of the rotation (C5) and the slices
	@# counted below are still the two jobs alternating.
	@grep -q 'CONCV: live=3' build/conc7.txt \
		|| { echo "verify-conc7: FAIL -- CONCV did not see three live processes, so"; \
		     echo "              nothing was ever going to take the machine away from"; \
		     echo "              it and there are no slices to measure."; \
		     grep 'CONCV: live' build/conc7.txt; exit 1; }
	@sed -n 's/^CONCV: runs=//p' build/conc7.txt | tr ' ' '\n' > build/conc7-runs.txt
	@sed -n 's/^CONCV: gaps=//p' build/conc7.txt | tr ' ' '\n' > build/conc7-gaps.txt
	@test -s build/conc7-runs.txt \
		|| { echo "verify-conc7: FAIL -- CONCV never printed a run list.  The"; \
		     echo "              transcript is above."; exit 1; }
	@echo "$(CONCVQ)" > build/conc7-q.txt
	@test -s build/conc7-q.txt && test "$(CONCVQ)" != "" \
		|| { echo "verify-conc7: FAIL -- PQBASE could not be read out of"; \
		     echo "              src/bdos/proc.h, so there is nothing to check against."; \
		     exit 1; }
	@awk -v q=$(CONCVQ) -v mn=$(CONCVMIN) \
	  'NF == 0 { next } \
	   FILENAME ~ /runs/ { r[++nr] = $$1; h[$$1]++; next } \
	   { g[++ng] = $$1 } \
	   END { \
	     printf "verify-conc7: how many ticks a job runs before the switch\n"; \
	     printf "  PQBASE (src/bdos/proc.h)   %8d\n", q; \
	     printf "  slices measured            %8d\n", nr; \
	     printf "  %-18s %8s %8s\n", "run length", "slices", "share"; \
	     for (k in h) if (h[k] > best) { best = h[k]; mode = k } \
	     for (k = 0; k <= 64; k++) if (h[k]) \
	       printf "  %-18d %8d %7.1f%%\n", k, h[k], h[k] * 100.0 / nr; \
	     gs = 0; for (i = 1; i <= ng; i++) gs += g[i]; \
	     if (ng) printf "  mean gap                   %8.2f  (a slice plus the tick that ends it)\n", gs / ng; \
	     if (nr < mn) { \
	       printf "verify-conc7: FAIL -- only %d slices came back, fewer than %d.  The\n", nr, mn; \
	       printf "              child probably died before the measurement finished.\n"; \
	       exit 1; } \
	     if (mode + 0 != q) { \
	       printf "verify-conc7: FAIL -- a job runs %d ticks before the switch, not the\n", mode; \
	       printf "              %d PQBASE asks for.  src/bios/trap.s ttick_ is not\n", q; \
	       printf "              spending the slice proc.c hands it.\n"; \
	       exit 1; } \
	     if (best * 2 <= nr) { \
	       printf "verify-conc7: FAIL -- %d is the commonest run length but only %d of\n", mode, best; \
	       printf "              %d slices, so there is no quantum here, just a spread.\n", nr; \
	       exit 1; } \
	     printf "verify-conc7: PASS -- a job runs %d ticks before the tick hands the\n", mode; \
	     printf "              machine on, in %d of %d slices, and %d is PQBASE.\n", best, nr, q; \
	     exit 0 }' build/conc7-runs.txt build/conc7-gaps.txt

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
# One medium per target: verify-get and verify-put rebuild it and the guest
# writes to it, and under verify-all they run at the same time.
GPIMG	= build/gptest-$(notdir $@).bin
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
# --- 4: PUT.RSX is not there, so nothing can be captured.  The point of
# this run is what is left ON THE DISK afterwards, not what was printed:
# PUT used to create the requested file BEFORE it went looking for its
# module, so a missing PUT.RSX left a zero-length file of the user's own
# name -- and PUT's own "already exists; erase it first" test then refused
# the corrected retry, which is why the command is issued TWICE here.  The
# second run must be refused for the same reason as the first and not for
# the file the first one left.  PSTUB.TXT must not exist at the end, and
# that is checked by pulling the partition apart, not by reading DIR.
PUTIN4	= $(OSSEL)ERA PUT.RSX\rPUT FILE PSTUB.TXT\rPUT FILE PSTUB.TXT\rDIR PSTUB.TXT\r
PUTMARK	= [NO ECHO]
GPDIR	= build/gp

.PHONY: verify-put
verify-put: all $(CPMAGP)
	for n in 1 2 3 4; do \
		$(MKDISK) $(GPIMG) $(CPMSYS) $(CPMAGP); \
		case $$n in \
		1) in='$(PUTIN1)';; 2) in='$(PUTIN2)';; 3) in='$(PUTIN3)';; \
		4) in='$(PUTIN4)';; esac; \
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
#	--- 4: a refusal must leave no file behind.  GPIMG still holds run
#	4's disk, so the answer is on the medium.
	@test "`grep -c 'cannot find PUT.RSX' $(GPLOG)-put4.log`" = 2 \
		|| { echo "verify-put: FAIL -- with PUT.RSX erased, BOTH attempts must be refused for the missing module; one of them was not"; exit 1; }
	@test "`grep -c 'already exists; erase it first' $(GPLOG)-put4.log`" = 0 \
		|| { echo "verify-put: FAIL -- the second PUT was refused because of a file the FIRST one left behind: the empty output file is still being created before the module is validated"; exit 1; }
	rm -rf $(GPDIR); mkdir -p $(GPDIR)
	dd if=$(GPIMG) of=$(GPDIR)/cpma.img bs=512 skip=$(CPMA_START) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(GPDIR)/cpma.img $(GPDIR)
	@test ! -e $(GPDIR)/PSTUB.TXT \
		|| { echo "verify-put: FAIL -- PSTUB.TXT is ON THE DISK after two"; \
		     echo "                refused commands.  PUT created the requested"; \
		     echo "                output before it knew it could write to it,"; \
		     echo "                and the empty file it left is what blocks the"; \
		     echo "                corrected retry (src/cmd/put.c)"; exit 1; }
	@test -s $(GPDIR)/PMARKER.TXT \
		|| { echo "verify-put: FAIL -- the fixture file went missing, so the check above proved nothing about PSTUB.TXT"; exit 1; }
	@echo "verify-put: PASS -- functions 2, 9 and 111 copied into a file, the module seen in the chain, [ECHO] and [NO ECHO] differing in what the console saw and not in what the file got, the last partial record flushed and the file closed by PUT CONSOLE, six refusals by name, and a missing PUT.RSX leaving no file on the disk to block the retry"

# ---- the chain over a NON-SEGMENTED program (src/bdos/bdosglue.s) ----
# Every target above drives the chain with programs this tree built, and
# lout2cpm wraps all of those as 0xEE01 segmented images (Makefile).  So
# until now nothing asked the question this target asks: does an RSX
# reach a STOCK Commodore CP/M-8000 binary?  It did not.  The gate tested
# FCW bit 15 -- the SEGMENTED bit -- and declined every non-segmented
# caller, which is two containers and not one: 0xEE0B (split I/D), where
# the refusal is right, and 0xEE03 (non-segmented, combined I/D), where
# nothing justified it.  bdosglue.s rsxenter now asks the loader instead
# (`spflag'), and rsxgon/rsxback carry a segmented module across a
# non-segmented caller's mode.  DEVIATIONS.md #7 is the entry.
#
# Four legs, and the reason there are four is that "intercepted" has two
# directions and the refusal has to survive both.  Leg 1 is the direction
# that SUPPLIES a call's answer, and it is the one that needs a stock
# 0xEE03 binary -- the SDB that ZCC and LD8K build on the machine -- so it
# lives in verify-zcc, which is where that ten-minute build lives.  The
# three legs here need no ZCC build and run in a few minutes:
#
#   1  (verify-zcc) GET FILE NCMDS.TXT feeds SDB.Z8K, and every line of
#      its session -- its own prompt, a deliberate syntax error, its
#      `exit' -- is read out of the file through functions 1 and 10.
#   2  DDT.Z8K with nothing resident: the control for leg 3, and the
#      reason it is needed is that leg 3 asserts a MUTATION of DDT's
#      output, so the unmutated form has to be on the record.
#   3  UCASEH.RSX resident, then DDT.Z8K: its banner comes out in upper
#      case.  DDT is the binary the deviation named, and it is the
#      awkward one.  Its four segments total 61,188 bytes (0xEF04,
#      decoded from build/diska/DDT.Z8K), and an 0xEE03 image is given
#      0x10000 - rsxres() - BPLEN - DEFSTACK: 0xFE00 with nothing
#      resident, 0xF500 under UCASEH at 0xF700, but only 0xEE00 under
#      UCASE at 0xF000 and less again under GET at 0xE400.  So it loads
#      under UCASEH and under neither of the others, which is what
#      src/tests/ucrsxh.s at 0xF700 is for.  The unfenced figure was 0xF7F8
#      while the CCP's state page held the top of the TPA and is 0xFDF8
#      now that it does not; DDT fits under it either way, so what this
#      leg exercises is unchanged.  DDT also never issues a
#      console-INPUT call on this port (it prints two lines and stops
#      without a prompt; a GET.RSX relinked high enough for it to load
#      under was never asked for a byte), so upper-casing its output is
#      the whole of what can be shown with DDT, and it is enough: the
#      call reached the module.
#   4  PUT FILE DOUT.TXT captures a session that runs DUMP.Z8K (0xEE03)
#      and SIZEZ8K.Z8K (0xEE0B), then TYPEs the capture back.  DUMP's
#      output is in the file; SIZEZ8K's is not, because the gate still
#      sends a split-I/D caller straight to the BDOS.  So the marker
#      strings are COUNTED: twice for the 0xEE03 program (live, then
#      read back) and once for the split one.  That is the deviation
#      being CONFIRMED rather than merely left alone.
#
# Legs 2 and 3 cannot end on their own -- DDT never returns to the CCP --
# so they are bounded by their own budget rather than by $(ENDMARK).  It
# is small because the whole of what they have to reach is a banner.
RSXNIMG	= build/rsxntest.bin
RSXNLOG	= build/verify-rsxn
RSXNMAX	= 250000000
RSXNIN1	= $(OSSEL)GET FILE NCMDS.TXT\r
RSXNIN2	= $(OSSEL)DDT MHELLO.Z8K\r
RSXNIN3	= $(OSSEL)RSXLDR UCASEH.RSX\rDDT MHELLO.Z8K\r
RSXNIN4	= $(OSSEL)PUT FILE DOUT.TXT\rDUMP NMARKER.TXT\rSIZEZ8K MHELLO.Z8K\rPUT CONSOLE\rTYPE DOUT.TXT\r$(ENDIN)

.PHONY: verify-rsxn
verify-rsxn: all $(CPMAGP)
	$(MKDISK) $(RSXNIMG) $(CPMSYS) $(CPMAGP)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RSXNIMG)) \
		--input="$(RSXNIN2)" --max=$(RSXNMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(RSXNLOG))-2.log
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RSXNIMG)) \
		--input="$(RSXNIN3)" --max=$(RSXNMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(RSXNLOG))-3.log
	@$(EMUOK)
	$(MKDISK) $(RSXNIMG) $(CPMSYS) $(CPMAGP)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(RSXNIMG)) \
		--input="$(RSXNIN4)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(RSXNLOG))-4.log
	@$(EMUOK)
#	--- 2/3: a stock binary's output, unmutated and mutated
	@grep -q 'Zilog portable debugger' $(RSXNLOG)-2.log \
		|| { echo "verify-rsxn: FAIL -- DDT did not run with nothing resident: the control leg proves nothing"; exit 1; }
	@grep -q 'RSXLDR: ATTACHED AT F700 RESIDENT' $(RSXNLOG)-3.log \
		|| { echo "verify-rsxn: FAIL -- UCASEH.RSX did not attach"; exit 1; }
	@grep -q 'ZILOG PORTABLE DEBUGGER' $(RSXNLOG)-3.log \
		|| { echo "verify-rsxn: FAIL -- DDT.Z8K's console output did not go through the chain (or DDT did not load under the module)"; exit 1; }
	@test "`grep -c 'Zilog portable debugger' $(RSXNLOG)-3.log`" = 0 \
		|| { echo "verify-rsxn: FAIL -- DDT's banner came out unmutated as well: something is printing round the module"; exit 1; }
#	--- 4: an 0xEE03 program captured, a split-I/D one still not
	@test "`grep -c 'GET-DROVE-A-STOC' $(RSXNLOG)-4.log`" = 2 \
		|| { echo "verify-rsxn: FAIL -- DUMP.Z8K's output was not captured into the file and read back"; exit 1; }
	@test "`grep -c 'Segmented Program' $(RSXNLOG)-4.log`" = 1 \
		|| { echo "verify-rsxn: FAIL -- SIZEZ8K.Z8K is split I/D and its output must NOT reach the chain; DEVIATIONS.md #7 says so and this counted it twice"; exit 1; }
	@echo "verify-rsxn: PASS -- a stock 0xEE03 binary's output mutated by a"
	@echo "             module, DDT.Z8K's banner among it, another one's"
	@echo "             output captured to a file, and a split-I/D caller"
	@echo "             still going straight to the BDOS"

# ---- verify-ddtseg: no program may write over a supervisor stack ----
# Segment 0x3F holds EVERY process's supervisor stack (proc.h PSTKOF: six
# stacks from 0xFC00 down to 0x3C00).  Nothing may ever write BELOW the
# lowest of them, so the headroom at 0x0000 and 0x2000 is a tripwire: on a
# healthy machine it reads as zeros for the whole run.
#
# DDT.Z8K is the program that proved this can be violated.  It is a
# NON-SEGMENTED caller of the SC #1 memory gate, and it passes that gate a
# zero-extended 16-bit context pointer.  The gate used to take the zero
# high word literally, naming segment 0 (ROM, which reads 0xFFFF), so
# xfer_ launched a context of 0xFFFF words -- and, because that rubbish
# FCW had the System bit set, launched it in SYSTEM mode, where segment
# 0x3F is mapped.  The machine then ran away writing 0xFFFF over the
# supervisor stacks of processes that had nothing to do with DDT.
#
# Before the bdosglue.s/glue.s fix this target FAILS on the default build:
# the two windows come back full of 0xFFFF.  It is not a DDT test -- DDT is
# merely the caller that gets there -- it is the rule that one program's
# mistake cannot reach another process's stack.
DDTSEGIMG = build/ddtsegtest.bin
DDTSEGLOG = build/verify-ddtseg.log
DDTSEGMAX = 60000000
DDTSEGIN  = $(OSSEL)DDT MHELLO.Z8K\r
.PHONY: verify-ddtseg
verify-ddtseg: all $(CPMAGP)
	$(MKDISK) $(DDTSEGIMG) $(CPMSYS) $(CPMAGP)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DDTSEGIMG)) \
		--input="$(DDTSEGIN)" --max=$(DDTSEGMAX) \
		--dump=3F:0000+32 --dump=3F:2000+32 2>&1; $(EMUSTAT); } \
		| tee $(abspath $(DDTSEGLOG))
	@$(EMUOK)
	@grep -q 'Zilog portable debugger' $(DDTSEGLOG) \
		|| { echo "verify-ddtseg: FAIL -- DDT.Z8K did not run, so the gate it exercises was never reached"; exit 1; }
	@grep -qE 'spec=3F:0000\+32 .*data=0{64} fault=0' $(DDTSEGLOG) \
		|| { echo "verify-ddtseg: FAIL -- segment 0x3F:0000 was written."; \
		     echo "               That is BELOW the lowest supervisor stack, so a"; \
		     echo "               process has written over another process's stack."; \
		     echo "               Suspect the SC #1 gate (src/bdos/bdosglue.s memgate)"; \
		     echo "               taking a non-segmented pointer literally, and xfer_"; \
		     echo "               (src/bios/glue.s) launching it in System mode."; exit 1; }
	@grep -qE 'spec=3F:2000\+32 .*data=0{64} fault=0' $(DDTSEGLOG) \
		|| { echo "verify-ddtseg: FAIL -- segment 0x3F:2000 was written: see above"; exit 1; }
	@echo "verify-ddtseg: PASS -- DDT ran and the supervisor-stack headroom in"
	@echo "               segment 0x3F is untouched: no program wrote over"
	@echo "               another process's supervisor stack"

# ---- verify-sstk: SC #1 refuses a user copy into the supervisor stacks ----
# SSTKT copies a pattern over five windows of segment 0x3F -- the headroom,
# another process's stack, its own stack below the gate's frame, a copy
# running past its own stack top and one wrapping to 3F:0000 -- and reads
# each back unchanged; then a TPA copy must land.  DIR afterwards shows
# the kernel survived.
SSTKIMG = build/sstktest.bin
SSTKLOG = build/verify-sstk.log
SSTKIN  = $(OSSEL)SSTKT\rDIR SSTKT.Z8K\r$(ENDIN)
.PHONY: verify-sstk
verify-sstk: all
	$(MKDISK) $(SSTKIMG) $(CPMSYS) $(CPMAIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SSTKIMG)) \
		--input='$(SSTKIN)' --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } | tee $(abspath $(SSTKLOG))
	@$(EMUOK)
	@grep -q 'SSTKT: PASS' $(SSTKLOG) \
		|| { echo "verify-sstk: FAIL -- a user SC #1 copy wrote segment 0x3F, or a TPA copy did not land: see the BAD lines above"; exit 1; }
	@grep -q '$(ENDMARK)' $(SSTKLOG) \
		|| { echo "verify-sstk: FAIL -- the CCP did not come back after SSTKT"; exit 1; }
	@echo "verify-sstk: PASS -- user SC #1 copies into segment 0x3F were refused"
	@echo "               and a TPA copy still works"

# ---- verify-ddtbrk: DDT.Z8K takes real breakpoints ----
# DDT records its SC #0 handler through BDOS fn 50 carrying BIOS fn 22
# for vector 32 (src/bdos/iosys.c), and reads the frame it is called with
# as DRI's 40 bytes: r0-r13, the caller's normal r14/r15, id, FCW, PC
# (src/bios/trap.s faultcom_).  Session, on a copy of drive A: with
# SCZERO added (tests/images.mk):
#   SCZERO    control: SC #0 with no handler recorded is a TRAP report
#             and a warm boot, as it always was
#   DDT MHELLO.Z8K
#             DDT plants SC #0 at the debugee's entry and runs it: the
#             first stop.  `b 3200000A' plants a second one past crt0's
#             `jr begin' and two POPLs (src/lib/crt0.s); `g' resumes.
#   ^C        at DDT's `-' prompt: the program ends, warm boot
#   SCZERO    again: DDT's handler must have died with DDT (proc.c
#             procdead, xvclr), so this is the same TRAP report, not a
#             jump into the debugger's freed segment
# DDT's `-' is not a prompt the emulator gates on, so everything after
# the DDT command line runs gate-off (\g); \003 is ^C (a printf format,
# like EDVERIFYFMT).  DDT starts a debugee NON-segmented (fcw 1800, its
# own choice) and MHELLO is a segmented program, so the second stop is
# placed before anything that depends on the mode.
DDTBRKIMG = build/ddtbrktest.bin
DDTBRKLOG = build/verify-ddtbrk.log
DDTBRKMAX = 200000000
DDTBRKFMT = $(OSSEL)SCZERO\rDDT MHELLO.Z8K\r\\gb 3200000A\rg\r\003SCZERO\r$(ENDIN)
.PHONY: verify-ddtbrk
verify-ddtbrk: all $(CPMADDT)
	$(MKDISK) $(DDTBRKIMG) $(CPMSYS) $(CPMADDT)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DDTBRKIMG)) \
		--input="$$(printf '$(DDTBRKFMT)')" --max=$(DDTBRKMAX) \
		$(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(DDTBRKLOG))
	@$(EMUOK)
	@grep -q 'Zilog portable debugger' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- DDT.Z8K did not run"; exit 1; }
	@# the entry breakpoint: id is SC #0 and the PC is the planted
	@# address.  With our 36-byte frame DDT read the id where the PC
	@# segment is and never took the breakpoint at all.
	@grep -q 'id=7F00 fcw=1000 pcs=B200 pc=0000 ' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- no stop at the debugee's entry (id=7F00 pc=0000): DDT's handler was not recorded, or read a frame of the wrong shape"; exit 1; }
	@grep -q '3200000A: *1 ' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- DDT did not accept the second breakpoint"; exit 1; }
	@# the second stop, at exactly the address planted: execution went on
	@# from the corrected PC (0000, not the 0002 past the SC -- that is a
	@# warm-boot stub), and the handler's frame edits were taken back.
	@grep -q 'id=7F00 fcw=1000 pcs=B200 pc=000A ' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- the continued debugee did not stop at the planted 000A"; exit 1; }
	@grep -q '^B200000A: 3524 0018 ' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- DDT did not list the instruction at the second breakpoint"; exit 1; }
	@# r14 is the banked NSPSEG of a non-segmented program, carried at
	@# +28 of DRI's frame: the two POPLs through @r14 moved it 3200 -> 3208
	@grep -q 're=3208 rf=FDFC' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- r14/r15 at the second stop are not the debugee's normal SP (frame +28/+30)"; exit 1; }
	@# SCZERO before DDT and after it: both a TRAP report for vector 32,
	@# and nothing else trapped
	@test "`grep -c 'TRAP vec=0020 id=7F00' $(DDTBRKLOG)`" = 2 \
		|| { echo "verify-ddtbrk: FAIL -- SC #0 with no handler did not report a trap both before and after DDT: DDT's vector outlived it"; exit 1; }
	@test "`grep -c 'TRAP vec=' $(DDTBRKLOG)`" = 2 \
		|| { echo "verify-ddtbrk: FAIL -- an unexpected trap"; exit 1; }
	@! grep -q 'SC #0 RETURNED' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- a program's SC #0 was resumed with no handler of its own"; exit 1; }
	@grep -q '$(ENDMARK)' $(DDTBRKLOG) \
		|| { echo "verify-ddtbrk: FAIL -- the session did not get back to the CCP"; exit 1; }
	@echo "verify-ddtbrk: PASS -- DDT stopped at its entry breakpoint and at a"
	@echo "               planted one, continued between them, and its trap"
	@echo "               vector died with it"

# ================= C10: THE CONSOLE-OWNERSHIP RULE =================
# A console has ONE owner.  The owner keeps it until it DETACHES, and any
# other process that asks for it WAITS.  That is MP/M's Attach Console and
# Detach Console, XDOS 146 and 147 (src/bdos/xdos.c); the rule itself is
# src/bdos/proc.c pconatt().  With an owner named, the second session
# starts at the COLD BOOT -- proc.c pcoldses(), called from bdosmisc.c
# bdosinit() -- which is the thing C5 built, measured and withdrew.
# SINCE D8 A SERIAL BOOT STARTS NONE (only a video console gets one, on
# SCC-B), so c10wait, c10brk and c10two type SESSION 1 first.
#
# Four targets, and they are four because they assert four different
# things and only one of them is the happy path:
#
#   verify-c10own   the round trip on a console this process already owns.
#		    One console, no wire, and the only one of the four that
#		    runs on a bare emulator.
#   verify-c10wait  146 BLOCKS on a console somebody else owns, and comes
#		    back when that owner detaches.  Two consoles.
#   verify-c10brk   ^C ON A HOLDER RELEASES WHAT IT HELD.  The one that
#		    matters: it is a path that only runs after something
#		    else has already gone wrong, so nothing else exercises
#		    it and a bug in it would sit there for ever.
#   verify-c10two   two sessions, two consoles, and neither one sees the
#		    other's input.
#
# All four use CATT.Z8K (src/tests/catt.c), which rides on $(CPMAXDOS) next
# to CON1.Z8K and for the same reason: that image is nobody's alignment.
C10IMG	= build/c10test.bin
# Prerequisites are the FILES it is made of, not the phony `all': a medium
# that is rebuilt on every make is rebuilt by every one of verify-all's
# workers, over the copy the others are booting.
$(C10IMG): $(CPMSYS) $(CPMAXDOS) $(CPMBIMG) $(wildcard $(KBOOT)) tools/mkcpmdisk.py
	$(MKDISK) $@ $(CPMSYS) $(CPMAXDOS) $(CPMBIMG)

# The guest writes to the medium it booted, so each of the four takes its own
# copy of it: they share the image, not the disk, and under verify-all they
# run at the same time.  A copy also means none of them boots what another
# one left behind, whatever order they run in.
C10RUN	= build/c10-$(notdir $@).bin
C10COPY	= cp $(C10IMG) $(C10RUN)

# ---- verify-c10own: 146 AND 147 ON A CONSOLE THIS PROCESS OWNS ----
# The transient IS the process the CCP was running in, so the console it
# was started from is already attached -- the CCP's own prompt-read
# attached it (conbdos.c getch).  So this is the round trip: attach
# (succeeds at once), detach, detach AGAIN, attach, read.
#
# THE SECOND DETACH IS WHAT MAKES THE FIRST ONE MEAN SOMETHING.  A 147
# that always answered yes would pass a test that checked only the first
# one; this one must be REFUSED, because by then the process no longer
# holds the console.  And the read at the end is there because a rule that
# gave a console back and could not take it again would be a rule that
# broke the machine on its way to being right.
C10OWNLOG = build/verify-c10own.log
.PHONY: verify-c10own
verify-c10own: all $(C10IMG)
	@# THE `7' IS TYPE-AHEAD (\i) AND IT HAS TO BE.  The feeder holds
	@# the byte after a carriage return until the guest prints a fresh
	@# prompt (emulator src/bus.c, inq_wait_seq), and CATT reads before
	@# it ever gets back to one -- so a paced `7' would never arrive.
	@# --input-mark releases it on `CATT: again 0' instead: after the
	@# console has been detached and retaken, which is the moment the
	@# read is meant to test.
	@$(C10COPY)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(C10RUN)) \
		--input="CATT\r\i7$(ENDIN)" --input-mark='CATT: again 0' \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } | tee $(abspath $(C10OWNLOG))
	@$(EMUOK)
	@tr -d '\r' < $(C10OWNLOG) > build/c10own.txt
	@grep -q 'CATT: FAIL' build/c10own.txt \
		&& { echo "verify-c10own: FAIL -- CATT said so itself:"; \
		     grep -h 'CATT: FAIL' build/c10own.txt; exit 1; } || true
	@grep -q '^CATT: con 0' build/c10own.txt \
		|| { echo "verify-c10own: FAIL -- CATT did not run at all"; exit 1; }
	@grep -q '^CATT: own 0' build/c10own.txt \
		|| { echo "verify-c10own: FAIL -- function 146 refused a console this"; \
		     echo "               process was already reading"; exit 1; }
	@grep -q '^CATT: gave 0' build/c10own.txt \
		|| { echo "verify-c10own: FAIL -- function 147 refused to give back a"; \
		     echo "               console this process held"; exit 1; }
	@grep -q '^CATT: not mine 0' build/c10own.txt \
		|| { echo "verify-c10own: FAIL -- the SECOND 147 succeeded, so 147 answers"; \
		     echo "               yes whatever the state is, and the first one"; \
		     echo "               proved nothing"; exit 1; }
	@grep -q '^CATT: again 0' build/c10own.txt \
		|| { echo "verify-c10own: FAIL -- 146 could not retake a console it had"; \
		     echo "               just detached"; exit 1; }
	@# Not anchored: function 1 echoes, so the `7' the emulator typed
	@# sits at the head of the line CATT then prints on.
	@grep -q 'CATT: read 7' build/c10own.txt \
		|| { echo "verify-c10own: FAIL -- the console did not read after being"; \
		     echo "               detached and reattached"; exit 1; }
	@echo "verify-c10own: PASS -- 146 on a console this process owns, 147 giving"
	@echo "               it back, a second 147 refused, 146 taking it again, and"
	@echo "               a character read through the console afterwards"

# ---- verify-c10wait: 146 BLOCKS, AND STOPS BLOCKING ----
# Console 1's session is started by SESSION 1 (typed first) and sits in getch() on
# console 1, which means it OWNS console 1.  `CATT W' on console 0 moves
# itself there (fn 148, which still costs nothing), says so ON CONSOLE 1
# -- output is not owned, only reading is -- and then asks for the console
# with 146.
#
# It must not get it.  The test then types `CATT D' at console 1, the
# session detaches, and the hand-off gives the console to the waiter
# rather than back to the CCP that is already running (src/bdos/proc.c
# pconhand, and the paragraph above it says why that is not a nicety).
#
# WHAT IS ASSERTED IS AN ORDER, IN ONE TRANSCRIPT: `CATT: waiting 1', then
# `CATT: gave 1', then `CATT: attached 1', all three on console 1.  The
# middle line is another process's detach.  A 146 that did not block would
# have printed the third line before the middle one, and the order is what
# says so -- no instruction count, no timing, and no interleaving between
# the two files.
C10WC0	= build/verify-c10wait-c0.log
C10WC1	= build/verify-c10wait-c1.log
.PHONY: verify-c10wait
verify-c10wait: all $(C10IMG)
	@$(C10COPY)
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(C10RUN) \
		--input='$(SESS1)CATT W\r$(ENDIN)' --max=$(EMUMAX) --stop-on=idle \
		--stop-mark='$(ENDMARK)' \
		--send-after='CATT: waiting 1' --send='USER 0\r' \
		--send-after='A>' --send='CATT D\r' \
		--log $(C10WC0) --wire-log $(C10WC1)
	@tr -d '\r' < $(C10WC0) > build/c10wait-c0.txt
	@tr -d '\r' < $(C10WC1) > build/c10wait-c1.txt
	@grep -q 'CATT: FAIL' build/c10wait-c0.txt build/c10wait-c1.txt \
		&& { echo "verify-c10wait: FAIL -- CATT said so itself:"; \
		     grep -h 'CATT: FAIL' build/c10wait-c0.txt build/c10wait-c1.txt; \
		     exit 1; } || true
	@grep -q 'CATT: waiting 1' build/c10wait-c1.txt \
		|| { echo "verify-c10wait: FAIL -- CATT never reached console 1, so either"; \
		     echo "                there is no session to contend with or the wire"; \
		     echo "                is not connected"; exit 1; }
	@grep -q 'CATT: gave 1' build/c10wait-c1.txt \
		|| { echo "verify-c10wait: FAIL -- the session on console 1 never ran"; \
		     echo "                \`CATT D', so it never detached"; exit 1; }
	@grep -q 'CATT: attached 1' build/c10wait-c1.txt \
		|| { echo "verify-c10wait: FAIL -- function 146 never returned: the waiter"; \
		     echo "                was not woken by the owner's 147"; exit 1; }
	@w=`grep -n 'CATT: waiting 1' build/c10wait-c1.txt | head -1 | sed -n 's/:.*//p'`; \
	 d=`grep -n 'CATT: gave 1' build/c10wait-c1.txt | head -1 | sed -n 's/:.*//p'`; \
	 a=`grep -n 'CATT: attached 1' build/c10wait-c1.txt | head -1 | sed -n 's/:.*//p'`; \
	 test "$$w" -lt "$$d" && test "$$d" -lt "$$a" \
		|| { echo "verify-c10wait: FAIL -- console 1 says waiting=$$w detach=$$d"; \
		     echo "                attached=$$a, so 146 did not block behind the"; \
		     echo "                owner: it returned without waiting for the 147"; \
		     exit 1; }
	@grep -q '^CATT: got 1' build/c10wait-c0.txt \
		|| { echo "verify-c10wait: FAIL -- CATT never came back to console 0 with"; \
		     echo "                console 1 in hand"; exit 1; }
	@grep -q '^CATT: freed 1' build/c10wait-c0.txt \
		|| { echo "verify-c10wait: FAIL -- CATT could not detach the console it had"; \
		     echo "                just been given"; exit 1; }
	@grep -q 'CATT: waiting 1' build/c10wait-c0.txt \
		&& { echo "verify-c10wait: FAIL -- console 1's output reached console 0"; \
		     exit 1; } || true
	@echo "verify-c10wait: PASS -- 146 blocked on the console the console-1 session"
	@echo "                owned and returned only after that session's 147:"
	@echo "                waiting, detach and attached, in that order on console 1"

# ---- verify-c10brk: ^C ON A HOLDER RELEASES WHAT IT HELD ----
# THE TARGET THIS FEATURE IS WORTH.  A program that attaches a console and
# never detaches holds it for the life of the machine, and the program
# that does that is not the polite one -- it is the one that crashed, or
# was ^C'd, or simply forgot.  So the release is in the BDOS, at the one
# place every way out of a program meets (src/bdos/proc.c procdead ->
# pconrel), and this is the target that proves it from outside.
#
# `CATT H' takes console 1 the way verify-c10wait does, says so, goes back
# to console 0 and reads there for ever.  It NEVER detaches: nothing in
# that mode calls 147.  The ^C is aimed with the emulator's --input-mark,
# so it is released only once `CATT: held 1' has been printed -- that is,
# only once the program really has the console -- rather than landing on
# whatever happens to be reading at the time.
#
# THE PROOF IS ON CONSOLE 1, AND IT IS SOMEBODY ELSE'S PROGRAM RUNNING.
# `CON1 X' is typed into console 1 while CATT H is holding it; the bytes
# sit in the receiver, unread, because the session that would read them is
# blocked in 146 behind the holder.  If `CON1: transient' ever appears on
# console 1, that session got its console back -- and the only thing that
# could have given it back is the ^C, because CATT H did not.
C10BC0	= build/verify-c10brk-c0.log
C10BC1	= build/verify-c10brk-c1.log
.PHONY: verify-c10brk
verify-c10brk: all $(C10IMG)
	@$(C10COPY)
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(C10RUN) \
		--input="`printf '$(SESS1)CATT H\r\\i\003CATT D\r'`" \
		--input-mark='CATT: held 1' \
		--max=$(EMUMAX) --stop-on=idle --stop-mark='$(ENDMARK)' \
		--send-after='CATT: grabbing 1' --send='USER 0\r' \
		--send-after='A>' --send='CATT D\r' \
		--send-after='CATT: holds 1' --send='CON1 X\r$(ENDIN)' \
		--log $(C10BC0) --wire-log $(C10BC1)
	@tr -d '\r' < $(C10BC0) > build/c10brk-c0.txt
	@tr -d '\r' < $(C10BC1) > build/c10brk-c1.txt
	@grep -q 'CATT: FAIL' build/c10brk-c0.txt build/c10brk-c1.txt \
		&& { echo "verify-c10brk: FAIL -- CATT said so itself:"; \
		     grep -h 'CATT: FAIL' build/c10brk-c0.txt build/c10brk-c1.txt; \
		     exit 1; } || true
	@grep -q 'CATT: gave 1' build/c10brk-c1.txt \
		|| { echo "verify-c10brk: FAIL -- the session on console 1 never detached,"; \
		     echo "               so the holder never got the console and there is"; \
		     echo "               nothing to release"; exit 1; }
	@grep -q 'CATT: holds 1' build/c10brk-c1.txt \
		|| { echo "verify-c10brk: FAIL -- CATT H never took console 1"; exit 1; }
	@grep -q 'CON1: transient' build/c10brk-c1.txt \
		|| { echo "verify-c10brk: FAIL -- CONSOLE 1 NEVER CAME BACK.  A program that"; \
		     echo "               held it was ^C'd and the console stayed held: the"; \
		     echo "               session blocked in 146 for the rest of the run and"; \
		     echo "               the command typed at it was never read"; exit 1; }
	@h=`grep -n 'CATT: holds 1' build/c10brk-c1.txt | head -1 | sed -n 's/:.*//p'`; \
	 t=`grep -n 'CON1: transient' build/c10brk-c1.txt | head -1 | sed -n 's/:.*//p'`; \
	 test "$$h" -lt "$$t" \
		|| { echo "verify-c10brk: FAIL -- console 1 ran the transient at line $$t,"; \
		     echo "               before the holder took the console at line $$h, so"; \
		     echo "               the console was never actually held"; exit 1; }
	@grep -q '^CATT: gave 0' build/c10brk-c0.txt \
		|| { echo "verify-c10brk: FAIL -- console 0 never ran another command after"; \
		     echo "               the ^C, so the ^C did not reach the holder at all"; \
		     exit 1; }
	@grep -q 'CON1: transient' build/c10brk-c0.txt \
		&& { echo "verify-c10brk: FAIL -- console 1's transient printed on console 0"; \
		     exit 1; } || true
	@echo "verify-c10brk: PASS -- a program held console 1 and was ^C'd without ever"
	@echo "               detaching; the session blocked behind it then read the"
	@echo "               command that had been waiting in the receiver and ran it,"
	@echo "               so the abnormal exit is what released the console"

# ---- verify-c10two: TWO SESSIONS, TWO CONSOLES, TWO SETS OF KEYS ----
# Both sessions exist before CATT runs: console 0's is the cold boot
# and console 1's is SESSION 1's.  Each runs CATT with no argument, which
# asks fn 153 which console it is on, takes it and gives it back, and
# READS ONE CHARACTER.  The two characters typed are different and both
# consoles are told to run the same program, so if either console could
# see the other's keys the digit would come out wrong -- and if either
# session could be pushed onto the other's device, the console number
# would.
#
# Nothing here contends: each session already owns the console it is
# sitting on, so both 146s succeed at once.  That is the point.  A rule
# that made two independent sessions wait for each other would be a rule
# that had broken the machine.
#
# THE STOP MARK IS CONSOLE 0's AND ONLY CONSOLE 0's.  The emulator's
# --stop-mark watches every serial channel, so a `ZZEND' typed at console
# 1 would end the run the moment that console finished -- with console 0
# still spelling out its command a byte at a time, and its half of the
# test never run.  Console 0 is the slower of the two (it waits for the
# input mark, then pays the feeder's per-byte quiet for six more bytes),
# so ending on console 0's mark ends the run after both are done.
C10TC0	= build/verify-c10two-c0.log
C10TC1	= build/verify-c10two-c1.log
.PHONY: verify-c10two
verify-c10two: all $(C10IMG)
	@$(C10COPY)
	python3 tests/wirecon.py --emu '$(EMU)' --disk $(C10RUN) \
		--input='$(SESS1)CATT\r\i0$(ENDIN)' --input-mark='CATT: again 0' \
		--max=$(EMUMAX) --stop-on=idle \
		--stop-mark='$(ENDMARK)' \
		--send-after='1A>' --send='USER 0\r' \
		--send-after='A>' --send='CATT\r5' \
		--log $(C10TC0) --wire-log $(C10TC1)
	@tr -d '\r' < $(C10TC0) > build/c10two-c0.txt
	@tr -d '\r' < $(C10TC1) > build/c10two-c1.txt
	@grep -q 'CATT: FAIL' build/c10two-c0.txt build/c10two-c1.txt \
		&& { echo "verify-c10two: FAIL -- CATT said so itself:"; \
		     grep -h 'CATT: FAIL' build/c10two-c0.txt build/c10two-c1.txt; \
		     exit 1; } || true
	@grep -q '^CATT: con 0' build/c10two-c0.txt \
		|| { echo "verify-c10two: FAIL -- console 0's session does not think it is"; \
		     echo "               on console 0"; exit 1; }
	@grep -q '^CATT: con 1' build/c10two-c1.txt \
		|| { echo "verify-c10two: FAIL -- console 1's session does not think it is"; \
		     echo "               on console 1, or there is no session there"; exit 1; }
	@# Not anchored: function 1 echoes, so the digit each console was
	@# typed sits at the head of the line CATT then prints on.
	@grep -q 'CATT: read 0' build/c10two-c0.txt \
		|| { echo "verify-c10two: FAIL -- console 0 did not read the character typed"; \
		     echo "               at console 0"; exit 1; }
	@grep -q 'CATT: read 5' build/c10two-c1.txt \
		|| { echo "verify-c10two: FAIL -- console 1 did not read the character typed"; \
		     echo "               at console 1"; exit 1; }
	@grep -q 'CATT: read 5' build/c10two-c0.txt \
		&& { echo "verify-c10two: FAIL -- console 0 saw the character typed at"; \
		     echo "               console 1"; exit 1; } || true
	@grep -q 'CATT: read 0' build/c10two-c1.txt \
		&& { echo "verify-c10two: FAIL -- console 1 saw the character typed at"; \
		     echo "               console 0"; exit 1; } || true
	@grep -q 'CATT: con 1' build/c10two-c0.txt \
		&& { echo "verify-c10two: FAIL -- console 1's output reached console 0"; \
		     exit 1; } || true
	@grep -q 'CATT: con 0' build/c10two-c1.txt \
		&& { echo "verify-c10two: FAIL -- console 0's output reached console 1"; \
		     exit 1; } || true
	@echo "verify-c10two: PASS -- two sessions, each owning the console it sits on,"
	@echo "               each reading only the character typed at its own device"

# ---- the pool must not contain the framebuffer (verify-pgseg) ----
#
# First-release review P1 #9 and #10, and the two halves of this target
# answer them in turn.
#
# #9.  src/bios/pgalloc.c hands out logical segments PGSEGLO..PGSEGLO+6 and
# mapseg()s each one as it goes.  Those segments used to be 0x38..0x3E,
# which INCLUDES 0x3A and 0x3B -- the ROM's two display planes
# (src/bios/crsr.c writes character cells in 0x3a and homes the HR bitmap
# through 0x3b; rom_source/display_re.c names them VRAM_A_CHAR and
# VRAM_A_ATTR).  The third allocation from an empty pool therefore
# reprogrammed the display descriptor onto a pool page: console output went
# into process memory and pgfree() never put the video page back.  NOTHING
# IN THIS SUITE COULD SEE IT, and it is worth being precise about why: the
# emulator's console is serial (crsr.c picks CK_SER when the ROM's CONALT
# and CONHIRES cells are clear, which is why verify-crsr reads the console
# with tests/vt.py, an ANSI parser), so no verify target has ever put the
# console on segment 0x3a.  "Print something after three allocations" is
# consequently NOT a test that can fail here.  The tests that can are the
# arithmetic, read out of the header rather than restated, and the segment
# numbers the machine actually served.
#
# #10.  pgrelall() runs on every warm boot and used to release every
# allocated, unheld slot.  Unheld means "not a parked 64 KB process image",
# which is true of a background 8086 or Z80 interpreter's guest segments
# while the interpreter is running -- so a foreground warm boot handed a
# live program's memory back to the pool.  CONCW/CONCWB/CONCWC (see
# src/tests/concw.c) make that observable with no timing in it at all.
PGSEGLO  = $(shell sed -n 's/^#define[ 	]*PGSEGLO[ 	][ 	]*\(0x[0-9a-fA-F]*\).*/\1/p' src/bios/c900cfg.h)
PGNSLOT  = $(shell sed -n 's/^#define[ 	]*PGNSLOT[ 	][ 	]*\([0-9]*\).*/\1/p' src/bios/c900cfg.h)
PGNUP    = $(shell sed -n 's/^#define[ 	]*PGNUP[ 	][ 	]*\([0-9]*\).*/\1/p' src/bios/c900cfg.h)
PGSEGVIDA = $(shell sed -n 's/^#define[ 	]*PGSEGVIDA[ 	][ 	]*\(0x[0-9a-fA-F]*\).*/\1/p' src/bios/c900cfg.h)
PGSEGVIDB = $(shell sed -n 's/^#define[ 	]*PGSEGVIDB[ 	][ 	]*\(0x[0-9a-fA-F]*\).*/\1/p' src/bios/c900cfg.h)
PGSEGIMG = build/pgseg.bin
PGSEGLOG = build/verify-pgseg.log
PGSEGIN  = $(OSSEL)CONCW\rCONCWC\r

.PHONY: verify-pgseg
verify-pgseg: all $(CPMACONCW)
	@test -n "$(PGSEGLO)" -a -n "$(PGNSLOT)" \
		-a -n "$(PGSEGVIDA)" -a -n "$(PGSEGVIDB)" \
		|| { echo "verify-pgseg: FAIL -- PGSEGLO/PGNSLOT/PGSEGVIDA/PGSEGVIDB"; \
		     echo "              could not be read out of src/bios/c900cfg.h, so"; \
		     echo "              this target does not know what the pool is."; exit 1; }
	@# The whole pool, slot by slot, against the two display planes.
	@# Slots 0..PGNUP-1 ascend from PGSEGLO and the rest descend below
	@# it (c900cfg.h PGSEG); a slot outside 0x02..0x2F would be ROM,
	@# resident or display territory.
	@lo=$$(($(PGSEGLO))); n=$$(($(PGNSLOT))); up=$$(($(PGNUP))); \
	 va=$$(($(PGSEGVIDA))); vb=$$(($(PGSEGVIDB))); \
	 i=0; while [ $$i -lt $$n ]; do \
		if [ $$i -lt $$up ]; then s=$$((lo + i)); \
		else s=$$((lo - 1 - (i - up))); fi; \
		if [ $$s -lt 2 ] || [ $$s -gt 47 ]; then \
			printf "verify-pgseg: FAIL -- slot %d is segment 0x%x, outside 0x02..0x2F\n" $$i $$s; \
			exit 1; \
		fi; \
		if [ $$s -eq $$va ] || [ $$s -eq $$vb ]; then \
			echo "verify-pgseg: FAIL -- the allocator pool includes logical"; \
			echo "              segment $(PGSEGVIDA)/$(PGSEGVIDB) territory: slot $$i is segment"; \
			printf  "              0x%x, a ROM display plane.  pgalloc() mapseg()s\n" $$s; \
			echo "              every segment it hands out, so that allocation"; \
			echo "              redirects the console into process memory and"; \
			echo "              pgfree() never restores the display mapping."; \
			exit 1; \
		fi; \
		i=$$((i + 1)); \
	 done
	$(MKDISK) $(PGSEGIMG) $(CPMSYS) $(CPMACONCW) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(PGSEGIMG)) \
		--input="$(PGSEGIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(PGSEGLOG))
	@$(EMUOK)
	@tr -d '\r' < $(PGSEGLOG) > build/pgseg.txt
	@grep -q 'CONCW: no segment' build/pgseg.txt \
		&& { echo "verify-pgseg: FAIL -- the pool was empty, so nothing below this"; \
		     echo "              could run.  A 512 KB machine has no free page"; \
		     echo "              (src/bios/pgalloc.c); the emulator is 1 MB."; exit 1; } || true
	@grep -q 'CONCW: no second process' build/pgseg.txt \
		&& { echo "verify-pgseg: FAIL -- function 144 refused, so there was never a"; \
		     echo "              background process whose memory a warm boot could take."; \
		     exit 1; } || true
	@# The three numbers this test is made of.
	@sed -n 's/^CONCW: A seg \([0-9A-F][0-9A-F]\)$$/\1/p' build/pgseg.txt > build/pgseg-a.txt
	@sed -n 's/^CONCWB: B seg \([0-9A-F][0-9A-F]\)$$/\1/p' build/pgseg.txt > build/pgseg-b.txt
	@sed -n 's/^CONCWC: got //p' build/pgseg.txt > build/pgseg-c.txt
	@test -s build/pgseg-a.txt -a -s build/pgseg-b.txt -a -s build/pgseg-c.txt \
		|| { echo "verify-pgseg: FAIL -- one of the three programs never reported its"; \
		     echo "              segments.  The transcript is above."; exit 1; }
	@# No segment anybody was handed may be a display plane.  The check
	@# above is the pool as configured; this is the pool as served.
	@for s in `cat build/pgseg-a.txt build/pgseg-b.txt build/pgseg-c.txt | tr -d ' \n' | sed 's/../& /g'`; do \
		case "$$s" in 3A|3B) \
			echo "verify-pgseg: FAIL -- BIOS function 25 handed out segment $$s,"; \
			echo "              which is one of the ROM's display planes."; \
			exit 1;; \
		esac; done
	@# #10, and the assertion runs in two directions because either one
	@# alone would be passed by a bug.  CONCW's own segment MUST come
	@# back: a warm boot is still the end of the program that warm booted,
	@# and a fix that leaked instead of over-freeing would be no better.
	@a=`cat build/pgseg-a.txt`; c=`cat build/pgseg-c.txt`; \
	 case " $$c " in *" $$a "*) : ;; \
		*) echo "verify-pgseg: FAIL -- the warm boot did NOT reclaim CONCW's own"; \
		   echo "              segment $$a.  CONCWC was given: $$c"; \
		   echo "              pgrelall() must still free the scratch of the"; \
		   echo "              process that warm booted."; exit 1;; esac
	@# ...and CONCWB's must not, because CONCWB is still alive and still
	@# using it.  This is the failure the review reported.
	@b=`cat build/pgseg-b.txt`; c=`cat build/pgseg-c.txt`; \
	 case " $$c " in *" $$b "*) \
		echo "verify-pgseg: FAIL -- the foreground warm boot freed segment $$b,"; \
		echo "              which belongs to the LIVE background process, and the"; \
		echo "              pool then handed it to the next program to ask"; \
		echo "              (CONCWC was given: $$c).  src/bios/pgalloc.c"; \
		echo "              pgrelall() must release only the warm-booting"; \
		echo "              process's own slots."; exit 1;; esac
	@# And the memory itself, not only the bookkeeping.
	@b=`cat build/pgseg-b.txt`; \
	 grep -q "CONCWB: B still seg $$b, signature intact" build/pgseg.txt \
		|| { echo "verify-pgseg: FAIL -- the background process did not read its own"; \
		     echo "              signature back out of segment $$b after the"; \
		     echo "              foreground warm boot."; \
		     grep 'CONCWB' build/pgseg.txt; exit 1; }
	@a=`cat build/pgseg-a.txt`; b=`cat build/pgseg-b.txt`; c=`cat build/pgseg-c.txt`; \
	 echo "verify-pgseg: PASS -- pool $(PGSEGLO)..+$(PGNSLOT) clear of the display planes"; \
	 echo "              $(PGSEGVIDA)/$(PGSEGVIDB); the warm boot reclaimed the foreground's $$a and"; \
	 echo "              left the live background's $$b alone (CONCWC got:$$c)"

# ---- a wedged disk controller must be reported, not retried for ever ----
# src/bios/wd900.c wdsec() in the first-release review.  wdsec() looped `for (;;)'
# on the controller's 0x76 "busy, retry" answer and each turn called
# wdgo900(), which starts a fresh three-second deadline -- so a wedged
# controller hung inside one BIOS read and the BDOS never got a status to
# report.  tests/wdtest.c drives the real wdsec() on the host against a
# controller that answers 0x76 every time.  `timeout' is the point: the old
# code does not return, so the test has to be able to say that.
.PHONY: verify-wdbusy
verify-wdbusy: build/wdtest
	@timeout 20 ./build/wdtest; rc=$$?; \
	 test $$rc = 0 \
		|| { echo "verify-wdbusy: FAIL -- wdsec() did not bound its retries on a"; \
		     echo "               controller stuck at 0x76 (rc $$rc; 124 means it"; \
		     echo "               never returned at all)."; exit 1; }
	@echo "verify-wdbusy: PASS -- a wedged controller comes back as a status"

build/wdtest: tests/wdtest.c src/bios/wd900.c | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -o $@ tests/wdtest.c


# ======================================================================
# F1 -- THE DIRECTORY IS THE THING TWO PROCESSES SHARE, and all three of
# its guards were missing.  Closes P1 #1, #3 and #8 of
# docs/cpm/docs/FIRST-RELEASE-REVIEW-2026-09-12.md in c900oses.
# ======================================================================

# ---- verify-dirgen: a peer's directory entry must survive our close ----
#
# src/bdos/dskutil.c dirget().  pdirbuf and dirsecn are per-process (they
# are inside struct stvars, which src/bdos/proc.c copies at every switch),
# and with cks == 0 -- every C900 drive, src/bios/bios900.c -- a cached
# record used to be handed straight back.  close() then writes all 128
# bytes of it, so a record cached before a peer's create and written after
# it erases the peer's entry.
#
# The ordering is the programs', not the scheduler's: DGENA and DGENB hand
# each other XDOS flags (BDOS 132/133), so DGENA has the record cached
# before DGENB creates and closes after it.  `DGENB: made=2' asserts the
# situation was actually built -- both entries in ONE 128-byte record --
# and without it the target would pass having tested nothing.
DIRGIMG	= build/dirgen.bin
DIRGLOG	= build/dirgen.log
.PHONY: verify-dirgen
verify-dirgen: all $(CPMADIRG)
	$(MKDISK) $(DIRGIMG) $(CPMSYS) $(CPMADIRG) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DIRGIMG)) \
		--input="$(OSSEL)DGENA\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(DIRGLOG))
	@$(EMUOK)
	@grep -q 'DGENA: A start' $(DIRGLOG) \
		|| { echo "verify-dirgen: FAIL -- DGENA did not run at all"; exit 1; }
	@grep -q 'DGENA: no second process' $(DIRGLOG) \
		&& { echo "verify-dirgen: FAIL -- function 144 refused; the reason is in the"; \
		     echo "               transcript above"; exit 1; } || true
	@grep -q 'DGENA: FAIL' $(DIRGLOG) \
		&& { echo "verify-dirgen: FAIL -- DGENA could not set the situation up; see"; \
		     echo "               the transcript above"; exit 1; } || true
	@grep -q 'DGENA: slot=1' $(DIRGLOG) \
		|| { echo "verify-dirgen: FAIL -- the target's entry is not at slot 1 of a"; \
		     echo "               directory record, so a stale copy of that record"; \
		     echo "               would not cover the peer's entry at all"; exit 1; }
	@grep -q 'DGENB: made=2' $(DIRGLOG) \
		|| { echo "verify-dirgen: FAIL -- the peer's entry did not land in the SAME"; \
		     echo "               128-byte record as the target's, so nothing was"; \
		     echo "               shared and this target tested nothing"; exit 1; }
	@grep -q 'DGENA: own=1' $(DIRGLOG) \
		|| { echo "verify-dirgen: FAIL -- the closing process lost its OWN file"; exit 1; }
	@grep -q 'DGENA: peer=1' $(DIRGLOG) \
		|| { echo "verify-dirgen: FAIL -- one process's close() overwrote another"; \
		     echo "               process's directory entry.  The per-process cached"; \
		     echo "               directory record (src/bdos/dskutil.c dirget) was"; \
		     echo "               reused after a peer had rewritten that record, and"; \
		     echo "               the whole 128 bytes went back to the disk."; exit 1; }
	@grep -q 'DGENA: A done' $(DIRGLOG) \
		|| { echo "verify-dirgen: FAIL -- DGENA did not finish"; exit 1; }
	@grep -q 'DGENB: B done' $(DIRGLOG) \
		|| { echo "verify-dirgen: FAIL -- DGENB did not finish"; exit 1; }
	@echo "verify-dirgen: PASS -- a process closed a file over a directory record"
	@echo "               another process had rewritten, and both entries survived"

# ---- verify-dirwerr: a refused transfer is not a success ----
#
# src/bdos/dskutil.c rdwrt() left its retry loop and returned zero, so a
# transfer the medium refused was reported as complete.  The `C' answer
# (continue with bad data) is the sharpest form of it: error mode 0 sets no
# errcode either, so nothing anywhere said the record had not been written.
#
# The device error is REAL.  The medium is truncated so drive B: keeps its
# whole directory (32 sectors) and its first data block (8 more) and nothing
# after that, and the emulated hard-disk controller answers 92h, drive not
# ready, for an LBA past the end of the image file (bus.c).  DERR writes
# past that block; the scripted operator answers `C' once, which is all the
# session needs whether or not the write is reported.
#
# THE PROMPT IS PART OF THE ASSERTION.  `write error on drive B' in the
# transcript is what says a physical error actually happened; without it
# `no failure reported' would be indistinguishable from `nothing failed'.
# CPMB_BASEBLK (mk/config.mk) is where the B: partition starts.  DIRWKEEP is
# how many of its sectors the truncated medium keeps: 32 for the whole
# directory (four 4096-byte allocation blocks) plus 8 for one data block.
DIRWKEEP = 40
DIRWIMG	= build/dirwerr.bin
DIRWLOG	= build/dirwerr.log
.PHONY: verify-dirwerr
verify-dirwerr: all $(CPMADIRG)
	$(MKDISK) $(DIRWIMG) $(CPMSYS) $(CPMADIRG) $(CPMBIMG)
	truncate -s $$(( ($(CPMB_BASEBLK) + $(DIRWKEEP)) * 512 )) $(DIRWIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DIRWIMG)) \
		--input="$(OSSEL)DERR\r\iC$(ENDIN)" --input-mark="Continue with bad data" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(DIRWLOG))
	@$(EMUOK)
	@grep -q 'DERR: start' $(DIRWLOG) \
		|| { echo "verify-dirwerr: FAIL -- DERR did not run at all"; exit 1; }
	@grep -q 'DERR: FAIL' $(DIRWLOG) \
		&& { echo "verify-dirwerr: FAIL -- DERR could not set up on B:; see above"; exit 1; } || true
	@grep -q 'write error on drive B' $(DIRWLOG) \
		|| { echo "verify-dirwerr: FAIL -- no physical write error happened at all, so"; \
		     echo "                the truncation did not remove the block DERR aims"; \
		     echo "                at and this target tested nothing"; exit 1; }
	@grep -q 'DERR: refused=unreported' $(DIRWLOG) \
		&& { echo "verify-dirwerr: FAIL -- the write was reported as SUCCESSFUL even"; \
		     echo "                though the medium refused it and the operator said"; \
		     echo "                to continue with bad data.  rdwrt() returned zero"; \
		     echo "                after leaving its retry loop (src/bdos/dskutil.c),"; \
		     echo "                which is also what let a failed directory read be"; \
		     echo "                published as cache and a failed directory write be"; \
		     echo "                followed by clraloc()."; exit 1; } || true
	@grep -q 'DERR: refused=reported' $(DIRWLOG) \
		|| { echo "verify-dirwerr: FAIL -- DERR reported nothing"; exit 1; }
	@grep -q 'DERR: done' $(DIRWLOG) \
		|| { echo "verify-dirwerr: FAIL -- DERR did not finish"; exit 1; }
	@echo "verify-dirwerr: PASS -- a write the medium refused was reported to the"
	@echo "                program instead of being counted as a success"

# ---- verify-dirbnd: a corrupt block number stays out of the next drive ----
#
# src/bdos/fileio.c alloc() handed directory-supplied block numbers to
# setaloc() with no check against the drive's dsm, and setaloc had no bound
# of its own.  src/bios/bios900.c drvinit() carves every drive's alv out of
# ONE 1536-byte pool in drive order, so a block number past A:'s dsm lands
# inside B:'s LIVE allocation map: tests/dirpoke.py writes 2600 into an A:
# entry (byte 325 of the pool, B:'s block 40).
#
# The run carries its own control: B: is logged in and its free space read,
# A: is logged in next -- which is when the corrupt entry is scanned -- and
# B:'s free space is read again without B: being logged in a second time.
# Nothing but the scan of A: can have moved it.  No second session and no
# expected constant, so nothing here goes stale when the disk contents do.
DIRBIMG	= build/dirbnd.bin
DIRBLOG	= build/dirbnd.log
.PHONY: verify-dirbnd
verify-dirbnd: all $(CPMADIRB)
	$(MKDISK) $(DIRBIMG) $(CPMSYS) $(CPMADIRB) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(DIRBIMG)) \
		--input="$(OSSEL)DBOUND\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(DIRBLOG))
	@$(EMUOK)
	@grep -q 'DBOUND: start' $(DIRBLOG) \
		|| { echo "verify-dirbnd: FAIL -- DBOUND did not run at all"; exit 1; }
	@grep -q 'DBOUND: b0=0' $(DIRBLOG) \
		&& { echo "verify-dirbnd: FAIL -- drive B: reported no free space at all, so"; \
		     echo "               the two readings cannot say anything"; exit 1; } || true
	@grep -q 'DBOUND: peermap=intact' $(DIRBLOG) \
		|| { echo "verify-dirbnd: FAIL -- logging drive A: in changed drive B:'s free"; \
		     echo "               space.  A corrupt block number in an A: directory"; \
		     echo "               entry reached setaloc() unbounded and set a bit in"; \
		     echo "               B:'s live allocation map (one 1536-byte pool holds"; \
		     echo "               both -- src/bios/bios900.c drvinit)."; exit 1; }
	@grep -q 'DBOUND: done' $(DIRBLOG) \
		|| { echo "verify-dirbnd: FAIL -- DBOUND did not finish"; exit 1; }
	@echo "verify-dirbnd: PASS -- a directory entry claiming a block past the"
	@echo "               drive's dsm left the next drive's allocation map alone"

# ---- a refusal that mutates first is not a refusal ----
# P1 #2 and the password/XFCB P2 items of the first-release review, in one
# session, because all four are the same mistake: the program is told no
# and the protected object has already changed.
#
# The four legs, and what each of them is allowed to conclude.
#
#  (a) The read-only ATTRIBUTE.  error(5) returns whenever function 45
#      error mode is 0FEh or 0FFh (bdosmisc.c error()), and delete, rename and
#      truncate went on to erase the entry, overwrite the name, and free
#      blocks.  In the DEFAULT mode error(5) reaches filero(), where `A'
#      aborts through warmboot() and `C' clears the read-only bit before
#      returning -- so the mutation there is the operator's instruction
#      and this leg cannot be shown in mode 0 at all.  Arming has nothing
#      to do with this leg, so BOTH runs demand the same three refusals.
#  (b) Function 15's user-0 SYS fallback re-scanned with drvcode = 0 and no
#      second chk$password (bdosmain.c:323), and the XFCB it should have
#      found lives in user 0 -- so from user 3 a read-protected user-0 SYS
#      file opened with no password.  The positive half matters as much:
#      with the password (function 106) the same open must still work, and
#      an unprotected user-0 SYS file must still be shared.
#  (c) Function 99 asked no password at all, unlike erase and rename.
#  (d) Function 103 is in neither preflight group (bdosmain.c:163-169), so on a
#      drive the program had just marked read-only with function 28 it went
#      through and wrote an XFCB.  Nothing deeper stops it: dskutil.c:110
#      calls error(4) on a read-only drive and then writes anyway, which
#      only mode 0 survives because there error(4) never returns.  This
#      refusal is the dispatcher's, so it does not depend on the label and
#      both images must end with no XFCB for ROTX.TXT.
#
# Three witnesses, as verify-pass has: the code the call returned, the file
# as the same program sees it afterwards (function 35's record count,
# function 15 on both names), and the disk itself, read host-side by a
# program that does not share a BDOS with the one under test.  The second
# and third are the point of the row -- a test that checked only the return
# value would pass against the broken BDOS.
#
# The control image differs from the armed one in the label's password bit
# alone.  There the two password legs are NOT enforced, and ROTP.TXT ends
# up truncated: that is what makes the armed run's refusals refusals about
# a password rather than about truncation.
ROTMKCPMA  = build/cpma-rotmk.img
ROTMKIMG   = build/rotmk.bin
ROTARMCPMA = build/cpma-rot.img
ROTCTLCPMA = build/cpma-rotctl.img
ROTARMIMG  = build/rot.bin
ROTCTLIMG  = build/rotctl.bin
ROTLOG	   = build/verify-refuse
# R on the three files leg (a) uses, S on the two the user-0 fallback needs
ROTATTR = ROTD.TXT 0R ROTR.TXT 0R ROTT.TXT 0R ROTS.TXT 0S ROTN.TXT 0S
ROTXFCB = --xfcb ROTS.TXT:0x80:SSECRET --xfcb ROTP.TXT:0x20:PSECRET \
	  --xfcb ROTQ.TXT:0x20:QSECRET
.PHONY: verify-refuse
verify-refuse: all
	cp $(CPMAIMG) $(ROTMKCPMA)
	$(MKDISK) $(ROTMKIMG) $(CPMSYS) $(ROTMKCPMA)
	@# pass 1, on an ordinary image: mkcpmfs.py can add an XFCB to a
	@# finished image but not a file, so the files come from a session
	{ $(EMUCD) && ./c900 --disk=$(abspath $(ROTMKIMG)) \
		--input="$(OSSEL)ROT MAKE\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(ROTLOG)-0.log)
	@$(EMUOK)
	@grep -q 'ROT: made' $(ROTLOG)-0.log \
		|| { echo "verify-refuse: FAIL -- the eight fixture files were not created"; exit 1; }
	dd if=$(ROTMKIMG) of=$(ROTARMCPMA) bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/ccpuser.py $(ROTARMCPMA) $(ROTATTR)
	cp $(ROTARMCPMA) $(ROTCTLCPMA)
	python3 tools/mkcpmfs.py --label C900R \
		--label-mode create,update,password $(ROTXFCB) $(ROTARMCPMA)
	python3 tools/mkcpmfs.py --label C900R \
		--label-mode create,update $(ROTXFCB) $(ROTCTLCPMA)
	@# the fixture has to BE the fixture, or the refusals below prove
	@# nothing: one bit of label between the images, R on three files,
	@# S on two, three XFCBs and none for ROTX.TXT
	@python3 tools/mkcpmfs.py --entries $(ROTARMCPMA) \
		| grep -q 'label C900R .*mode 0xb1 \[password,update,create,exists\]' \
		|| { echo "verify-refuse: FAIL -- the armed image has no password bit"; exit 1; }
	@python3 tools/mkcpmfs.py --entries $(ROTCTLCPMA) \
		| grep -q 'label C900R .*mode 0x31 \[update,create,exists\]' \
		|| { echo "verify-refuse: FAIL -- the control image IS armed"; exit 1; }
	@python3 tests/dirattr.py $(ROTARMCPMA) --user 0 > build/rot-attr-before.txt
	@for n in ROTD ROTR ROTT; do grep -qE "^$$n\.TXT +R$$" build/rot-attr-before.txt \
		|| { echo "verify-refuse: FAIL -- $$n.TXT is not read-only to start with"; exit 1; }; done
	@for n in ROTS ROTN; do grep -qE "^$$n\.TXT +S$$" build/rot-attr-before.txt \
		|| { echo "verify-refuse: FAIL -- $$n.TXT is not a SYS file"; exit 1; }; done
	@test "`python3 tools/mkcpmfs.py --entries $(ROTARMCPMA) | grep -c ' xfcb '`" = 3 \
		|| { echo "verify-refuse: FAIL -- the armed image does not carry exactly three XFCBs"; exit 1; }
	@python3 tools/mkcpmfs.py --entries $(ROTARMCPMA) | grep -q 'xfcb  ROTX' \
		&& { echo "verify-refuse: FAIL -- ROTX.TXT starts with an XFCB"; exit 1; } || true
	@python3 tools/mkcpmfs.py --list $(ROTARMCPMA) | grep 'ROT' > build/rot-list-before.txt
	@cat build/rot-list-before.txt
	$(MKDISK) $(ROTARMIMG) $(CPMSYS) $(ROTARMCPMA)
	$(MKDISK) $(ROTCTLIMG) $(CPMSYS) $(ROTCTLCPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(ROTARMIMG)) \
		--input="$(OSSEL)ROT\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(ROTLOG)-1.log)
	@$(EMUOK)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(ROTCTLIMG)) \
		--input="$(OSSEL)ROT NONE\r$(ENDIN)" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(ROTLOG)-2.log)
	@$(EMUOK)
	@grep -q 'ROT: PASS' $(ROTLOG)-1.log \
		|| { echo "verify-refuse: FAIL -- the armed run, see its BAD lines above"; exit 1; }
	@grep -q 'ROT: PASS' $(ROTLOG)-2.log \
		|| { echo "verify-refuse: FAIL -- THE CONTROL RUN.  Either a password check fired on a drive nobody armed, or a read-only attribute stopped being enforced; see its BAD lines above"; exit 1; }
	@# the other half of error mode 0FEh: v3's own wording on the
	@# console, and the function number that raised it
	@grep -q 'Read/Only File' $(ROTLOG)-1.log \
		|| { echo "verify-refuse: FAIL -- no 'Read/Only File' message on the console"; exit 1; }
	@grep -q 'Password Error' $(ROTLOG)-1.log \
		|| { echo "verify-refuse: FAIL -- no 'Password Error' message on the console"; exit 1; }
	@grep -q 'Read/Only Disk' $(ROTLOG)-1.log \
		|| { echo "verify-refuse: FAIL -- no 'Read/Only Disk' message on the console"; exit 1; }
	@grep -q 'BDOS Function = 103' $(ROTLOG)-1.log \
		|| { echo "verify-refuse: FAIL -- the read-only drive refusal did not come from function 103"; exit 1; }
	@grep -q 'Password Error' $(ROTLOG)-2.log \
		&& { echo "verify-refuse: FAIL -- the control run reported a password error"; exit 1; } || true
	@# ---- and now the disk, which shares nothing with the program
	dd if=$(ROTARMIMG) of=build/rot-arm-after.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	dd if=$(ROTCTLIMG) of=build/rot-ctl-after.img bs=512 \
		skip=$(CPMA_BASEBLK) count=$(CPMA_BLOCKS) status=none conv=sparse
	@python3 tools/mkcpmfs.py --list build/rot-arm-after.img | grep 'ROT' \
		> build/rot-list-after.txt
	@cat build/rot-list-after.txt
	@python3 tests/dirattr.py build/rot-arm-after.img --user 0 \
		> build/rot-attr-after.txt
	@# (a) the three read-only files, byte for byte the size they were,
	@# under the names they had, still read-only
	@for n in ROTD ROTR; do grep -qE "^ *0 $$n\.TXT +128 bytes" build/rot-list-after.txt \
		|| { echo "verify-refuse: FAIL -- $$n.TXT was deleted or resized by a call that refused"; exit 1; }; done
	@grep -qE '^ *0 ROTT\.TXT +25600 bytes' build/rot-list-after.txt \
		|| { echo "verify-refuse: FAIL -- ROTT.TXT WAS TRUNCATED by the function 99 that refused"; exit 1; }
	@grep -q 'ROTRX' build/rot-list-after.txt \
		&& { echo "verify-refuse: FAIL -- ROTRX.TXT exists: the rename that refused renamed the file"; exit 1; } || true
	@for n in ROTD ROTR ROTT; do grep -qE "^$$n\.TXT +R$$" build/rot-attr-after.txt \
		|| { echo "verify-refuse: FAIL -- $$n.TXT lost its read-only attribute"; exit 1; }; done
	@# (c) the protected file kept every record on the armed drive, and
	@# lost them on the unarmed one -- so the refusal was the password
	@grep -qE '^ *0 ROTP\.TXT +25600 bytes' build/rot-list-after.txt \
		|| { echo "verify-refuse: FAIL -- ROTP.TXT WAS TRUNCATED without its password"; exit 1; }
	@grep -qE '^ *0 ROTQ\.TXT +6400 bytes' build/rot-list-after.txt \
		|| { echo "verify-refuse: FAIL -- function 99 WITH the password did not truncate ROTQ.TXT"; exit 1; }
	@python3 tools/mkcpmfs.py --list build/rot-ctl-after.img | grep 'ROT' \
		> build/rot-ctl-after.txt
	@grep -qE '^ *0 ROTP\.TXT +6400 bytes' build/rot-ctl-after.txt \
		|| { echo "verify-refuse: FAIL -- the UNARMED drive also refused the truncate: this target would pass for a BDOS that simply never truncates"; exit 1; }
	@grep -qE '^ *0 ROTT\.TXT +25600 bytes' build/rot-ctl-after.txt \
		|| { echo "verify-refuse: FAIL -- the read-only attribute stopped protecting ROTT.TXT on the unarmed drive"; exit 1; }
	@# (d) neither image may grow an XFCB for the file function 103 was
	@# pointed at while the drive was read-only
	@test "`python3 tools/mkcpmfs.py --entries build/rot-arm-after.img | grep -c ' xfcb '`" = 3 \
		|| { echo "verify-refuse: FAIL -- the armed disk's XFCB count changed: function 103 wrote to a read-only drive"; exit 1; }
	@python3 tools/mkcpmfs.py --entries build/rot-arm-after.img | grep -q 'xfcb  ROTX' \
		&& { echo "verify-refuse: FAIL -- function 103 WROTE AN XFCB for ROTX.TXT on a read-only drive"; exit 1; } || true
	@python3 tools/mkcpmfs.py --entries build/rot-ctl-after.img | grep -q 'xfcb  ROTX' \
		&& { echo "verify-refuse: FAIL -- the control run's function 103 wrote an XFCB on a read-only drive"; exit 1; } || true
	@echo "verify-refuse: PASS -- in function 45 return-error mode a read-only file"
	@echo "        survived erase, rename and truncate; a password-protected file"
	@echo "        survived function 99 and was truncated once the password was"
	@echo "        given; a read-protected user-0 SYS file stayed shut from user 3"
	@echo "        without its password and opened with it; and function 103 wrote"
	@echo "        nothing onto a read-only drive"

# ---- the loader must not believe the file (verify-xout) ----
# P1 #4 and #5 of the first-release review, in src/bdos/pgmld.c.  Three
# malformed images, all derived from MHELLO.Z8K by tests/mkxout.py so that
# everything except the defect is the linker's own work:
#
#   BADSEG.Z8K    header segment count 17, one past the sixteen-element
#   HUGESEG.Z8K   header segment count 1000        seglim/segsiz/segloc/x_sg
#   TRUNCX.Z8K    last record dropped; the declared segment lengths run
#                 past the records that exist, code segment intact
#
# WHAT IS ASSERTED IS THE OBJECT, NOT THE RETURN CODE.  A refusal that
# still ran the program would satisfy a return-value check, so the
# transcript is asked instead how many times MHELLO's own greeting
# appears: TRUNCX is MHELLO, so if the loader accepts it the greeting
# appears a THIRD time (and its arguments do not, because there are
# none).  Two is the only right answer -- the two real MHELLO runs.
# The same count also proves the stale-TPA path did not run: MHELLO ONE
# leaves MHELLO's code in the TPA, and TRUNCX's missing records are
# exactly what a loader that strides over failed reads leaves behind.
#
# Then the session must go on and load GOOD programs: MHELLO TWO (0xEE01)
# after both refusals, ED (0xEE0B) and DDT (0xEE03), the two release-disk
# binaries a header check could plausibly break.  ED is gate-off (\g) from
# its command line on, as in verify-ed; DDT is last because its `-' prompt
# does not latch scripted input, and the run ends on console idle.
# The transcript cannot see the OTHER half of P1 #4 -- a bad count that is
# refused only AFTER its loop has written past the arrays looks exactly like
# a bad count that was bounded -- so the same pgmld.c is compiled for the
# host with the compiler's bounds checking on its globals and driven over
# the same malformed images.  tests/xouttest.c's header says why.
build/xouttest: tests/xouttest.c src/bdos/pgmld.c src/bdos/x.out.h \
		src/bdos/bdosdef.h src/bios/c900cfg.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -fsanitize=address -Isrc/bios -o $@ tests/xouttest.c

XOUTIMG	= build/xouttest.bin
XOUTLOG	= build/verify-xout.log
XOUTFMT	= $(OSSEL)MHELLO ONE\rBADSEG\rHUGESEG\rTRUNCX\rMHELLO TWO\rED XOUTT.TXT\r\\gi\rline from ED after the refusals\r\032e\rTYPE XOUTT.TXT\rDDT MHELLO.Z8K\r
.PHONY: verify-xout
verify-xout: all $(CPMAXOUT) build/xouttest
	./build/xouttest \
		|| { echo "verify-xout: FAIL -- the host run of pgmld() above either"; \
		     echo "             mis-handled a malformed image or wrote past the"; \
		     echo "             segment arrays (an address-sanitizer report is"; \
		     echo "             the overrun itself, not a test artefact)"; exit 1; }
	$(MKDISK) $(XOUTIMG) $(CPMSYS) $(CPMAXOUT)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XOUTIMG)) \
		--input="$$(printf '$(XOUTFMT)')" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } \
		| tee $(abspath $(XOUTLOG))
	@$(EMUOK)
	@test "`grep -c 'MWC hello' $(XOUTLOG)`" = 2 \
		|| { echo "verify-xout: FAIL -- MHELLO's greeting appears `grep -c 'MWC hello' $(XOUTLOG)` times, not 2:"; \
		     echo "             a malformed image was LOADED AND RUN (three times means"; \
		     echo "             TRUNCX ran; one means a good program stopped loading)."; exit 1; }
	@test "`grep -c 'File is not executable' $(XOUTLOG)`" = 2 \
		|| { echo "verify-xout: FAIL -- the two bad segment counts were not both"; \
		     echo "             refused with a reportable error (BADHDR)"; exit 1; }
	@test "`grep -c 'Read error on program load' $(XOUTLOG)`" = 1 \
		|| { echo "verify-xout: FAIL -- the truncated image was not refused with"; \
		     echo "             a read error the caller can report"; exit 1; }
	@grep -q 'arg 1: ONE' $(XOUTLOG) \
		|| { echo "verify-xout: FAIL -- the baseline MHELLO run is missing, so the"; \
		     echo "             greeting count proves nothing"; exit 1; }
	@grep -q 'arg 1: TWO' $(XOUTLOG) \
		|| { echo "verify-xout: FAIL -- a good 0xEE01 program no longer loads after"; \
		     echo "             the three refusals"; exit 1; }
	@grep -q 'line from ED after the refusals' $(XOUTLOG) \
		|| { echo "verify-xout: FAIL -- ED.Z8K (0xEE0B, on the release disk) did not"; \
		     echo "             load, run and write its file"; exit 1; }
	@grep -q 'Zilog portable debugger' $(XOUTLOG) \
		|| { echo "verify-xout: FAIL -- DDT.Z8K (0xEE03, on the release disk) did not load"; exit 1; }
	@# ---- the third finding: a child load must not move the PARENT's
	@# default DMA, and must leave the CHILD one of its own.  Its own run,
	@# because XDMA creates a second process and the run ends when both
	@# are done (no end mark: the CCP prompt comes back while the child is
	@# still going, which is verify-conc3's arrangement too).  What is
	@# asserted is where a disk read LANDED -- src/tests/xdma.c says why
	@# there is nothing else to ask.
	{ $(EMUCD) && ./c900 --disk=$(abspath $(XOUTIMG)) \
		--input="$(OSSEL)XDMA\r" --max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } \
		| tee -a $(abspath $(XOUTLOG))
	@$(EMUOK)
	@grep -q 'XDMA: parent start' $(XOUTLOG) \
		|| { echo "verify-xout: FAIL -- XDMA did not run"; exit 1; }
	@test "`grep -c 'fn13 DMA = own base page' $(XOUTLOG)`" = 2 \
		|| { echo "verify-xout: FAIL -- function 13 did not put BOTH processes'"; \
		     echo "             DMA back to their own base page buffer (the child's"; \
		     echo "             is the one a child load used to leave unset):"; \
		     grep 'XDMA:' $(XOUTLOG); exit 1; }
	@echo "verify-xout: PASS -- 17 and 1000 segments and a truncated image all"
	@echo "             refused with a reportable error and NOT run (greeting"
	@echo "             twice, from the two real MHELLO runs); ED and DDT still"
	@echo "             load; and a spawned child's function 13 aims at its own"
	@echo "             base page, its parent's at the parent's"

# ======================================================================
# F6 -- THE COMPATIBILITY LAYERS VALIDATED ONE RECORD AND THEN COPIED
# MANY.  Closes P1 #12 and the i86dec.c and z80load.c bullets of
# docs/cpm/docs/FIRST-RELEASE-REVIEW-2026-09-12.md in c900oses.
# ======================================================================

# ---- the two shims, with the compiler watching their guest memory ----
# `z80test' and `i86test' above run the same two suites, and they are the
# right place for what a return code can say.  THIS target is for what it
# cannot.
#
# The defect P1 #12 names is a write OUTSIDE the guest region: the seams
# checked a 128-byte DMA window -- one record -- and then let BDOS
# function 44's record count through to a native BDOS whose multio()
# (src/bdos/bdosrw.c:342) writes count * 128 bytes from that address.  A
# refusal and an acceptance come back through the same registers, so the
# suites assert it with a canary behind the guest's memory; and the
# review's own reproduction was an AddressSanitizer run, which is the
# other half -- the guest memory in both suites is an exact-size global
# (src/shim/tests/z80test.c gmem[0x10000], src/shim/tests/i86test.c bseg/cseg[65536]), so
# with -fsanitize=address a write one byte past it ABORTS the run and the
# abort is the assertion.  It is what caught src/shim/z80load.c's read of
# byte 256 of a 256-byte RSX image, which no return code reports at all.
#
# `timeout' is here for the third defect: src/shim/i86dec.c's prefix loop
# had no bound and its fetch wraps at sixteen bits, so a segment of
# prefix bytes hung inside one decode.  A hang is not an exit status.
# (src/shim/tests/i86test.c t_prefix() carries its own alarm too, so the failure
# is named rather than merely timed out.)
build/z80test-asan: src/shim/tests/z80test.c $(Z80SRC) src/shim/z80.h src/shim/gdpb.h src/shim/conmode.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -DHOSTCC -fsanitize=address -g -o $@ \
		src/shim/tests/z80test.c $(Z80SRC)
build/i86test-asan: src/shim/tests/i86test.c $(I86SRC) src/shim/i86.h src/shim/gdpb.h src/shim/conmode.h | $(OBJDIR)
	$(HOSTCC) -std=gnu89 -w -DHOSTCC -fsanitize=address -g -o $@ \
		src/shim/tests/i86test.c $(I86SRC)

.PHONY: verify-shim
verify-shim: build/z80test-asan build/i86test-asan $(Z80CORPUS)/SOURCES \
		$(I86CORPUS)/SOURCES $(I86FIX)/MANIFEST
	@timeout 300 ./build/z80test-asan $(Z80CORPUS) > build/verify-shim-z80.log 2>&1; \
	 rc=$$?; \
	 test $$rc = 0 \
		|| { echo "verify-shim: FAIL -- the CP/M-80 shim (rc $$rc; 124 means it"; \
		     echo "             never returned, 1 a failed check or an"; \
		     echo "             address-sanitizer report -- a report IS the"; \
		     echo "             out-of-bounds access, not a test artefact):"; \
		     grep -E '^FAIL|ERROR: AddressSanitizer|SUMMARY:' build/verify-shim-z80.log; \
		     exit 1; }
	@timeout 300 ./build/i86test-asan $(I86CORPUS) $(I86FIX) > build/verify-shim-i86.log 2>&1; \
	 rc=$$?; \
	 test $$rc = 0 \
		|| { echo "verify-shim: FAIL -- the CP/M-86 shim (rc $$rc; 124 means it"; \
		     echo "             never returned, 1 a failed check or an"; \
		     echo "             address-sanitizer report):"; \
		     grep -E '^FAIL|ERROR: AddressSanitizer|SUMMARY:' build/verify-shim-i86.log; \
		     exit 1; }
	@grep -q 'z80test: 1022 checks, 0 failures' build/verify-shim-z80.log \
		|| { echo "verify-shim: FAIL -- the CP/M-80 suite did not run all 1022 of its"; \
		     echo "             checks (`grep -o '[0-9]* checks, [0-9]* failures' build/verify-shim-z80.log`)."; \
		     echo "             A smaller passing run is not a pass."; exit 1; }
	@grep -q 'i86test: 1801 checks, 0 failures' build/verify-shim-i86.log \
		|| { echo "verify-shim: FAIL -- the CP/M-86 suite did not run all 1801 of its"; \
		     echo "             checks (`grep -o '[0-9]* checks, [0-9]* failures' build/verify-shim-i86.log`)."; exit 1; }
	@# The two instruction-count triples verify-z80 and verify-i86 gate on
	@# the TARGET are measured here on the HOST, and they are the reason
	@# this target reads two lines of a log at all: a DMA bound that
	@# changed the path DUMP or PIP takes would be a different program.
	@grep -q 'z80test: DUMP ran 14314 instructions, 605 BDOS calls, 164 flag' build/verify-shim-z80.log \
		|| { echo "verify-shim: FAIL -- DUMP no longer takes the 14,314/605/164 path"; \
		     echo "             verify-z80 gates on.  Read the divergence; do not"; \
		     echo "             relax this."; exit 1; }
	@grep -q 'i86test: PIP ran 9248 instructions, 58 BDOS calls' build/verify-shim-i86.log \
		|| { echo "verify-shim: FAIL -- PIP no longer takes the 9,248/58 path"; \
		     echo "             verify-i86 gates on."; exit 1; }
	@echo "verify-shim: PASS -- both shims, compiled with the guest region's"
	@echo "             bounds instrumented: a 2-record transfer from 0xff80"
	@echo "             wrote nothing above the region, a 256-byte RSX image"
	@echo "             was parsed without reading byte 256, a segment of"
	@echo "             prefix bytes decoded instead of hanging, and DUMP and"
	@echo "             PIP still take the paths verify-z80 and verify-i86 gate on"

# ---- verify-concr: TWO CREATORS, ONE FREE DESCRIPTOR, AND A YIELD IN
# ---- THE MIDDLE OF CREATION (F4, P1 #7) ----
#
# src/bdos/proc.c pcrgen() picks a free process descriptor, then calls
# plock() -- which PARKS the caller whenever another process holds the
# filesystem lock.  Until the slot was claimed before that yield, a second
# creator resuming in the window found it still PS_FREE and built its
# process in it, and the first creator came back and built its own on top.
#
# THE ARRANGEMENT is verify-conclk's plus a second creator, and it is
# described where the programs are (src/tests/concm.c).  CONCM creates CONCO
# while the lock is free, then parks at the one operator prompt that comes
# up UNDER the lock; CONCO waits for that, says so, and asks for a process
# -- parking inside pcrgen() with a descriptor picked.  The emulator holds
# the answer to the prompt (\i plus --input-mark) until CONCO says it is
# ready, which is the only way to put both creators in that window at
# once: paced input never arrives while two processes are parked at each
# other, and type-ahead would answer the prompt before CONCO had run.
#
# THE TWO CREATORS ASK FOR DIFFERENT PROGRAMS, and that is the object.
# CONCO asks for MHELLO.Z8K, CONCM for CONCB.Z8K.  With the descriptor
# reserved, exactly one request can be served -- CONCO's, which got there
# first -- and MHELLO's greeting is in the transcript while CONCB never
# runs at all.  Without it, CONCM is told 0 and CONCB runs in the slot
# CONCO had already picked.
#
# WHAT THIS TARGET DOES NOT SHOW, stated here because a reader would
# otherwise assume it: it does NOT discriminate the reservation.  Running it
# against a build with the PS_RSVD claim removed PASSES, and the reason is
# worth writing down.  The race needs BOTH creators to have picked the same
# free slot before either resumes, which needs a THIRD process to be the one
# holding the lock -- a lock holder plus two creators plus a slot to contend
# for, and the console-1 session (SESSION 1) besides, which is five live
# processes and could not be had from four descriptors.  Here CONCM is
# itself the lock holder, so
# when it releases the lock the dispatcher hands the machine to the parked
# creator at that call's own gate return (src/bdos/proc.c pdisp), CONCO
# completes, and CONCM's own request finds the slot LIVE rather than free.
# It is told 5 either way.
#
# So this is a concurrency regression test and not a proof of P1 #7: a
# creator parked inside pcrgen() resumes, gets ITS OWN program, the other is
# refused cleanly, nothing deadlocks, and no reserved slot is left behind to
# leak (a PS_RSVD that was never released would refuse the next create).
# docs/cpm/docs/run/F4.md records the reachability argument and what a test
# that did discriminate would need: three live processes and a spare
# descriptor, which is a one-console machine (no cold-boot session) or a
# larger PNPROC.  PNPROC IS 6 SINCE F14 AND THAT TEST NOW EXISTS:
# verify-concr2, at the end of this file, fails with the reservation removed.
# This one is kept as the regression it honestly is and is NOT weakened to
# overlap it.
CONCRIMG = build/concr.bin
CONCRLOG = build/verify-concr.log
# Four program loads, a handful of disk operations and a second-and-a-half
# wait; it ends on console idle well inside this.
CONCRMAX ?= 900000000
# THE ARRANGEMENT NEEDS EXACTLY ONE FREE DESCRIPTOR, and at PNPROC 4 that
# was implicit: the console-1 session, CONCM and CONCO were three of four.
# At 6 it has to be said out loud, so CONCM creates this many CONCR W
# ballast processes and prints the live count for the check below to assert.
# Nothing here is weakened by that -- the refusal being tested is the same
# refusal; it is the machine's capacity that moved (F14).
CONCRBALLAST = 2
CONCRLIVE = 5
.PHONY: verify-concr
verify-concr: all $(CPMACONCR)
	$(MKDISK) $(CONCRIMG) $(CPMSYS) $(CPMACONCR) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCRIMG)) \
		--input="$(OSSEL)$(SESS1)CONCM $(CONCRBALLAST)\r\iC" \
		--input-mark='CONCO: asking' \
		--max=$(CONCRMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCRLOG))
	@$(EMUOK)
	@# ---- the arrangement, which has to be true before anything else is
	@grep -q 'CONCM: O created' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- the second creator was never created"; exit 1; }
	@test "`tr -d '\r' < $(CONCRLOG) | sed -n 's/^CONCM: live before the race //p'`" \
		= "$(CONCRLIVE)" \
		|| { echo "verify-concr: FAIL -- there were not $(CONCRLIVE) live processes"; \
		     echo "              going into the race, so the number of FREE descriptors"; \
		     echo "              was not one and this program's own request below would"; \
		     echo "              be served on its merits rather than refused.  PNPROC"; \
		     echo "              (src/bdos/proc.h) or the cold-boot session count has"; \
		     echo "              moved: set CONCRBALLAST and CONCRLIVE to match."; exit 1; }
	@grep -q 'is read-only' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- close() never reached its operator prompt,"; \
		     echo "              so nothing was parked HOLDING the lock and no creator"; \
		     echo "              ever had to park inside pcrgen()"; exit 1; }
	@grep -q 'CONCO: asking' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- CONCO never asked for a process"; exit 1; }
	@grep -q 'CONCO: my request answered 0' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- the creator that got there FIRST was not"; \
		     echo "              served.  If it was answered 5 it had not yet parked"; \
		     echo "              when the prompt was answered, so the window this"; \
		     echo "              target is about never opened; anything else is in the"; \
		     echo "              transcript above."; exit 1; }
	@# ---- THE OBJECT: whose program ran ----
	@grep -q 'CONCB: B alive' $(CONCRLOG) \
		&& { echo "verify-concr: FAIL -- CONCB RAN.  The second creator picked the"; \
		     echo "              descriptor the parked one had already picked, so two"; \
		     echo "              processes were built in one slot (src/bdos/proc.c"; \
		     echo "              pcrgen: the claim must be written down before the"; \
		     echo "              yield in plock, not after the load)."; exit 1; } || true
	@grep -q 'MWC hello' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- MHELLO, the program the served creator asked"; \
		     echo "              for, never ran: its request was replaced by the other"; \
		     echo "              creator's"; exit 1; }
	@grep -q 'CONCM: my own request answered 5' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- the creator that arrived second was not told"; \
		     echo "              5 (no free process descriptor).  The only free slot"; \
		     echo "              belongs to the creator that was parked in plock()."; exit 1; }
	@# ---- and nothing deadlocked: both creators came back out
	@grep -q 'CONCO: O done' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- the parked creator never resumed"; exit 1; }
	@grep -q 'CONCM: M done' $(CONCRLOG) \
		|| { echo "verify-concr: FAIL -- the lock holder never finished"; exit 1; }
	@echo "verify-concr: PASS -- a creator parked inside process creation resumed"
	@echo "              with ITS OWN program, the second creator was refused"
	@echo "              cleanly, and the program IT asked for never ran"

# ---- verify-split: A REFUSED SECOND SPLIT-I/D PROGRAM MUST NOT HAVE
# ---- ALREADY OVERWRITTEN THE FIRST ONE'S BANKS (F4, P1 #6) ----
#
# A 0xEE0B program's code runs in its process's own 64 KB page, but its
# data address space is ONE fixed physical bank (src/bios/c900cfg.h
# SPLITDSEG) and its instruction patch table is ONE more (SPLITTSEG).
# Neither moves with a page swap.  That is why at most one split program
# may be live, and it is why the refusal of the second one has to come
# before the loader writes anything: src/bdos/proc.c pcrgen() used to ask
# after ldimage() had returned, so the caller got a correct 7 and the
# program already running had had its data image and its side table
# replaced underneath it.
#
# THE RETURN CODE IS THEREFORE NOT THE OBJECT.  It was 7 before the fix
# and it is 7 after it; what differs is whether the first split program's
# WORK survives.  So this target runs the same job twice --
#
#   control       SPLITB creates ASZ8K.Z8K as a background process to
#                 assemble STARTUP.8KN, and asks for nothing else;
#   interference  the same, and two seconds in, with the assembler well
#                 into its source, it asks for a second split program
#                 (SIZEZ8K.Z8K, also 0xEE0B).
#
# -- and compares STARTUP.OBJ, the assembler's output, byte for byte.
# Both runs must produce the same non-trivial file.  With the refusal
# after the load, the interference run's assembler takes a split trap on
# its own patched code and leaves a ZERO-LENGTH object behind, while still
# printing "refused 7, as it must be": that transcript is why this target
# reads the disk and not the log.
#
# The size floor is what stops two cut-off assemblies from passing as a
# match.  SPLITB stays alive until function 145 says the assembler is
# gone, for the same reason (src/tests/splitb.c).
# Both runs type SESSION 1 first: the `live=3' checked below counts the
# console-1 session, which a serial boot no longer starts by itself (D8).
SPLITCTL = build/split-ctl.bin
SPLITINT = build/split-int.bin
SPLITCLOG = build/verify-split-ctl.log
SPLITILOG = build/verify-split-int.log
# The assembly is minutes of emulated time; both runs end on console idle
# long before this, and a run that did not is a run to look at.
SPLITMAX ?= 2000000000
# STARTUP.OBJ is 896 bytes when the assembly completes.  The floor is well
# under that and well over a truncated file: it is a floor, not a value.
SPLITOBJMIN = 256
.PHONY: verify-split
verify-split: all $(CPMASPLIT)
	$(MKDISK) $(SPLITCTL) $(CPMSYS) $(CPMASPLIT) $(CPMBIMG)
	$(MKDISK) $(SPLITINT) $(CPMSYS) $(CPMASPLIT) $(CPMBIMG)
	@echo "--- 1. the control: one split program, nobody interfering"
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SPLITCTL)) \
		--input="$(OSSEL)$(SESS1)SPLITB 1\r" --max=$(SPLITMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } \
		| tee $(abspath $(SPLITCLOG))
	@$(EMUOK)
	@echo "--- 2. the same job, with a second split program asked for in the middle"
	{ $(EMUCD) && ./c900 --disk=$(abspath $(SPLITINT)) \
		--input="$(OSSEL)$(SESS1)SPLITB 2\r" --max=$(SPLITMAX) $(EMUIDLE) 2>/dev/null; \
		$(EMUSTAT); } \
		| tee $(abspath $(SPLITILOG))
	@$(EMUOK)
	@# the arrangement itself: both runs must have had an assembler
	@grep -q 'SPLITB: assembling, live=3' $(SPLITCLOG) \
		|| { echo "verify-split: FAIL -- the control run created no assembler process"; \
		     echo "              (function 144's answer is in the transcript above)"; exit 1; }
	@grep -q 'SPLITB: assembling, live=3' $(SPLITILOG) \
		|| { echo "verify-split: FAIL -- the interference run created no assembler process"; exit 1; }
	@grep -q 'SPLITB: assembler gone' $(SPLITCLOG) \
		|| { echo "verify-split: FAIL -- the control assembly never ended, so the"; \
		     echo "              comparison below would be between two unfinished files"; exit 1; }
	@grep -q 'SPLITB: assembler gone' $(SPLITILOG) \
		|| { echo "verify-split: FAIL -- the interference assembly never ended"; exit 1; }
	@# the refusal, which is necessary but is NOT the object
	@grep -q 'SPLITB: CREATED' $(SPLITILOG) \
		&& { echo "verify-split: FAIL -- a SECOND split-I/D program was created while"; \
		     echo "              one was running.  The data bank and the side table are"; \
		     echo "              single fixed pages (src/bios/c900cfg.h): both programs"; \
		     echo "              are now sharing one data address space."; exit 1; } || true
	@grep -q 'SPLITB: refused 7' $(SPLITILOG) \
		|| { echo "verify-split: FAIL -- the second split-I/D request was not refused"; \
		     echo "              with PC_SPLIT (7).  What it was told is in the"; \
		     echo "              transcript above."; exit 1; }
	@# ---- THE OBJECT: the first split program's output file ----
	dd if=$(SPLITCTL) of=build/split-ctl-cpma.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	dd if=$(SPLITINT) of=build/split-int-cpma.img bs=512 skip=$(CPMA_BASEBLK) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	rm -rf build/split-ctl-fs build/split-int-fs
	python3 tools/mkcpmfs.py --extract build/split-ctl-cpma.img build/split-ctl-fs
	python3 tools/mkcpmfs.py --extract build/split-int-cpma.img build/split-int-fs
	@test "`wc -c < build/split-ctl-fs/STARTUP.OBJ`" -ge $(SPLITOBJMIN) \
		|| { echo "verify-split: FAIL -- the CONTROL assembly produced `wc -c < build/split-ctl-fs/STARTUP.OBJ` bytes,"; \
		     echo "              under the $(SPLITOBJMIN)-byte floor: nothing was measured, because"; \
		     echo "              the undisturbed run did not finish its own job"; exit 1; }
	@test "`wc -c < build/split-int-fs/STARTUP.OBJ`" -ge $(SPLITOBJMIN) \
		|| { echo "verify-split: FAIL -- THE ASSEMBLER'S OUTPUT IS `wc -c < build/split-int-fs/STARTUP.OBJ` BYTES."; \
		     echo "              A second split-I/D program was refused, and the live one's"; \
		     echo "              data bank and side table went with the attempt: it died on"; \
		     echo "              its own patched code (look for a TRAP line above) with its"; \
		     echo "              object file unwritten.  The refusal has to happen before"; \
		     echo "              the loader writes the shared banks, not after"; \
		     echo "              (src/bdos/pgmld.c X_NXI_MAGIC, src/bdos/proc.c pcrgen)."; exit 1; }
	@cmp build/split-ctl-fs/STARTUP.OBJ build/split-int-fs/STARTUP.OBJ \
		|| { echo "verify-split: FAIL -- the assembly came out DIFFERENTLY when a second"; \
		     echo "              split-I/D program was asked for while it ran.  The refusal"; \
		     echo "              was correct and the shared banks were written anyway."; exit 1; }
	@echo "verify-split: PASS -- a second split-I/D program is refused before the"
	@echo "              loader touches the shared data bank or side table: the"
	@echo "              running one's assembly came out byte-identical to the"
	@echo "              undisturbed run (`wc -c < build/split-ctl-fs/STARTUP.OBJ` bytes)"

# ---- verify-repl: a good file is not thrown away for a copy that fails ----
#
# Three programs that replace a file used to delete or overwrite the old
# one BEFORE they could know a replacement existed.  Every assertion here
# is on the FILE, because that is what a user loses; a check on a program's
# exit code would pass against all three of the defects.
#
# THE MEDIUM IS THE INJECTION, for FCOPY and MSCOPY.  Drive B: keeps its
# whole directory (32 sectors: four 4096-byte allocation blocks) and THREE
# data blocks -- two hold BONLY.TXT and READMEB.TXT, the third is the one
# free block the copy gets -- and nothing after that, so the emulated
# controller answers 92h, drive not ready, for the fourth (bus.c, and the
# same arrangement verify-dirwerr uses).  BIG.TXT is 20 KB, five times that
# free block, so the copy cannot finish.  The scripted operator answers `C'
# (continue with bad data) once, which is the sharpest form of the failure:
# the write is refused and the program is told so.
#
# `write error on drive B' in the transcript is part of the assertion.
# Without it, "the destination survived" would be indistinguishable from
# "nothing was ever attempted", which is how this target could rot into
# proving nothing.
#
# THE PASSWORD IS THE INJECTION for PIP, and it needs no crippled medium.
# HELLO.TXT is given an XFCB in DELETE mode (20h) on a drive whose label
# carries the password bit; a read does not need the password, so PIP opens
# the source, writes its scratch file and closes it -- and then the delete
# of the old destination is refused (delete, rename and set-attributes are
# protected by ANY password, src/bdos/fileio.c).  PIP ignored the result of
# both that delete and the rename that follows it, so it returned as if the
# copy had happened, leaving the old contents in place and a .$$$ file
# beside them.  Now it says so, and the litter is gone.
REPLKEEP  = 56
REPLBIMG  = build/repl.bin
REPLBLOG  = build/verify-repl-b
REPLBDIR  = build/repl-b
REPLACPMA = build/cpma-repl.img
REPLAIMG  = build/repla.bin
REPLALOG  = build/verify-repl-a.log
REPLADIR  = build/repl-a
.PHONY: verify-repl
verify-repl: all $(CPMAIMG) $(CPMBIMG)
#	--- FCOPY and MSCOPY: the medium runs out under the copy
	rm -rf $(REPLBDIR); mkdir -p $(REPLBDIR)/before
	python3 tools/mkcpmfs.py --extract $(CPMBIMG) $(REPLBDIR)/before
	for n in 1 2; do \
		$(MKDISK) $(REPLBIMG) $(CPMSYS) $(CPMAIMG) $(CPMBIMG); \
		truncate -s $$(( ($(CPMB_BASEBLK) + $(REPLKEEP)) * 512 )) \
			$(REPLBIMG); \
		case $$n in \
		1) in='FCOPY A:BIG.TXT B:BONLY.TXT';; \
		2) in='MSCOPY A:BIG.TXT B:READMEB.TXT 16';; esac; \
		{ $(EMUCD) && ./c900 --disk=$(abspath $(REPLBIMG)) \
			--input="$(OSSEL)$$in\r\iC$(ENDIN)" \
			--input-mark="Continue with bad data" \
			--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(REPLBLOG))-$$n.log; $(EMUOK); \
		mkdir -p $(REPLBDIR)/after$$n; \
		dd if=$(REPLBIMG) of=$(REPLBDIR)/b-after$$n.img bs=512 \
			skip=$(CPMB_BASEBLK) status=none conv=sparse; \
		truncate -s $$(( $(CPMB_BLOCKS) * 512 )) \
			$(REPLBDIR)/b-after$$n.img; \
		python3 tools/mkcpmfs.py --extract $(REPLBDIR)/b-after$$n.img \
			$(REPLBDIR)/after$$n; done
	@for n in 1 2; do \
		grep -q 'write error on drive B' $(REPLBLOG)-$$n.log \
		|| { echo "verify-repl: FAIL -- run $$n saw no physical write error at"; \
		     echo "             all, so the truncation did not remove the block"; \
		     echo "             the copy needed and this run tested nothing"; \
		     exit 1; }; done
	@grep -q 'fcopy: write error' $(REPLBLOG)-1.log \
		|| { echo "verify-repl: FAIL -- FCOPY did not report the refused write"; exit 1; }
	@test "`grep -c 'fcopy: copied' $(REPLBLOG)-1.log`" = 0 \
		|| { echo "verify-repl: FAIL -- FCOPY reported a completed copy after a"; \
		     echo "             write the medium refused"; exit 1; }
	@grep -q 'mscopy: write error' $(REPLBLOG)-2.log \
		|| { echo "verify-repl: FAIL -- MSCOPY did not report the refused write"; exit 1; }
	@cmp $(REPLBDIR)/before/BONLY.TXT $(REPLBDIR)/after1/BONLY.TXT \
		|| { echo "verify-repl: FAIL -- B:BONLY.TXT is not what it was before"; \
		     echo "             FCOPY failed to copy over it.  The destination was"; \
		     echo "             deleted before the first record was written"; \
		     echo "             (src/cmd/fcopy.c)"; exit 1; }
	@cmp $(REPLBDIR)/before/READMEB.TXT $(REPLBDIR)/after2/READMEB.TXT \
		|| { echo "verify-repl: FAIL -- B:READMEB.TXT is not what it was before"; \
		     echo "             MSCOPY failed to copy over it (src/tests/mscopy.c)"; exit 1; }
	@for n in 1 2; do \
		test "`ls $(REPLBDIR)/after$$n | grep -c '[$$]'`" = 0 \
		|| { echo "verify-repl: FAIL -- run $$n left a scratch file on drive B:"; \
		     ls $(REPLBDIR)/after$$n; exit 1; }; done
#	--- PIP: the destination cannot be deleted, so the copy cannot happen
	cp $(CPMAIMG) $(REPLACPMA)
	python3 tools/mkcpmfs.py --label C900R --label-mode create,update,password \
		--xfcb HELLO.TXT:0x20:DSECRET --xfcb MHELLO.Z8K:0x20:DSECRET \
		$(REPLACPMA)
	@python3 tools/mkcpmfs.py --entries $(REPLACPMA) \
		| grep -q 'label C900R .*password' \
		|| { echo "verify-repl: FAIL -- the drive was not armed, so the delete"; \
		     echo "             below cannot be refused and this leg tests nothing"; exit 1; }
	rm -rf $(REPLADIR); mkdir -p $(REPLADIR)/before $(REPLADIR)/after
	python3 tools/mkcpmfs.py --extract $(REPLACPMA) $(REPLADIR)/before
	$(MKDISK) $(REPLAIMG) $(CPMSYS) $(REPLACPMA)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(REPLAIMG)) \
		--input="$(OSSEL)PIP HELLO.TXT=README.TXT\rDIR HELLO.*\r$(ENDIN)" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(REPLALOG))
	@$(EMUOK)
#	--- GENCOM: the program file it cannot replace must still be there.
#	The same armed drive, and the same refusal: GENCOM builds TEMP.$$$,
#	closes it, and is then refused the delete of the program file.  Its
#	die() takes TEMP.$$$ back out, which is right HERE -- the program is
#	still in place -- and wrong only in the window after the program has
#	been deleted, where the temporary is the only copy; that window is
#	dielast()'s, and no medium this suite can build makes the RENAME fail
#	on its own.
#
#	THIS RUN FOUND THE SHARPER HALF OF THAT BUG.  GENCOM compared the
#	BDOS gate's result with 255 outright, and the extended error code
#	comes back in the HIGH byte, so a delete refused for a password
#	(0FEFFh) was not 255, the rename that then failed because the file
#	was still there was not 255 either, and GENCOM printed `GENCOM
#	completed.' over a program it had not touched, leaving TEMP.$$$
#	behind.  Both tests now mask the low byte.  What is asserted is the
#	guarantee a user cares about: the refusal is reported, the program is
#	whole, and no half-built TEMP.$$$ is left holding a program's worth
#	of the disk.
	{ $(EMUCD) && ./c900 --disk=$(abspath $(REPLAIMG)) \
		--input="$(OSSEL)GENCOM MHELLO.Z8K PROT.RSX\rDIR TEMP.*\r$(ENDIN)" \
		--max=$(EMUMAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(REPLALOG))-gc
	@$(EMUOK)
	dd if=$(REPLAIMG) of=$(REPLADIR)/cpma.img bs=512 skip=$(CPMA_START) \
		count=$(CPMA_BLOCKS) status=none conv=sparse
	python3 tools/mkcpmfs.py --extract $(REPLADIR)/cpma.img $(REPLADIR)/after
	@grep -q 'ERROR' $(REPLALOG) \
		|| { echo "verify-repl: FAIL -- PIP said nothing.  It could neither delete"; \
		     echo "             the old destination nor rename its scratch file over"; \
		     echo "             it, and returned as though the copy had been made"; \
		     echo "             (src/cmd/pip.c closedest)"; exit 1; }
	@cmp $(REPLADIR)/before/HELLO.TXT $(REPLADIR)/after/HELLO.TXT \
		|| { echo "verify-repl: FAIL -- HELLO.TXT changed under a copy that could"; \
		     echo "             not complete"; exit 1; }
	@test "`ls $(REPLADIR)/after | grep -c '[$$]'`" = 0 \
		|| { echo "verify-repl: FAIL -- a scratch file was left on drive A: by a"; \
		     echo "             command that could not finish (PIP's .\$$\$$\$$ or"; \
		     echo "             GENCOM's TEMP.\$$\$$\$$):"; ls $(REPLADIR)/after; exit 1; }
	@grep -q 'gencom: cannot replace the program file' $(REPLALOG)-gc \
		|| { echo "verify-repl: FAIL -- GENCOM did not report being unable to"; \
		     echo "             replace the program file, so this leg proved nothing"; \
		     exit 1; }
	@cmp $(REPLADIR)/before/MHELLO.Z8K $(REPLADIR)/after/MHELLO.Z8K \
		|| { echo "verify-repl: FAIL -- MHELLO.Z8K changed or went away under a"; \
		     echo "             GENCOM that could not replace it (src/cmd/gencom.c)"; exit 1; }
	@test "`grep -c 'GENCOM completed' $(REPLALOG)-gc`" = 0 \
		|| { echo "verify-repl: FAIL -- GENCOM said it had completed after the"; \
		     echo "             medium refused both the delete of the program file"; \
		     echo "             and the rename over it.  The gate's result was"; \
		     echo "             compared with 255 without masking off the extended"; \
		     echo "             error code in the high byte (src/cmd/gencom.c)"; exit 1; }
	@echo "verify-repl: PASS -- a refused write left B:BONLY.TXT and"
	@echo "             B:READMEB.TXT exactly as they were and no scratch file"
	@echo "             behind, a destination PIP could not delete was reported"
	@echo "             instead of being reported as copied, and a GENCOM that"
	@echo "             could not replace its program file left it whole"

# ---- src/app under malformed input ----
# verify-a3, verify-sdb and verify-zcc run these same programs on good
# input.  This target runs them on BAD input, and it runs them
# ON THE HOST for the reason tests/appbound.sh's header gives at length:
# every defect it covers is a write past the end of a buffer, and on the
# Z8001 such a write lands in whatever is next in the TPA while the command
# still reports success.  A return code cannot see it.  So the sources are
# compiled here with -fsanitize=address, where the compiler watches the
# arrays and a write one element past one ABORTS the run -- the same
# instrument as tests/xouttest.c and the -fsanitize=address half of
# verify-shim, and the same reasoning.
#
# It covers the review's P1 #17 (FROMHEX's EOF-less semicolon search and its
# store-before-the-limit), #18 (CMD.C get_aname's unbounded form-attribute
# name), #19 (SORTFL's line and pointer arrays, KILLDU's two line arrays)
# and the INT.C, IO.C and IEX.C items in its P2 list.  Fourteen checks
# failed against the sources as shipped.
#
# THE SOURCES ARE NOT MODIFIED TO GET THEM THROUGH A MODERN COMPILER, with
# two exceptions that are command-line only.  -Dstatic= is there because
# ZCC1 accepted a call to a `static' function written before its definition
# and gcc makes that a hard error in forty places; dropping `static' for the
# host build is smaller and safer than editing forty declarations, and the
# only function-scope statics in src/app are inside `#ifdef Lattice'.
# -Dexit=appexit is for CMD.C:50's argument-less exit(), which a prototyped
# stdlib refuses.  tests/appshim.c holds that, plus openb/creatb/fopenb --
# SDBIO.H defines CPM68K, so the branches compiled here are the ones the
# shipped Z8001 binaries take.
APPBDIR	= build/appbound
APPBSRCD = $(APPBDIR)/src
APPBSDB	= $(APPBSRCD)/cmd.c $(APPBSRCD)/com.c $(APPBSRCD)/cre.c \
	  $(APPBSRCD)/err.c $(APPBSRCD)/iex.c $(APPBSRCD)/int.c \
	  $(APPBSRCD)/io.c $(APPBSRCD)/junk.c $(APPBSRCD)/mth.c \
	  $(APPBSRCD)/scn.c $(APPBSRCD)/sdb.c $(APPBSRCD)/sel.c \
	  $(APPBSRCD)/srt.c $(APPBSRCD)/tbl.c
APPBCC	= $(HOSTCC) -std=gnu89 -w -fsanitize=address -g

# The guest sources are named in upper case and include their headers in
# lower ("sdbio.h"), which the 1984 CP/M file system did not distinguish and
# this one does.  Copy them down rather than rename anything shipped.
$(APPBDIR)/src.stamp: $(wildcard src/app/*.C) src/app/SDB.H src/app/SDBIO.H
	@mkdir -p $(APPBSRCD)
	@for f in src/app/*.C src/app/*.H; do \
		cp $$f $(APPBSRCD)/`basename $$f | tr 'A-Z' 'a-z'`; done
	@touch $@

$(APPBDIR)/appshim.o: tests/appshim.c | $(OBJDIR)
	@mkdir -p $(APPBDIR)
	$(APPBCC) -c -o $@ tests/appshim.c

$(APPBDIR)/fromhex: $(APPBDIR)/src.stamp $(APPBDIR)/appshim.o
	$(APPBCC) -Dabort=appabort -o $@ $(APPBSRCD)/fromhex.c \
		$(APPBDIR)/appshim.o
$(APPBDIR)/sortfl: $(APPBDIR)/src.stamp
	$(APPBCC) -o $@ $(APPBSRCD)/sortfl.c
$(APPBDIR)/killdu: $(APPBDIR)/src.stamp
	$(APPBCC) -o $@ $(APPBSRCD)/killdu.c
$(APPBDIR)/sdb: $(APPBDIR)/src.stamp $(APPBDIR)/appshim.o
	$(APPBCC) -Dstatic= -Dexit=appexit -o $@ $(APPBSDB) \
		$(APPBDIR)/appshim.o

.PHONY: verify-appbound
verify-appbound: $(APPBDIR)/fromhex $(APPBDIR)/sortfl $(APPBDIR)/killdu \
		$(APPBDIR)/sdb
	python3 tests/appbound.py $(APPBDIR)
	@sh tests/appbound.sh $(APPBDIR) \
		|| { echo "verify-appbound: FAIL -- see the checks above.  An"; \
		     echo "                 AddressSanitizer report IS the"; \
		     echo "                 out-of-bounds access, not a test"; \
		     echo "                 artefact; rc 124 IS the hang."; \
		     exit 1; }
	@echo "verify-appbound: PASS -- the seven unbounded inputs of the review's"
	@echo "                 P1 #17, #18, #19 and its INT.C, IO.C and IEX.C"
	@echo "                 items, each driven by the input that reached it,"
	@echo "                 with the arrays instrumented and every"
	@echo "                 well-formed control still right"

# ---- verify-concr2: THE SAME RACE AS verify-concr, ARRANGED SO THAT THE
# ---- RESERVATION IS THE ONLY THING THAT DECIDES IT (F14, for F4's P1 #7) ----
#
# verify-concr is an honest concurrency regression and it stays one, but it
# does NOT discriminate the fix: run it against a build with the PS_RSVD
# claim removed and it still passes.  F4 measured that and wrote down why
# (docs/cpm/docs/run/F4.md).  CONCM is the lock holder AND the second
# creator, so when its close() releases the lock the dispatcher hands the
# machine to the parked creator at that call's own gate return -- the parked
# creator COMPLETES before CONCM has even searched for a descriptor, and
# CONCM is told 5 either way.
#
# The race needs BOTH creators to have picked the same free slot before
# either resumes, so the lock holder has to be a THIRD process: a holder,
# two creators, ballast enough that exactly ONE descriptor is free, and the
# console-1 session (SESSION 1, typed first) that holds one for as long as the machine
# is up.  That is five live processes and a spare, which is why this target
# could not exist at PNPROC 4 and can at 6 (src/bdos/proc.h).
#
# THE ARRANGEMENT is in src/tests/concl.c at length.  In short: CONCL creates
# CONCR A and CONCR B and one CONCR Z of ballast while the lock is free,
# PRINTS the live count (BDOS function 145) so the transcript says what the
# arrangement actually was, and then parks at the read-only close prompt
# HOLDING the lock and creates nothing more.  A asks first and parks inside
# pcrgen(); B asks second and parks too; and only then does the BALLAST
# print the line that is the --input-mark releasing the answer to CONCL's
# prompt.
#
# THE RELEASE HAS TO COME FROM THE BALLAST, and the first version of this
# target got that wrong in a way worth recording, because it is F4's dead
# end wearing different clothes.  With the mark on B's own `asking' line the
# target PASSED with PS_RSVD removed: src/bdos/bdosglue.s tests `psched' at
# the SC return and calls pdisp_ unconditionally, so B lost the machine at
# that very print's gate return, the holder took the answer that had just
# arrived, released the lock, and the creator already parked COMPLETED --
# all before B's own descriptor search had run.  B was then told 5 either
# way.  A fourth process that is not racing decouples the release from the
# racers, and the order check below is what asserts it happened.
#
# THE VERDICT IS THE TWO ANSWERS, and it cannot be reached by accident:
#
#   with the reservation   A is answered 0 and B is answered 5.  B's search
#                          skips A's PS_RSVD slot, finds nothing free and
#                          refuses without reaching the lock at all.  One
#                          MHELLO runs and it carries A's tail, QA.
#   without it             A is answered 0 and B is answered 0.  Both
#                          picked the same slot and both built a process in
#                          it, so one child is simply gone and its 64 KB
#                          page is leaked.
#
# TWO SUCCESSFUL CREATES OUT OF ONE FREE DESCRIPTOR IS THE ASSERTION, and no
# scheduling order can produce it: if the window had been missed the loser
# would be refused 5 (the slot is LIVE, not free), which is the passing
# answer.  So this target fails only when the bug is present -- measured,
# not assumed: rebuilt with `kid->pd_state = PS_RSVD' removed it reports
# `BOTH creators were served' and exits 1.
CONCR2IMG = build/concr2.bin
CONCR2LOG = build/verify-concr2.log
# Five process loads, a handful of disk operations and three one-sided
# waits; the run ends on console idle well inside this.
CONCR2MAX ?= 900000000
# One ballast process, which is what leaves exactly one descriptor free at
# PNPROC 6: the console-1 session, CONCL, CONCR A, CONCR B and the ballast
# are five of six.  CONCL prints the count and the check below asserts it,
# so if PNPROC moves again this target says so instead of quietly measuring
# nothing.
CONCR2BALLAST = 1
CONCR2LIVE = 5
.PHONY: verify-concr2
verify-concr2: all $(CPMACONCR2)
	$(MKDISK) $(CONCR2IMG) $(CPMSYS) $(CPMACONCR2) $(CPMBIMG)
	{ $(EMUCD) && ./c900 --disk=$(abspath $(CONCR2IMG)) \
		--input="$(OSSEL)$(SESS1)CONCL $(CONCR2BALLAST)\r\iC" \
		--input-mark='CONCR Z: releasing the prompt now' \
		--max=$(CONCR2MAX) $(EMUIDLE) 2>/dev/null; $(EMUSTAT); } \
		| tee $(abspath $(CONCR2LOG))
	@$(EMUOK)
	@# ---- the arrangement, which has to be true before anything else is
	@grep -q 'CONCR A: alive' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the first creator was never created"; exit 1; }
	@grep -q 'CONCR B: alive' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the second creator was never created"; exit 1; }
	@test "`tr -d '\r' < $(CONCR2LOG) | sed -n 's/^CONCL: live before the race //p'`" \
		= "$(CONCR2LIVE)" \
		|| { echo "verify-concr2: FAIL -- there were not $(CONCR2LIVE) live processes"; \
		     echo "               going into the race, so the number of FREE"; \
		     echo "               descriptors was not one and the two creators never"; \
		     echo "               contended for the same slot.  PNPROC (src/bdos/proc.h)"; \
		     echo "               or the cold-boot session count has moved: set"; \
		     echo "               CONCR2BALLAST and CONCR2LIVE to match."; exit 1; }
	@grep -q 'is read-only' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- close() never reached its operator prompt,"; \
		     echo "               so nothing was parked HOLDING the lock"; exit 1; }
	@grep -q 'CONCR A: asking' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the first creator never asked"; exit 1; }
	@grep -q 'CONCR B: asking' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the second creator never asked, so the"; \
		     echo "               prompt was never released and the run proved nothing"; exit 1; }
	@# THE WINDOW, AND THIS IS THE CHECK THAT SAYS IT WAS OPEN.  The
	@# transcript must read: prompt reached, A asks, B asks, and only THEN
	@# the ballast releases the answer.  Between B's ask and that release
	@# nothing can have taken the lock from the holder, so both creators
	@# had searched -- and, without the reservation, both had picked the
	@# same slot -- before either of them could resume.
	@p=`tr -d '\r' < $(CONCR2LOG) | grep -an 'is read-only' | head -1 | cut -d: -f1`; \
	a=`tr -d '\r' < $(CONCR2LOG) | grep -an 'CONCR A: asking' | head -1 | cut -d: -f1`; \
	b=`tr -d '\r' < $(CONCR2LOG) | grep -an 'CONCR B: asking' | head -1 | cut -d: -f1`; \
	z=`tr -d '\r' < $(CONCR2LOG) | grep -an 'CONCR Z: releasing' | head -1 | cut -d: -f1`; \
	test -n "$$p" -a -n "$$a" -a -n "$$b" -a -n "$$z" \
	     -a "$$p" -lt "$$a" -a "$$a" -lt "$$b" -a "$$b" -lt "$$z" \
		|| { echo "verify-concr2: FAIL -- the transcript order is not prompt ($$p),"; \
		     echo "               A asks ($$a), B asks ($$b), ballast releases ($$z)."; \
		     echo "               The lock was not still held by the third process"; \
		     echo "               when the second creator searched, so the window"; \
		     echo "               this target is about never opened."; exit 1; }
	@# ---- THE OBJECT: one free descriptor cannot serve two creators ----
	@test "`grep -c 'CONCR .: answered 0' $(CONCR2LOG)`" = 1 \
		|| { echo "verify-concr2: FAIL -- BOTH creators were served out of ONE free"; \
		     echo "               descriptor.  The second one picked the slot the"; \
		     echo "               first had already picked and parked in, so two"; \
		     echo "               processes were built in one descriptor and one of"; \
		     echo "               them, with its 64 KB page, is gone.  The claim must"; \
		     echo "               be written down BEFORE the yield in plock()"; \
		     echo "               (src/bdos/proc.c pcrgen, PS_RSVD)."; exit 1; }
	@grep -q 'CONCR A: answered 0' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the creator that was already PARKED in"; \
		     echo "               plock() with a descriptor picked was not the one"; \
		     echo "               served"; exit 1; }
	@grep -q 'CONCR B: answered 5' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the creator that arrived second was not"; \
		     echo "               told 5 (no free process descriptor): the only free"; \
		     echo "               slot was already claimed by the parked creator"; exit 1; }
	@# ---- and the served request was the served creator's own ----
	@grep -q 'arg 1: QA' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- MHELLO did not run with the parked"; \
		     echo "               creator's own command tail"; exit 1; }
	@grep -q 'arg 1: QB' $(CONCR2LOG) \
		&& { echo "verify-concr2: FAIL -- the refused creator's request was loaded"; \
		     echo "               anyway: the resident pcreq buffer is shared by every"; \
		     echo "               creator and the copy into it must be inside the"; \
		     echo "               lock (src/bdos/proc.c pcrgen)"; exit 1; } || true
	@# ---- nothing deadlocked and no reservation leaked ----
	@grep -q 'CONCR A: done' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the parked creator never resumed"; exit 1; }
	@grep -q 'CONCR B: done' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the refused creator never came back out"; exit 1; }
	@grep -q 'CONCL: a create after the race answered 0' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- a create after the race was refused, so a"; \
		     echo "               PS_RSVD slot was claimed and never given back.  A"; \
		     echo "               reserved slot is invisible to every other loop in"; \
		     echo "               proc.c, so a leaked one is lost for good."; exit 1; }
	@grep -q 'CONCL: done' $(CONCR2LOG) \
		|| { echo "verify-concr2: FAIL -- the lock holder never finished"; exit 1; }
	@echo "verify-concr2: PASS -- with a third process holding the lock, five"
	@echo "               processes live and ONE descriptor free, the creator"
	@echo "               parked inside pcrgen() kept its slot and its own"
	@echo "               request, the second creator was refused 5 before it"
	@echo "               reached the lock, and nothing was left reserved"

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

# ---- verifyprep: what the targets above share ----
# Every file prerequisite of every verify target: the fixture images, the
# media and the host-side tools.  verify-all builds them here, once, before
# it starts running targets concurrently -- two makes that each built
# build/cpma-conc.img would be writing the same file at the same time.
#
# Read from this file rather than listed, for the reason verify-all's own
# target list is: a target added above is prepared without being registered
# here.  The first sed joins continued prerequisite lines; the $(eval)
# expands the variable names the list is written in.  verify-zcc is left out
# for the reason verify-all leaves it out: its inputs are the twenty-minute
# ZCC rebuild, and `make verify-zcc' is where that belongs.  The name has no
# hyphen after `verify', so verify-all's enumeration does not take it for a
# test.
VERIFYPREPRAW := $(shell sed -e :a -e '/\\$$/N; s/\\\n//; ta' tests/verify.mk \
	| grep -Ev '^verify-(all|zcc):' \
	| sed -n 's/^verify-[a-z0-9-]*:\(.*\)/\1/p')
$(eval VERIFYPREP := $(VERIFYPREPRAW))
VERIFYPREP := $(filter-out all verify-%,$(sort $(VERIFYPREP)))
.PHONY: verifyprep
verifyprep: $(VERIFYPREP)
