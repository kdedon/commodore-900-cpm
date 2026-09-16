#!/usr/bin/env python3


Structures per COHERENT's include/sys/filsys.h, fblk.h, ino.h, all values
native Z8001 canonical -- shorts little-endian, longs PDP order (high word
first, each word little-endian):

    block 1     superblock
    block 2..   inodes, 64 bytes each; BADFIN = 1, ROOTIN = 2
                V7 chained free-block stack (NICFREE 64), free-inode cache
                (NICINOD 100), 16-byte directory entries, 13 three-byte
                packed block addresses (10 direct, 1 indirect, 1 double).

"""

BS = 512
NICFREE = 64
NICINOD = 100
INODE_SIZE = 64
BADFIN, ROOTIN = 1, 2
# A fixed build timestamp, mkfs-style: an image whose inode times came from
# the clock would differ from itself between two builds of the same input.
STAMP = 1784736000

ROM_MIN_HEADS = 4

# Blocks of kboot.cfg the loader reads; must match CFGBLK in kboot's
# src/bmain.c.
CFGBLK = 2


def p16(v):
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def p32(v):
    """PDP order: high word first, each word little-endian."""
    hi, lo = (v >> 16) & 0xFFFF, v & 0xFFFF
    return bytes((hi & 0xFF, hi >> 8, lo & 0xFF, lo >> 8))


def l3put(v):
    """COHERENT l3tol() packing of a block address into three bytes."""
    return bytes(((v >> 16) & 0xFF, v & 0xFF, (v >> 8) & 0xFF))


class FS:
    """One COHERENT filesystem being built inside `img' at block `base'."""

    def __init__(self, img, base, fsize, isize):
        self.img, self.base, self.fsize, self.isize = img, base, fsize, isize
        self.ninodes = (isize - 2) * 8
        self.nextino = ROOTIN + 1      # cursor: 1 = BADFIN, 2 = ROOTIN reserved
        self.nextblk = isize           # data block allocation cursor
        self.inodes = {}               # ino -> dict

    def wblk(self, n, data, off=0):
        assert self.isize <= n < self.fsize, "block %d outside data area" % n
        self.img.wblk(self.base + n, data, off)

    def balloc(self):
        if self.nextblk >= self.fsize:
            raise RuntimeError("filesystem full at block %d" % self.nextblk)
        b = self.nextblk
        self.nextblk += 1
        return b

    def ialloc(self):
        if self.nextino > self.ninodes:
            raise RuntimeError("out of inodes")
        i = self.nextino
        self.nextino += 1
        return i

    def write_file(self, data):
        """Store file data; return the 13-entry inode address list."""
        nblk = (len(data) + BS - 1) // BS
        blocks = []
        for i in range(nblk):
            b = self.balloc()
            self.wblk(b, data[i*BS:(i+1)*BS])
            blocks.append(b)
        addrs = [0]*13
        addrs[:min(nblk, 10)] = blocks[:10]
        rest = blocks[10:]
        if rest:
            ind = self.balloc()
            addrs[10] = ind
            ib = bytearray(BS)
            for i, b in enumerate(rest[:128]):
                ib[4*i:4*i+4] = p32(b)
            self.wblk(ind, ib)
            rest = rest[128:]
        if rest:
            dbl = self.balloc()
            addrs[11] = dbl
            db = bytearray(BS)
            for j in range(0, len(rest), 128):
                if j//128 >= 128:
                    raise RuntimeError("file too large (triple indirect)")
                l1 = self.balloc()
                db[4*(j//128):4*(j//128)+4] = p32(l1)
                ib = bytearray(BS)
                for i, b in enumerate(rest[j:j+128]):
                    ib[4*i:4*i+4] = p32(b)
                self.wblk(l1, ib)
            self.wblk(dbl, db)
        return addrs

    def set_inode(self, ino, mode, nlink, uid, gid, size, addrs):
        self.inodes[ino] = dict(mode=mode, nlink=nlink, uid=uid, gid=gid,
                                size=size, addrs=addrs)

    def flush_inodes(self):
        for ino, f in self.inodes.items():
            b = bytearray(INODE_SIZE)
            b[0:2] = p16(f['mode'])
            b[2:4] = p16(f['nlink'])
            b[4:6] = p16(f['uid'])
            b[6:8] = p16(f['gid'])
            b[8:12] = p32(f['size'])
            for i, a in enumerate(f['addrs'][:13]):
                b[12+3*i:15+3*i] = l3put(a)
            b[52:56] = p32(STAMP)   # di_atime (union dia_u @12 is 40 bytes)
            b[56:60] = p32(STAMP)   # di_mtime
            b[60:64] = p32(STAMP)   # di_ctime
            blk = 2 + (ino-1)//8
            self.img.wblk(self.base + blk, bytes(b), ((ino-1) % 8)*INODE_SIZE)

    def flush_super(self):
        # V7 free chain, built by replaying the kernel's bfree() (sys/coh/fs2.c)
        # over every unused data block descending, so allocation later pops
        # ascending.  Chain chunks land in the blocks being freed.
        #
        # The chain ENDS at a block whose df_nfree is 0, not at a full block
        # whose df_free[0] is 0: bfree() spills the in-core stack whenever
        # s_nfree is 0 OR NICFREE, so the very first block freed onto an empty
        # list receives a spill of a zero-deep stack -- an all-zero block.  The
        # walkers stop on that (kernel balloc() `b == 0' test, icheck's
        # `while ((i = fbp->df_nfree) != 0)'), and icheck's own -s rebuild
        # writes the same terminator explicitly.  Seeding nfree=1/free[0]=0
        # instead puts a phantom block 0 in the chain, which icheck counts as
        # free and then flags as a duplicate against the boot block.
        nfree, free = 0, [0]*NICFREE
        tfree = 0
        for b in range(self.fsize - 1, self.nextblk - 1, -1):
            if nfree == 0 or nfree == NICFREE:
                chunk = p16(nfree) + b''.join(p32(x) for x in free)
                self.wblk(b, chunk)
                nfree, free = 0, [0]*NICFREE
            free[nfree] = b
            nfree += 1
            tfree += 1
        sb = bytearray(BS)
        sb[0:2] = p16(self.isize)
        sb[2:6] = p32(self.fsize)
        sb[6:8] = p16(nfree)
        for i, v in enumerate(free):
            sb[8+4*i:12+4*i] = p32(v)
        freeinos = [i for i in range(1, self.ninodes+1)
                    if i not in self.inodes][:NICINOD]
        sb[264:266] = p16(len(freeinos))
        for i, v in enumerate(freeinos):
            sb[266+2*i:268+2*i] = p16(v)
        sb[470:474] = p32(STAMP)                            # s_time
        sb[474:478] = p32(tfree)                            # s_tfree
        sb[478:480] = p16(self.ninodes - len(self.inodes))  # s_tinode
        sb[480:482] = p16(1)                                # s_m
        sb[482:484] = p16(1)                                # s_n
        sb[484:496] = b'nonamenopack'                       # s_fname + s_fpack
        self.img.wblk(self.base + 1, bytes(sb))


def build_fs(img, base, fsize, isize, files):
    """Write a filesystem of `files' ({name: bytes}) at block `base'.

    Returns the number of blocks used, which mkcpmdisk.py checks against the
    ROM-safe span.  One flat directory: CP/M has no use for a tree, and a
    writer that cannot make one cannot make a wrong one.
    """
    fs = FS(img, base, fsize, isize)
    # BADFIN: allocated, empty regular file, nlink 0 -- what the shipped mkfs
    # leaves, and what icheck expects to find.
    fs.set_inode(BADFIN, 0o100000, 0, 0, 0, 0, [0]*13)

    entries = []
    for name in sorted(files):
        data = files[name]
        ino = fs.ialloc()
        addrs = fs.write_file(data)
        fs.set_inode(ino, 0o100644, 1, 0, 1, len(data), addrs)
        entries.append((name, ino))

    ent = [('.', ROOTIN), ('..', ROOTIN)] + entries
    data = b''.join(p16(i) + n.encode()[:14].ljust(14, b'\0') for n, i in ent)
    addrs = fs.write_file(data)
    # A directory's link count is one per name pointing at it: its own ".",
    # the ".." of each subdirectory (none here) and the entry naming it in its
    # parent.  The root has no naming entry in a parent, and dcheck accounts
    # for the absent one anyway (dcheck.c:182), so the root is 3.
    fs.set_inode(ROOTIN, 0o040755, 3, 0, 1, len(data), addrs)

    fs.flush_inodes()
    fs.flush_super()
    print("fs@%-6d %5d/%d blocks used, %d/%d inodes used"
          % (base, fs.nextblk, fsize, len(fs.inodes), fs.ninodes))
    return fs.nextblk


class Img:
    """The block-write interface build_fs() writes through."""

    def __init__(self, nblocks):
        self.d = bytearray(nblocks * BS)

    def wblk(self, n, data, off=0):
        assert off + len(data) <= BS
        self.d[n*BS+off : n*BS+off+len(data)] = data
