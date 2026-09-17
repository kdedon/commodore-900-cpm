/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * splittest.c -- targeted host tests for the split-I/D SC-trap emulator
 * (src/bdos/splitsc.c).  One test per emulated instruction form: sets up a
 * frame, banks and side table, executes one trap, and checks registers,
 * memory, flags, PC advance and bank routing.
 *
 * Build: cc -DHOSTCC -o splittest splittest.c ../src/bdos/splitsc.c ../src/bdos/zsplit.c
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/bdos/zsplit.h"

#define FC 0x80
#define FZ 0x40
#define FS 0x20
#define FV 0x10

struct spfr {
    zw r[14];
    zw id, fcw, pcseg, pcoff;
};

extern char *spcode, *spdata;
extern zw *sptab, sptop, spusp, spufp;
extern int spemu();

static unsigned char codeb[65536], datab[65536];
static zw tab[32768];
static struct spfr fr;

static int nfail, ntest;

#define INSN 0x0100     /* where each test plants its instruction */

static void setup(void)
{
    memset(codeb, 0, sizeof codeb);
    memset(datab, 0, sizeof datab);
    memset(tab, 0, sizeof tab);
    memset(&fr, 0, sizeof fr);
    spcode = (char *)codeb;
    spdata = (char *)datab;
    sptab = tab;
    sptop = 0xE000;
    spusp = 0xFEFC;
    spufp = 0;
    fr.pcoff = INSN + 2;    /* SC pushed the next-word PC */
    fr.fcw = 0x1800;
}

/* plant instruction words: w0 goes to the side table (it was patched),
 * the rest stay in text */
static void plant(zw w0, zw w1, zw w2)
{
    tab[INSN >> 1] = w0;
    codeb[INSN + 2] = w1 >> 8; codeb[INSN + 3] = w1 & 0xff;
    codeb[INSN + 4] = w2 >> 8; codeb[INSN + 5] = w2 & 0xff;
}

static zw drw(unsigned off)  /* read data-bank word */
{ return ((zw)datab[off & 0xffff] << 8) | datab[(off + 1) & 0xffff]; }
static zw crw(unsigned off)
{ return ((zw)codeb[off & 0xffff] << 8) | codeb[(off + 1) & 0xffff]; }
static void dww(unsigned off, zw v)
{ datab[off & 0xffff] = v >> 8; datab[(off + 1) & 0xffff] = v & 0xff; }
static void cww(unsigned off, zw v)
{ codeb[off & 0xffff] = v >> 8; codeb[(off + 1) & 0xffff] = v & 0xff; }

static void chk(const char *what, long got, long want)
{
    ntest++;
    if (got != want) {
        printf("FAIL %s: got 0x%lx want 0x%lx\n", what, got, want);
        nfail++;
    }
}

static void run(const char *what, int wantret)
{
    int r = spemu((long)&fr);
    ntest++;
    if (r != wantret) {
        printf("FAIL %s: spemu ret %d want %d\n", what, r, wantret);
        nfail++;
    }
}

static void chkpc(const char *what, int words)
{
    chk(what, (long)fr.pcoff, (long)(INSN + 2 * words));
}

int main(void)
{
    /* ---- LOAD ---- */
    setup();                            /* LD r3,@r2 */
    plant(0x2123, 0, 0); fr.r[2] = 0x2000; dww(0x2000, 0xBEEF);
    run("ld r3,@r2", 0);
    chk("ld r3,@r2 val", fr.r[3], 0xBEEF); chkpc("ld @r pc", 1);
    chk("ld @r flags", fr.fcw, 0x1800);          /* LD sets no flags */

    setup();                            /* LDB rl1,addr (DA, byte) */
    plant(0x6009, 0x2001, 0); datab[0x2001] = 0x5A;
    run("ldb rl1,DA", 0);
    chk("ldb DA val", fr.r[1] & 0xff, 0x5A); chkpc("ldb DA pc", 2);

    setup();                            /* LD r5,addr(r4) (X mode) */
    plant(0x6145, 0x1000, 0); fr.r[4] = 0x0234; dww(0x1234, 0xC0DE);
    run("ld r5,X", 0);
    chk("ld X val", fr.r[5], 0xC0DE); chkpc("ld X pc", 2);

    setup();                            /* LDL rr2,addr (DA, long) */
    plant(0x5402, 0x3000, 0); dww(0x3000, 0x1234); dww(0x3002, 0x5678);
    run("ldl DA", 0);
    chk("ldl DA hi", fr.r[2], 0x1234); chk("ldl DA lo", fr.r[3], 0x5678);

    setup();                            /* LD r6, r13(#disp) (BA mode) */
    plant(0x31D6, 0x0010, 0); fr.r[13] = 0x4000; dww(0x4010, 0xFACE);
    run("ld BA", 0);
    chk("ld BA val", fr.r[6], 0xFACE); chkpc("ld BA pc", 2);

    setup();                            /* LD r1, r2(r3) (BX mode) */
    plant(0x7121, 0x0300, 0); fr.r[2] = 0x5000; fr.r[3] = 0x0040;
    dww(0x5040, 0xD00D);
    run("ld BX", 0);
    chk("ld BX val", fr.r[1], 0xD00D);

    /* load from the stack region routes to the code bank */
    setup();                            /* LD r3,@r15 (frame local) */
    plant(0x21F3, 0, 0); spusp = 0xFE00; cww(0xFE00, 0xAA55);
    dww(0xFE00, 0x1111);                /* decoy in the data bank */
    run("ld @sp", 0);
    chk("ld @sp code bank", fr.r[3], 0xAA55);

    /* r14-based frame access (banked register) */
    setup();                            /* LD r7, r14(#4) */
    plant(0x31E7, 0x0004, 0); spufp = 0xFE20; spusp = 0xFE00;
    cww(0xFE24, 0x7777);
    run("ld r14(4)", 0);
    chk("ld r14(4)", fr.r[7], 0x7777);

    /* ---- STORE ---- */
    setup();                            /* LD @r4,r9 */
    plant(0x2F49, 0, 0); fr.r[4] = 0x2100; fr.r[9] = 0xCAFE;
    run("st @r", 0);
    chk("st @r", drw(0x2100), 0xCAFE);

    setup();                            /* LDL addr,rr8 */
    plant(0x5D08, 0x2200, 0); fr.r[8] = 0x0102; fr.r[9] = 0x0304;
    run("stl DA", 0);
    chk("stl hi", drw(0x2200), 0x0102); chk("stl lo", drw(0x2202), 0x0304);

    setup();                            /* LDB addr(r2),rl0 */
    plant(0x6E28, 0x0100, 0); fr.r[2] = 0x2000; fr.r[0] = 0x0042;
    run("stb X", 0);
    chk("stb X", datab[0x2100], 0x42);

    /* store into a caller frame above SP -> code bank */
    setup();                            /* LD @r5,r0 with r5 >= SP */
    plant(0x2F50, 0, 0); fr.r[5] = 0xFF00; fr.r[0] = 0xBEAD; spusp = 0xFE00;
    run("st stack", 0);
    chk("st stack", crw(0xFF00), 0xBEAD);
    chk("st stack not data", drw(0xFF00), 0);

    /* ---- STIMM / CLR ---- */
    setup();                            /* LD @r2,#imm */
    plant(0x0D25, 0x4321, 0); fr.r[2] = 0x2000;
    run("ld @r,#", 0);
    chk("ld @r,#", drw(0x2000), 0x4321); chkpc("ld @r,# pc", 2);

    setup();                            /* LDB addr,#imm (byte in hi half) */
    plant(0x4C05, 0x2001, 0x7700);
    run("ldb DA,#", 0);
    chk("ldb DA,#", datab[0x2001], 0x77); chkpc("ldb DA,# pc", 3);

    setup();                            /* CLR addr */
    plant(0x4D08, 0x2000, 0); dww(0x2000, 0xFFFF);
    run("clr DA", 0);
    chk("clr DA", drw(0x2000), 0);

    /* ---- ALUR ---- */
    setup();                            /* ADD r1,addr */
    plant(0x4101, 0x2000, 0); fr.r[1] = 0x7FFF; dww(0x2000, 1);
    run("add ovf", 0);
    chk("add val", fr.r[1], 0x8000);
    chk("add flags", fr.fcw & (FC|FZ|FS|FV), FS|FV);

    setup();                            /* ADD r1,addr carry */
    plant(0x4101, 0x2000, 0); fr.r[1] = 0xFFFF; dww(0x2000, 1);
    run("add cy", 0);
    chk("add cy val", fr.r[1], 0);
    chk("add cy flags", fr.fcw & (FC|FZ|FS|FV), FC|FZ);

    setup();                            /* CP r2,@r3 equal */
    plant(0x0B32, 0, 0); fr.r[2] = 0x1234; fr.r[3] = 0x2000;
    dww(0x2000, 0x1234);
    run("cp eq", 0);
    chk("cp eq Z", fr.fcw & FZ, FZ);
    chk("cp keeps reg", fr.r[2], 0x1234);

    setup();                            /* CPB rl0,@r3 borrow */
    plant(0x0A38, 0, 0); fr.r[0] = 0x0001; fr.r[3] = 0x2000;
    datab[0x2000] = 0x02;
    run("cpb lt", 0);
    chk("cpb C", fr.fcw & FC, FC);
    chk("cpb S", fr.fcw & FS, FS);

    setup();                            /* ANDB rl2,addr: parity */
    plant(0x460A, 0x2000, 0); fr.r[2] = 0x00FF; datab[0x2000] = 0x33;
    run("andb", 0);
    chk("andb val", fr.r[2] & 0xff, 0x33);
    chk("andb parity even", fr.fcw & FV, FV);   /* 0x33 = 4 bits */

    setup();                            /* SUBL rr4,@r2 */
    plant(0x1224, 0, 0); fr.r[4] = 0; fr.r[5] = 5; fr.r[2] = 0x2000;
    dww(0x2000, 0); dww(0x2002, 7);
    run("subl", 0);
    chk("subl hi", fr.r[4], 0xFFFF); chk("subl lo", fr.r[5], 0xFFFE);
    chk("subl C", fr.fcw & FC, FC);

    /* ---- ALUM ---- */
    setup();                            /* INC @r2,#4 */
    plant(0x2923, 0, 0); fr.r[2] = 0x2000; dww(0x2000, 0x00FE);
    run("inc", 0);
    chk("inc val", drw(0x2000), 0x0102);
    chk("inc no C", fr.fcw & FC, 0);

    setup();                            /* DECB addr,#1 */
    plant(0x6A00, 0x2005, 0); datab[0x2005] = 1;
    run("decb", 0);
    chk("decb val", datab[0x2005], 0);
    chk("decb Z", fr.fcw & FZ, FZ);

    setup();                            /* COM addr */
    plant(0x4D00, 0x2000, 0); dww(0x2000, 0x00FF);
    run("com", 0);
    chk("com val", drw(0x2000), 0xFF00);
    chk("com S", fr.fcw & FS, FS);

    setup();                            /* NEG addr */
    plant(0x4D02, 0x2000, 0); dww(0x2000, 1);
    run("neg", 0);
    chk("neg val", drw(0x2000), 0xFFFF);
    chk("neg C", fr.fcw & FC, FC);

    setup();                            /* TSET addr */
    plant(0x4D06, 0x2000, 0); dww(0x2000, 0x8000);
    run("tset", 0);
    chk("tset val", drw(0x2000), 0xFFFF);
    chk("tset S", fr.fcw & FS, FS);

    setup();                            /* SET addr,#3 */
    plant(0x6503, 0x2000, 0); dww(0x2000, 0);
    run("set", 0);
    chk("set val", drw(0x2000), 0x0008);

    setup();                            /* RESB @r2,#7 */
    plant(0x2227, 0, 0); fr.r[2] = 0x2001; datab[0x2001] = 0xFF;
    run("resb", 0);
    chk("resb val", datab[0x2001], 0x7F);

    /* ---- TESTM ---- */
    setup();                            /* TEST addr (zero) */
    plant(0x4D04, 0x2000, 0); dww(0x2000, 0);
    run("test", 0);
    chk("test Z", fr.fcw & FZ, FZ);

    setup();                            /* TESTL @r2 */
    plant(0x1C28, 0, 0); fr.r[2] = 0x2000; dww(0x2000, 0x8000);
    run("testl", 0);
    chk("testl S", fr.fcw & FS, FS);

    setup();                            /* BIT addr,#0 (bit clear) */
    plant(0x6700, 0x2000, 0); dww(0x2000, 0xFFFE);
    run("bit", 0);
    chk("bit Z", fr.fcw & FZ, FZ);

    setup();                            /* CP @r2,#imm */
    plant(0x0D21, 0x0055, 0); fr.r[2] = 0x2000; dww(0x2000, 0x0055);
    run("cp @r,#", 0);
    chk("cp @r,# Z", fr.fcw & FZ, FZ);

    setup();                            /* CPB addr,#imm (len 3) */
    plant(0x4C01, 0x2004, 0x1200); datab[0x2004] = 0x11;
    run("cpb DA,#", 0);
    chk("cpb DA,# C", fr.fcw & FC, FC);  /* 0x11 < 0x12: borrow */
    chkpc("cpb DA,# pc", 3);

    /* ---- EX ---- */
    setup();                            /* EX r1,@r2 */
    plant(0x2D21, 0, 0); fr.r[1] = 0x1111; fr.r[2] = 0x2000;
    dww(0x2000, 0x2222);
    run("ex", 0);
    chk("ex reg", fr.r[1], 0x2222); chk("ex mem", drw(0x2000), 0x1111);

    /* ---- MULT/DIV ---- */
    setup();                            /* MULT rr2,addr */
    plant(0x5902, 0x2000, 0); fr.r[3] = 100; dww(0x2000, 0xFF9C); /* -100 */
    run("mult", 0);
    chk("mult hi", fr.r[2], 0xFFFF); chk("mult lo", fr.r[3], 0xD8F0);
    chk("mult no C", fr.fcw & FC, 0);    /* -10000 fits 16 signed bits */

    setup();                            /* MULT overflowing a word */
    plant(0x5902, 0x2000, 0); fr.r[3] = 300; dww(0x2000, 300);
    run("multovf", 0);
    chk("multovf hi", fr.r[2], 1); chk("multovf lo", fr.r[3], 90000L & 0xffff);
    chk("multovf C", fr.fcw & FC, FC);

    setup();                            /* MULT small: no C */
    plant(0x5902, 0x2000, 0); fr.r[3] = 7; dww(0x2000, 6);
    run("mult2", 0);
    chk("mult2 lo", fr.r[3], 42); chk("mult2 hi", fr.r[2], 0);
    chk("mult2 no C", fr.fcw & FC, 0);

    setup();                            /* MULTL rq0,@r6 */
    plant(0x1860, 0, 0); fr.r[2] = 0x0001; fr.r[3] = 0x0000; /* rr2=65536 */
    fr.r[6] = 0x2000; dww(0x2000, 0x0001); dww(0x2002, 0x0000); /* 65536 */
    run("multl", 0);
    chk("multl q0hi", fr.r[0], 0); chk("multl q0lo", fr.r[1], 1);
    chk("multl q1hi", fr.r[2], 0); chk("multl q1lo", fr.r[3], 0);
    chk("multl C", fr.fcw & FC, FC);     /* 2^32 overflows a long */

    setup();                            /* DIV rr2,addr : 1000/3 */
    plant(0x5B02, 0x2000, 0); fr.r[2] = 0; fr.r[3] = 1000;
    dww(0x2000, 3);
    run("div", 0);
    chk("div q", fr.r[3], 333); chk("div r", fr.r[2], 1);
    chk("div flags", fr.fcw & (FC|FZ|FS|FV), 0);

    setup();                            /* DIV 100000/3: quotient 33333 is
                                         * just past 16-bit signed (case 2) */
    plant(0x5B02, 0x2000, 0); fr.r[2] = 0x0001; fr.r[3] = 0x86A0;
    dww(0x2000, 3);
    run("div2", 0);
    chk("div2 q", fr.r[3], 33333L & 0xffff); chk("div2 r", fr.r[2], 1);
    chk("div2 flags", fr.fcw & (FC|FZ|FS|FV), FC|FV);

    setup();                            /* DIV by zero */
    plant(0x5B02, 0x2000, 0); fr.r[2] = 0; fr.r[3] = 10; dww(0x2000, 0);
    run("div0", 0);
    chk("div0 flags", fr.fcw & (FZ|FV), FZ|FV);
    chk("div0 reg untouched", fr.r[3], 10);

    setup();                            /* DIV overflow (case 3) */
    plant(0x5B02, 0x2000, 0); fr.r[2] = 0x7FFF; fr.r[3] = 0xFFFF;
    dww(0x2000, 1);
    run("divovf", 0);
    chk("divovf V", fr.fcw & FV, FV);
    chk("divovf reg untouched", fr.r[3], 0xFFFF);

    setup();                            /* DIVL rq0,@r6: -10 / 3 */
    plant(0x1A60, 0, 0);
    fr.r[0] = 0xFFFF; fr.r[1] = 0xFFFF; fr.r[2] = 0xFFFF; fr.r[3] = 0xFFF6;
    fr.r[6] = 0x2000; dww(0x2000, 0); dww(0x2002, 3);
    run("divl", 0);
    chk("divl q", ((long)fr.r[2] << 16) | fr.r[3], 0xFFFFFFFDL & 0xFFFFFFFFL);
    chk("divl r", ((long)fr.r[0] << 16) | fr.r[1], 0xFFFFFFFFL);
    chk("divl S", fr.fcw & FS, FS);

    /* ---- LDM ---- */
    setup();                            /* LDM r2,@r6,#3 */
    plant(0x1C61, 0x0202, 0); fr.r[6] = 0x2000;
    dww(0x2000, 0x0A0A); dww(0x2002, 0x0B0B); dww(0x2004, 0x0C0C);
    run("ldm", 0);
    chk("ldm r2", fr.r[2], 0x0A0A); chk("ldm r3", fr.r[3], 0x0B0B);
    chk("ldm r4", fr.r[4], 0x0C0C); chkpc("ldm pc", 2);

    setup();                            /* LDM r13,@r6,#3 -> r13,r14,r15 */
    plant(0x1C61, 0x0D02, 0); fr.r[6] = 0x2000;
    dww(0x2000, 0x1313); dww(0x2002, 0x1414); dww(0x2004, 0x1515);
    run("ldm hi", 0);
    chk("ldm r13", fr.r[13], 0x1313);
    chk("ldm r14", spufp, 0x1414); chk("ldm r15", spusp, 0x1515);

    setup();                            /* LDM addr(r4),r8,#2 (store) */
    plant(0x5C49, 0x0801, 0x1000); fr.r[4] = 0x0100;
    fr.r[8] = 0x8888; fr.r[9] = 0x9999;
    run("stm X", 0);
    chk("stm w0", drw(0x1100), 0x8888); chk("stm w1", drw(0x1102), 0x9999);
    chkpc("stm X pc", 3);

    /* ---- PUSH/POP ---- */
    setup();                            /* PUSH @r15,addr */
    plant(0x53F0, 0x2000, 0); dww(0x2000, 0x4242);
    run("push DA", 0);
    chk("push sp", spusp, 0xFEFA);
    chk("push val on code stack", crw(0xFEFA), 0x4242);

    setup();                            /* PUSHL @r15,addr(r3) */
    plant(0x51F3, 0x2000, 0); fr.r[3] = 4;
    dww(0x2004, 0xAAAA); dww(0x2006, 0xBBBB);
    run("pushl X", 0);
    chk("pushl sp", spusp, 0xFEF8);
    chk("pushl hi", crw(0xFEF8), 0xAAAA);
    chk("pushl lo", crw(0xFEFA), 0xBBBB);

    setup();                            /* PUSH @r15,#imm */
    plant(0x0DF9, 0x1234, 0);
    run("pushi", 0);
    chk("pushi sp", spusp, 0xFEFA);
    chk("pushi val", crw(0xFEFA), 0x1234);

    setup();                            /* PUSH @r2,@r3: heap-directed push */
    plant(0x1323, 0, 0); fr.r[2] = 0x3000; fr.r[3] = 0x2000;
    dww(0x2000, 0x6666);
    run("push heap", 0);
    chk("push heap ptr", fr.r[2], 0x2FFE);
    chk("push heap val", drw(0x2FFE), 0x6666);  /* below SP: data bank */

    setup();                            /* POP addr,@r15 */
    plant(0x57F0, 0x2000, 0); cww(0xFEFC, 0x7A7A);
    run("pop DA", 0);
    chk("pop sp", spusp, 0xFEFE);
    chk("pop val", drw(0x2000), 0x7A7A);

    /* ---- block ops ---- */
    setup();                            /* LDIRB @r4,@r2,r6 */
    plant(0xBA21, 0x0641, 0);
    fr.r[2] = 0x2000; fr.r[4] = 0xFE10; fr.r[6] = 4; spusp = 0xFE00;
    datab[0x2000] = 1; datab[0x2001] = 2; datab[0x2002] = 3; datab[0x2003] = 4;
    run("ldirb", 0);
    chk("ldirb dst0", codeb[0xFE10], 1);         /* copied to stack bank */
    chk("ldirb dst3", codeb[0xFE13], 4);
    chk("ldirb src", fr.r[2], 0x2004);
    chk("ldirb cnt", fr.r[6], 0);
    chk("ldirb PV", fr.fcw & FV, FV);

    setup();                            /* LDI @r4,@r2,r6 (single) */
    plant(0xBB21, 0x0648, 0);           /* word LDI: src r2, w1 bit3 set */
    fr.r[2] = 0x2000; fr.r[4] = 0x3000; fr.r[6] = 2;
    dww(0x2000, 0x1122);
    run("ldi", 0);
    chk("ldi dst", drw(0x3000), 0x1122);
    chk("ldi cnt", fr.r[6], 1);
    chk("ldi no PV", fr.fcw & FV, 0);

    setup();                            /* CPIRB rl1,@r2,r0,eq: find byte */
    plant(0xBA24, 0x0096, 0);           /* cnt=r0, cmp=rl1(9), cc=6(eq) */
    fr.r[1] = 0x0033;                   /* rl1 = 0x33 */
    fr.r[2] = 0x2000; fr.r[0] = 8;
    datab[0x2002] = 0x33;
    run("cpirb", 0);
    chk("cpirb ptr", fr.r[2], 0x2003);  /* stops past the match */
    chk("cpirb Z", fr.fcw & FZ, FZ);
    chk("cpirb cnt", fr.r[0], 5);

    setup();                            /* CPIRB no match: count runs out */
    plant(0xBA24, 0x0096, 0);
    fr.r[1] = 0x0077; fr.r[2] = 0x2000; fr.r[0] = 3;
    run("cpirb miss", 0);
    chk("cpirb miss Z", fr.fcw & FZ, 0);
    chk("cpirb miss PV", fr.fcw & FV, FV);
    chk("cpirb miss cnt", fr.r[0], 0);

    /* ---- routing edges ---- */
    setup();                            /* read at SP exactly -> code */
    plant(0x21F3, 0, 0); spusp = 0x8000; cww(0x8000, 0x1234);
    dww(0x8000, 0x9999);
    run("edge @sp", 0);
    chk("edge @sp", fr.r[3], 0x1234);

    setup();                            /* read just below SP -> data */
    plant(0x2123, 0, 0); fr.r[2] = 0x7FFE; spusp = 0x8000;
    dww(0x7FFE, 0x5678); cww(0x7FFE, 0x9999);
    run("edge below", 0);
    chk("edge below", fr.r[3], 0x5678);

    /* ---- misses ---- */
    setup();                            /* genuine SC 255: no table entry */
    run("notpat", 1);

    setup();                            /* unimplemented (CPSIB) */
    plant(0xBA22, 0x0006, 0);
    run("unimp", 2);

    printf("%d tests, %d failures\n", ntest, nfail);
    return nfail != 0;
}
