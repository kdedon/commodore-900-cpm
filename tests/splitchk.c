/*
 * splitchk.c -- offline validation harness for the split-I/D loader shim
 * scanner (dev/test instrument only; what actually runs at load time is
 * the on-target scanner, src/bdos/zsplit.c, invoked from pgmld -- this
 * harness exists to validate that scanner off-target, not to replace it).
 *
 * For each 0xEE0B x.out named on the command line:
 *   1. extracts the code segment and runs the shared scanner
 *      (zscan + zsfix, exactly as the target's spload does);
 *   2. cross-checks every decoded instruction length against an
 *      independently transcribed Z8000 nonsegmented length map (reflen(),
 *      ported from the project emulator's decode ROM length computation,
 *      itself derived from the Zilog opcode map);
 *   3. runs a recursive-descent reachability walk seeded from the entry
 *      point and every global code symbol, verifying each reachable
 *      instruction start coincides with a linear-scan instruction start
 *      (a mismatch = the linear scan desynchronized over live code);
 *   4. checks every global code symbol lands on a linear-scan boundary;
 *   5. verifies no LDR-referenced (program-space data) word would be
 *      patched, and that none is reachable as an instruction.
 *
 * Exit status 0 = all checks clean over all binaries.
 *
 * Build: cc -DHOSTCC -o splitchk splitchk.c ../src/bdos/zsplit.c
 * (no -Isrc/bdos: the BDOS's local stdio.h must not shadow the host headers;
 * zsplit.h is included by relative path instead)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/bdos/zsplit.h"

extern int zdecode();
extern int zscan();
extern int zsfix();
extern char zqk[256];

/* ------------------------------------------------------------------ */
/* Independent length reference: transcription of the emulator decode
 * ROM's decode_length_compute() for nonsegmented mode (aw = 1).       */

static int reflen(unsigned op)
{
    unsigned hi = op >> 8, lo = op & 0xFF, n2 = (lo >> 4) & 0x0F;

    if (hi <= 0x0B) return (n2 == 0) ? 2 : 1;
    if (hi == 0x0C || hi == 0x0D) {
        unsigned l = lo & 0x0F;
        if (l == 0x01 || l == 0x05 || l == 0x09) return 2;
        return 1;
    }
    if (hi == 0x0E || hi == 0x0F) return 1;
    if (hi >= 0x10 && hi <= 0x17) {
        if (n2 == 0 && (hi == 0x10 || hi == 0x12 || hi == 0x14 || hi == 0x16)) return 3;
        return 1;
    }
    if (hi == 0x18) return (n2 == 0) ? 3 : 1;
    if (hi == 0x19) return (n2 == 0) ? 2 : 1;
    if (hi == 0x1A) return (n2 == 0) ? 3 : 1;
    if (hi == 0x1B) return (n2 == 0) ? 2 : 1;
    if (hi == 0x1C) { unsigned l = lo & 0x0F; return (l == 0x01 || l == 0x09) ? 2 : 1; }
    if (hi == 0x1D || hi == 0x1E || hi == 0x1F) return 1;
    if (hi == 0x20 || hi == 0x21) return (n2 == 0) ? 2 : 1;
    if (hi >= 0x22 && hi <= 0x27) return (n2 == 0) ? 2 : 1;
    if (hi >= 0x28 && hi <= 0x2F) return 1;
    if (hi >= 0x30 && hi <= 0x37) return 2;
    if (hi == 0x38 || hi == 0x39) return 1;
    if (hi == 0x3A || hi == 0x3B) return 2;
    if (hi >= 0x3C && hi <= 0x3F) return 1;
    if (hi >= 0x40 && hi <= 0x6F) {
        if ((hi == 0x4C || hi == 0x4D) &&
            (((lo & 0x0F) == 0x01) || ((lo & 0x0F) == 0x05) || ((lo & 0x0F) == 0x09)))
            return 3;
        if (hi == 0x5C && (((lo & 0x0F) == 0x01) || ((lo & 0x0F) == 0x09)))
            return 3;
        return 2;
    }
    if (hi >= 0x70 && hi <= 0x77) return 2;
    if (hi == 0x79) return 2;
    if (hi >= 0x78 && hi <= 0x7F) return (hi == 0x79) ? 2 : 1;
    if (hi == 0x8E || hi == 0x8F) return 2;
    if (hi >= 0x80 && hi <= 0xAF) return 1;
    if (hi >= 0xB0 && hi <= 0xBF) {
        if (hi == 0xB2 || hi == 0xB3) {
            unsigned l = lo & 0x0F;
            if (hi == 0xB2 && (l == 0x01 || l == 0x03 || l == 0x09 || l == 0x0B)) return 2;
            if (hi == 0xB3 && (l & 1)) return 2;
        }
        if (hi == 0xB8 || hi == 0xBA || hi == 0xBB) return 2;
        return 1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */

static zw *text;            /* code segment as host words */
static unsigned nwords;
static unsigned char *mark; /* LDR-target bitmap */
static unsigned char *istart;  /* linear-scan instruction-start map */
static unsigned char *rstart;  /* reachability instruction-start map */
static unsigned char *visited;

static int npatch, nldr, nill, nunpatch, nbadid;
static unsigned hist[64][8];   /* [op][mode] occurrence counts */
static unsigned char *patched; /* per-word: a patch landed here */

/* called through an old-style pointer from zscan: promoted arg types.
 * The scanner's id is only guaranteed to carry len and klass (the fast
 * table path fills nothing else), so re-decode for the histogram -- and
 * cross-check the claimed len/klass against the full decode. */
static int patchcb(arg, off, w0, id)
char *arg;
int off, w0;
struct zid *id;
{
    struct zid full;

    (void)arg;
    zdecode((zw)w0, (zw)(off + 1 < (int)nwords ? text[off + 1] : 0), &full);
    if (id->klass != ZK_DATA || full.klass != ZK_DATA || id->len != full.len)
        nbadid++;
    npatch++;
    patched[off & 0xffff] = 1;
    hist[full.op & 63][full.mode & 7]++;
    return 0;
}

static int unpatchcb(arg, off)
char *arg;
int off;
{
    (void)arg;
    if (patched[off & 0xffff]) { patched[off & 0xffff] = 0; nunpatch++; }
    return 0;
}

/* reachability walk */
static unsigned *stack;
static int sp;

static void push_pc(unsigned w)
{
    if (w >= nwords) return;
    if (visited[w]) return;
    stack[sp++] = w;
}

static int walk(void)
{
    int bad = 0;
    while (sp > 0) {
        unsigned off = stack[--sp];
        for (;;) {
            struct zid in;
            unsigned hi, n3;
            zw w0, w1;
            if (off >= nwords) break;
            if (visited[off]) break;
            visited[off] = 1;
            rstart[off] = 1;
            w0 = text[off];
            w1 = (off + 1 < nwords) ? text[off + 1] : 0;
            zdecode(w0, w1, &in);
            hi = (w0 >> 8) & 0xff;
            n3 = w0 & 0x0F;
            if (!istart[off]) bad++;
            if (mark[off >> 3] & (1 << (off & 7))) bad++;

            if (hi == 0x5E) {                       /* JP cc, DA|X */
                if (((w0 >> 4) & 0xF) == 0)         /* DA form */
                    push_pc((unsigned)(w1 >> 1));
                if (n3 == 8) break;                 /* unconditional */
            } else if (hi == 0x5F) {                /* CALL DA|X */
                if (((w0 >> 4) & 0xF) == 0)
                    push_pc((unsigned)(w1 >> 1));
            } else if (hi >= 0xE0 && hi <= 0xEF) {  /* JR cc, disp8 */
                int d = (int)(signed char)(w0 & 0xFF);
                push_pc((unsigned)((int)off + 1 + d));
                if ((hi & 0x0F) == 8) break;        /* JR T = always */
            } else if (hi >= 0xD0 && hi <= 0xDF) {  /* CALR disp12 */
                int raw = w0 & 0x0FFF;
                if (raw & 0x0800) raw |= ~0x0FFF;
                push_pc((unsigned)((int)off + in.len - raw));
            } else if (hi >= 0xF0) {                /* DJNZ (backward) */
                push_pc((unsigned)((int)off + 1 - (int)(w0 & 0x7F)));
            } else if (hi == 0x1E || hi == 0x1F) {  /* JP/CALL @Rs */
                if (hi == 0x1E && n3 == 8) break;   /* JP @Rs uncond */
            } else if (hi == 0x9E) {                /* RET cc */
                if (n3 == 8) break;
            } else if (hi == 0x7B && (w0 & 0xFF) == 0) {
                break;                              /* IRET */
            } else if (hi == 0x7A) {
                break;                              /* HALT */
            } else if (hi == 0x7F && (w0 & 0xFF) == 0) {
                /* SC #0 = BDOS warm boot: never returns */
                break;
            }
            off += in.len;
        }
    }
    return bad;
}

static unsigned rd16(unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned long rd32(unsigned char *p)
{ return ((unsigned long)rd16(p) << 16) | rd16(p + 2); }

static const char *opnm[] = {
    "?", "LOAD", "STORE", "STIMM", "CLR", "ALUR", "ALUM", "TESTM", "EX",
    "MUL", "DIV", "LDM", "STM", "PUSH", "POP", "PUSHI", "BLKT", "BLKC",
    "BLKS", "TRANS", "SC"
};
static const char *modenm[] = { "-", "IR", "DA", "X", "BA", "BX" };

int main(int argc, char **argv)
{
    int i, fail = 0;

    /* structural check: zdecode length vs independent reference, for all
     * 65536 first words (block-op forms need w1: check both siblings) */
    {
        unsigned op; int diffs = 0;
        for (op = 0; op < 0x10000; op++) {
            struct zid in;
            zdecode((zw)op, (zw)0, &in);
            if (in.klass != ZK_ILL && in.len != reflen(op)) {
                if (diffs < 10)
                    fprintf(stderr, "LEN DIFF op=%04x mine=%d ref=%d\n",
                            op, in.len, reflen(op));
                diffs++;
            }
            zdecode((zw)op, (zw)8, &in);
            if (in.klass != ZK_ILL && in.len != reflen(op)) diffs++;
        }
        printf("length cross-check vs reference: %d differences\n", diffs);
        if (diffs) fail = 1;
    }

    /* fast-classify table check: for every first word, the table action
     * (ZQ_xxx encoding, zsplit.h) must agree with the full decode --
     * skip entries on length and no-reference class, ZK_DATA actions on
     * class and length, nibble-2-gated entries per gate arm */
    {
        unsigned op; int diffs = 0;
        for (op = 0; op < 0x10000; op++) {
            struct zid in;
            int q = zqk[op >> 8], ok;
            int n2 = (op >> 4) & 0x0F;
            if (q == ZQ_FULL) continue;
            zdecode((zw)op, (zw)0, &in);
            switch (q) {
            case 1: case 2: case 3:
                ok = (in.len == q && in.klass == ZK_NONE);
                break;
            case ZQ_D1:
                ok = (in.klass == ZK_DATA && in.len == 1);
                break;
            case ZQ_D2:
                ok = (in.klass == ZK_DATA && in.len == 2);
                break;
            case ZQ_I2D1:
                ok = (n2 == 0) ? (in.len == 2 && in.klass == ZK_NONE)
                               : (in.klass == ZK_DATA && in.len == 1);
                break;
            case ZQ_I3D1:
                ok = (n2 == 0) ? (in.len == 3 && in.klass == ZK_NONE)
                               : (in.klass == ZK_DATA && in.len == 1);
                break;
            case ZQ_B2:
                /* n2 == 0 falls through to the full decode: no claim */
                ok = (n2 == 0) || (in.klass == ZK_DATA && in.len == 2);
                break;
            default:
                ok = 0;
                break;
            }
            if (!ok) {
                if (diffs < 10)
                    fprintf(stderr, "TABLE DIFF op=%04x q=%d len=%d klass=%d\n",
                            op, q, in.len, in.klass);
                diffs++;
            }
        }
        printf("fast-classify table check: %d differences\n", diffs);
        if (diffs) fail = 1;
    }

    for (i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        unsigned char *buf;
        long flen;
        unsigned nseg, w, codlen;
        unsigned long init, reloc, symb, off;
        struct zsctx ctx;
        int badsym = 0, badreach = 0, patchldr = 0;
        unsigned j, k;

        if (!f) { perror(argv[i]); return 2; }
        fseek(f, 0, SEEK_END); flen = ftell(f); fseek(f, 0, SEEK_SET);
        buf = malloc(flen);
        if (fread(buf, 1, flen, f) != (size_t)flen) { perror("read"); return 2; }
        fclose(f);

        if (rd16(buf) != 0xEE0B) {
            printf("%s: not split I/D (magic %04x), skipped\n", argv[i], rd16(buf));
            free(buf); continue;
        }
        nseg = rd16(buf + 2);
        init = rd32(buf + 4); reloc = rd32(buf + 8); symb = rd32(buf + 12);
        codlen = 0;
        for (j = 0; j < nseg; j++) {
            unsigned typ = buf[16 + 4 * j + 1];
            unsigned ln = rd16(buf + 16 + 4 * j + 2);
            if (typ == 3 && codlen == 0) codlen = ln;   /* first COD */
        }
        off = 16 + 4UL * nseg;                          /* file offset of COD */

        nwords = codlen >> 1;
        text = malloc(nwords * sizeof(zw));
        for (w = 0; w < nwords; w++)
            text[w] = (zw)rd16(buf + off + 2UL * w);

        mark = calloc(1, (nwords + 7) / 8);
        istart = calloc(1, nwords);
        rstart = calloc(1, nwords);
        visited = calloc(1, nwords);
        patched = calloc(1, 65536);
        stack = malloc(nwords * sizeof(unsigned));
        sp = 0;
        npatch = nldr = nill = nunpatch = nbadid = 0;
        memset(hist, 0, sizeof hist);

        ctx.text = text; ctx.nw = (zw)nwords; ctx.mark = (char *)mark;
        nill = zscan(&ctx, patchcb, (char *)0);
        zsfix(&ctx, unpatchcb, (char *)0);
        patchldr = nunpatch;   /* patches reverted as embedded prog data */

        /* linear instruction-start map + LDR count */
        {
            unsigned o = 0;
            while (o < nwords) {
                struct zid in;
                istart[o] = 1;
                zdecode(text[o], (zw)(o + 1 < nwords ? text[o + 1] : 0), &in);
                if (in.klass == ZK_PROG) nldr++;
                o += in.len;
            }
        }

        /* reachability: seed entry 0 + every global code symbol */
        push_pc(0);
        if (symb) {
            unsigned char *sy = buf + 16 + 4UL * nseg + init + reloc;
            unsigned nsym = (unsigned)(symb / 12);
            for (j = 0; j < nsym; j++) {
                unsigned sg = sy[12 * j], fl = sy[12 * j + 1];
                unsigned val = rd16(sy + 12 * j + 2);
                if (sg == 0 && fl == 3 && (val & 1) == 0)
                    push_pc(val >> 1);
            }
        }
        badreach = walk();

        /* symbol boundary check: every global code symbol must be a
         * linear-scan instruction start */
        if (symb) {
            unsigned char *sy = buf + 16 + 4UL * nseg + init + reloc;
            unsigned nsym = (unsigned)(symb / 12);
            for (j = 0; j < nsym; j++) {
                unsigned sg = sy[12 * j], fl = sy[12 * j + 1];
                unsigned val = rd16(sy + 12 * j + 2);
                if (sg == 0 && fl == 3) {
                    if ((val & 1) || !istart[val >> 1]) {
                        if (badsym < 5)
                            printf("  SYM not on boundary: %.8s = 0x%04x\n",
                                   sy + 12 * j + 4, val);
                        badsym++;
                    }
                }
            }
        }

        {
            unsigned reach = 0, starts = 0;
            for (j = 0; j < nwords; j++) { reach += rstart[j]; starts += istart[j]; }
            printf("%s: cod=%u words, %u insns, patch=%d ldr=%d ill=%d "
                   "reach=%u/%u badreach=%d badsym=%d patch-on-ldr=%d badid=%d\n",
                   argv[i], nwords, starts, npatch, nldr, nill,
                   reach, starts, badreach, badsym, patchldr, nbadid);
            if (nill || badreach || badsym || patchldr || nbadid) fail = 1;
        }

        printf("  patched forms:");
        for (j = 1; j < 64; j++)
            for (k = 0; k < 8; k++)
                if (hist[j][k])
                    printf(" %s/%s=%u",
                           j <= 20 ? opnm[j] : "?", modenm[k], hist[j][k]);
        printf("\n");

        free(buf); free(text); free(mark); free(istart);
        free(rstart); free(visited); free(stack); free(patched);
    }
    return fail;
}
