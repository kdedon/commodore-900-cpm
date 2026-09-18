# `tests/` — the things that produce verdicts

Everything here answers a question about a build; nothing here is part of
one. The dividing line with [`../tools/`](../tools/README.md) is **produce
versus assert**.

One of these, `mdcheck.sh`, runs over the packed images from the
`imagecheck` target, because a shipped disk that cites a document the
machine has not got is not worth shipping. Everything else is reached from
a `verify-*`, `splitcheck`, `splittest`, `i86test` or `dirfmt-check` target;
`make verify-all` runs every `verify-*` target and prints a PASS/FAIL line
per target. `VERIFYJOBS` of them run at a time (four by default, one for the
old one-after-another suite); `verifyrun.sh` is the runner and its header
says what makes running several at once safe. It skips `verify-zcc`, which rebuilds the
`src/app` programs on the machine with DRI's `ZCC` and takes twenty
minutes on its own; run that one by name before a release.

## The three kinds

**Assertions over an emulator transcript.** `verifychk.sh`, `bannerchk.sh`,
`signoncount.py`, `selfhostchk.sh`, `bootgate.sh`, `vt.py` (renders a
transcript as a screen and asserts on cells), `tstamp.py` (per-line wall
clock, for phase timings).

**Independent oracles over the bytes.** `dirattr.py`, `dirfmt-test.py`,
`driveb-check.py`, `initdir-check.py`. These re-derive host-side what the
target claimed, so a wrong answer has to be wrong twice, in two languages,
to pass.

**Hostile input to a host tool.** `extract-test.py` (`verify-extract`) builds
directory entries no well-behaved CP/M would write and runs
`../tools/mkcpmfs.py --extract` on them: the eleven name bytes of a directory
entry are untrusted input and `--extract` turns them into a host path, so an
entry named `../OUT.TXT` is a path traversal. It judges the host disk rather
than the tool's exit status — the traversal case asserts that the file outside
the destination still holds what it held, because the overwrite this closes
was silent and exited 0.

**Host builds of target code.** `rtctest.c` + `rtcchip.c`/`.h` + `rtcinc/`
run the real `src/bios/rtc900.c` and `src/cmd/date.c` against a simulated
MSM58321; `splitchk.c` and `splittest.c` exercise the split-I/D shim.
`crsrtest.c` covers the cursor layer. `i86test.c` runs the CP/M-86 shim's
8086 decoder, executor and `.CMD` loader (`src/cmd/i86*.c`), and is the
only one of these written *before* the target half exists rather than
after — `CPM86-STAGE-ONE.md` §5.1 is the argument for that ordering. It
loads real `.CMD` files from `i86corpus/` and generated ones from
`build/cmdfix/` (`../tools/mkcmdfix.py`), which is where the headers no
real file contains live. Its `-c` mode sweeps any `.CMD` file by hand.

**Mutation drivers — the gates on the gates.** `bannermutate.sh`,
`rtcmutate.sh`, `initdir-mutate.sh`. Each deliberately breaks something a
verify target claims to check and requires that target to *fail*. A check
that has never been seen failing is not known to be a check; this tree has
already found seven that were not (among them: `verify`/`reverify` had no
`grep`/`test`/`cmp` in their recipes and passed even when the emulator
crashed before CP/M loaded; `verify-trunc`/`verify-truncb` could not tell a
record-based extent computation from a disk-map-based one because both
happen to agree on dense files; and an `INITDIR` fixture with nothing in a
fourth directory slot relocated zero entries and reported every count as
correct). Read the header of `initdir-mutate.sh`
before writing another: it explains why a mutation driver and its pristine
copies must live inside the worktree, which cost a day to learn.

## bootgate.sh — did the medium still boot CP/M, and whose fault if not

```
bootgate.sh LOG
```

Walks the boot chain ROM -> kboot -> `cpm.sys` -> BDOS -> CCP over an emulator
transcript and names the **first** stage that did not happen, distinguishing a
loader fault from a CP/M fault in the message.  Exit 0 = booted.  Used by
`verify-boot`; proven able to fail by `verify-bootgate`.
