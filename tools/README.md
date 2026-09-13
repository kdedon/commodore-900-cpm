# `tools/` — the things that produce bytes

Host-side (Linux/Python 3). These build the medium, the drive images and
the `.CPM` program containers. No target toolchain involved. The disk
geometry they pack to and the x.out program-load format they target are
each described in the matching section below (`mkcpmfs.py`, `lout2cpm`).

The dividing line with [`../tests/`](../tests/README.md) is **produce
versus assert**: if it emits an artifact the build or a fixture needs, it
is here; if its output is a verdict, it is there. `mkcpmfs.py` is the
honest edge case — it packs images *and* serves as the independent oracle
that reads them back — and it lives here because `make all` cannot run
without it.

Contents: `mkcpmfs.py`, `mkcpmdisk.py`, `cohfs.py`, `mkrsx.py`,
`mkblob.py`, `mksig.py`, `stage-devpack.sh`, and the four fixture
builders `ccpuser.py`, `setbfill.py`, `u0fill.py` and `mkcmdfix.py` (the
CP/M-86 `.CMD` headers `make i86test` needs and no real file contains — a
nonzero A-Base, an oversized group, a malformed file).

## mkcpmfs.py — CP/M 2.2 filesystem packer / reader

```
mkcpmfs.py <img> <blocks> <srcdir>    # pack srcdir -> raw image (blocks=20480 for cpma)
mkcpmfs.py --list <img>               # directory listing
mkcpmfs.py --extract <img> <destdir>  # extract all files (independent reader path)
```

Fixed geometry, which must match the BIOS DPB (plan D4): 128-byte records,
4 per 512-byte physical sector, BLS=4096 (BSH=5 BLM=31 EXM=1), DSM=2559,
DRM=511 (directory = first 4 allocation blocks, data starts at block 4),
skew=0, OFF=0, CKS=0. Output is deterministic: sorted file order, no
timestamps, free dir entries 0xE5-filled, free data zeroed.

Extraction is record-granular (CP/M stores sizes in 128-byte records):
non-multiple-of-128 files come back padded to the next record.

EOF padding is extension-based (implemented in `is_text()`/`TEXT_EXTS`):
files with a text extension (`.TXT .C .H .SUB .PD .8KN .S .ASM .DOC .MAN`)
get their final partial record filled with 0x1A (^Z), the classic CP/M
text-EOF convention — `TYPE`/`ED`/`PIP` stop at ^Z instead of printing
trailing NULs.  All other extensions are binary and stay zero-padded
(program loading uses the x.out header sizes, so pad bytes are inert).
A heuristic rather than a `--textpad` flag keeps the image a pure
function of the staging directory (deterministic, no Makefile plumbing).

Extent semantics (validated against the consumer, `ref/may83/bdos/`):

- AL = eight 16-bit block numbers, **little-endian** on disk. Decider:
  `fileio.c alloc()` / `bdosrw.c blknum()` pass each word through the
  byte-swapper `swap()` before use on the big-endian Z8000.
- One entry = 8 blocks = 32 KB = 2 logical extents. Entry *k* carries
  `ext_total = (S2<<5)|EX` = `2k` (≤ 16 KB used in the entry) or `2k+1`;
  RC = records in the **last** logical extent, 0x80 = full.
- A 16 KB-aligned chunk gets **EX=2k, RC=0x80** (not EX=2k+1, RC=0).
  Decider: `bdosrw.c calcext()` (lines 174–194) derives the sub-extent
  from the last non-zero AL slot (4 blocks → +0), and `get_rc()` (203–218)
  returns 0 for any extent *past* calcext — so EX=2k+1/RC=0 would make
  every record of that extent read as EOF. EX=2k/RC=0x80 is also what the
  BDOS's own close path writes.
- Empty file: one entry, EX=0, RC=0, AL all zero.
- User byte: 0 for files, 0xE5 free; bytes ≥ 0x10 are skipped as
  MP/M-XFCBs by `fileio.c alloc()` — never written by the packer.

Validation (cpmtools was not installable — no sudo; self-validated):
awkward-size matrix 0/1/127/128/129/4095/4096/4097/16383/16384/16385/
32767/32768/32769/40000/102400 bytes packs, `--list`s and `--extract`s
byte-identically (mod record padding), **and** is read back correctly by a
transliteration of the may83 BDOS sequential-read algorithm
(dirscan/match/blkindx/new_ext/calcext/get_rc). Repacking is byte-identical.

## lout2cpm — l.out → x.out container (C, supersedes lout2cpm.py)

```
lout2cpm <input.lout> <output.Z8K>
```

Source: `$(C900_TOOLCHAIN)/tools/lout2cpm/lout2cpm.c` (see `LOUT2CPMSRC`
in the Makefile, which compiles it on demand into `build/lout2cpm`; the
toolchain repository's own tests cover both containers).
Parses our linked ld-z8001 l.out (magic 0x0107, LE fields, PDP-order
longs, 48-byte header) and wraps text/data verbatim in an x.out:
big-endian `x_hdr` + `x_sg` COD/DAT/BSS + payload, `x_reloc = x_symb = 0`.
Container choice:

- entry segment != 0 → **0xEE01 segmented** (`x_sg_no` = the link
  segment).  This is the MWC-transient-program path (Option 3): programs
  link flat at the TPA segment base (`ld -R 0x32000000 -e start`,
  `user/crt0.o` first) and pgmld's segmented path places each file
  segment at `x_sg_no<<24`.
- entry segment == 0 → **0xEE03 non-segmented** (genuinely nonseg code
  linked at 0 only; MWC output is segmented and never qualifies).

**No relocation records:** `pgmld.c` performs no relocation, and none is
needed — 0xEE03 placement is positional (the TPA mapping supplies the
base), 0xEE01 placement is at the absolute link segment.  The tool
rejects an entry that is not at segment offset 0 (pgmld enters at the
first text byte) and anything that cannot fit one 64K segment together
with the base page + DEFSTACK.

### What the may83 loader accepts (`ref/may83/bdos/pgmld.c`) — drives M4

- **Magics** (switch at pgmld.c:154): `X_NXN_MAGIC 0xEE03` (non-seg,
  combined I/D), `X_NXI_MAGIC 0xEE0B` (non-seg, split I/D),
  `X_SX_MAGIC 0xEE01` (segmented, combined). Anything else → BADHDR.
  Shared (0xEE07) is NOT accepted.
- **File layout consumed**: 16-byte `x_hdr`, then `x_nseg` × 4-byte
  `x_sg` headers (max NSEG=16), then segment payloads **sequentially in
  file order**. `x_init/x_reloc/x_symb` are never read — the loader stops
  after the last segment's bytes; trailing reloc/symbol sections are
  simply ignored.
- **Segment types**: COD/MXU/MXP → text; CON/DAT → data; BSS → space
  only (cleared by the user program's startup, not the loader); STK →
  space reserved below the base page. Each `x_sg_len` is 16-bit, so no
  section piece over 65535 bytes.
- **Placement**: non-segmented → all segments into one region of the MRT
  (BIOS fn 18): text at TPA offset 0, data after it (same region;
  separate region pair for split I/D). Segmented → physical address
  `x_sg_no << 24`, i.e. the x.out segment number IS the CPU segment —
  only usable once the BIOS maps those segments.
- **No relocation records are processed.** The "relocation pass" that
  makes stock binaries work is positional: non-segmented programs are
  linked at 0 and get their base implicitly from the TPA segment mapping
  (16-bit addresses, segment register supplies the rest). All five stock
  DRI utilities we stage are 0xEE03 with `x_reloc=0`.
- **Base page + stack**: loader builds the `struct b_page` (basepage.h)
  at top-of-TPA minus DEFSTACK(0x100)+basepage, pushes a fake return
  address (warm-boot) + basepage pointer, returns LPB with pgldaddr =
  actual text start (entry point), bpaddr, stackptr, flags (SPLIT/SEG).
- **True relocatable MWC output (open M4 question)**: to load an MWC
  program anywhere other than TPA offset 0 we would have to emit
  `X_RL_OFF/X_RL_SSG/X_RL_LSG` records from `ld -r` output *and* add a
  relocation pass to pgmld — the may83 loader has none to "keep". Since
  the TPA is one fixed 64 K segment (D2), linking user programs at 0 is
  sufficient and relocation records stay unnecessary.

## Staging directory: `build/diska/`

Drive-A: content packed into `build/cpma.img`.  The tracked fixtures are in
`src/dist/disk-a/`; the Makefile copies them into `build/diska/` and everything
else there is staged by `stage-devpack.sh` (below) or copied in from the
transient build.  Staging into `src/` is what this tree used to do, and it put
build output and vendor bytes among the sources.

**Naming**: the CCP finds executables by extension **`.Z8K`** (then blank
type, then `.SUB` — `ref/may83/ccp/ccp.c cmd_file()`), NEVER `.CPM`.  All
staged executables are `NAME.Z8K`; the script deletes stale `.CPM` copies.

## stage-devpack.sh — M5 dev-pack staging (idempotent)

```
sh host/stage-devpack.sh [basedir [diskadir]]
```

Copies the full DRI dev pack from `z8001mb/cpm8k/packages/base/` into
`disk-a/` under CP/M 8.3 UPPERCASE names, bytes verbatim (utilities
`STAT DUMP DDT ED PIP AR8K NMZ8K SIZEZ8K XCON XDUMP` as `.Z8K`; compiler
`ZCC ZCC1 ZCC2 ZCC3 ASZ8K LD8K` as `.Z8K` + `ASZ8K.PD`; runtime
`LIBCPM.A STARTUP.O STARTUP.8KN`; all 11 headers as `*.H`), and writes the
on-target K&R test source `HELLO.C`.  Files are only rewritten when
content differs (`cmp`), so re-runs are mtime-stable and do not force an
image repack.  This pack is what an on-target self-hosting session runs
against: `HELLO.C` compiled by the staged `ZCC` (which chains
`ZCC1`/`ZCC2`/`ZCC3` by name) and linked by `LD8K` against `STARTUP.O`
and `LIBCPM.A`.

Nothing staged is tracked: staging goes to `build/diska/`, and `build/` is
ignored wholesale.  Only the hand-written fixtures under `src/dist/disk-a/` are
in the repository, which is what lets a fresh checkout re-stage itself.

These were once a proposal for the owner of a separate `c900/Makefile`.  There
is no separate Makefile any more -- it is this repository's, and it implements
them: see the `$(CPMAIMG)` rule, which stages `src/dist/disk-a/`, runs
`stage-devpack.sh` over `$(ZBASE)` and packs `$(DISKA)` with `mkcpmfs.py`.

## mkcpmdisk.py — the CP/M-only bootable medium

```
```

Builds a whole disk image containing only what CP/M needs: a boot partition
holding kboot and a generated `kboot.cfg`, `cpmboot` holding `cpm.sys`, and the
CP/M-only image and the dual-boot image built from the same inputs agree on
where drive A: and B: start.

Every `verify-*` target builds its medium with this, so none of them needs a
built COHERENT distribution — only the `kboot` binary, which the Makefile names
as `KBOOT` and does not build.  It replaced `patchdisk.py`, which patched the
CP/M partitions of a 42 MB dual-boot disk somebody else had to produce first;
the release path for that disk is still `make dist DIST=extended-cpm` in the
COHERENT tree, which consumes this repository's `cpm.sys` and `cpma.img` as
inputs.

## cohfs.py — the COHERENT filesystem writer

Two of the four partitions above are COHERENT filesystems, because that is the
format the machine's boot ROM and `kboot` read.  That is an on-disk *interface*,
not a dependency on COHERENT the operating system, so the writer for it lives
here: a build must not reach into another repository to produce its own test
medium.  It is the filesystem half of the COHERENT tree's
`os/hostbuild/mkimage.py` (this project's own host tooling), minus everything a
CP/M medium has no use for — no media descriptors, no dist lists, no device or
ownership manifests, no subdirectories.  Proven by producing a byte-identical
`build/cpmonly.bin`.

`--mutate` breaks the boot chain on purpose and exists for `verify-bootgate`,
which requires `bootgate.sh` to reject each broken medium.  Do not use it to
build anything anyone is meant to boot.
