/* Shared CP/M-80 decoder, executor, loader, and BDOS bridge interfaces.
 * Guest memory is one 64 KB host segment. Cast effective addresses to z16
 * for wraparound and access words bytewise in little-endian order. */

#define z8	unsigned char		/* a guest byte			*/
#define z16	unsigned short		/* a guest word; 16 bits both ways */
#define z32	unsigned long		/* 32 bits both ways		*/

/* ------------------------------------------------------------------ */
/* guest state							       */

/* Register-pair slots, in the 8080's own rp-field encoding.  Each pair
 * is held as ONE 16-bit word with the 8080's high byte in the high half
 * -- B over C, D over E, H over L -- because that is how the machine's
 * own 16-bit operations (DAD, INX, PUSH) see it, and because it is what
 * makes the pair a native Z8001 word needing no byte swap at all
 * (Z80-SHIM-FEASIBILITY.md §2).  Byte access goes through getr()/setr(),
 * never through a char overlay, since the host and the Z8000 disagree
 * about which end of a word a byte lives at. */
#define P_BC	0
#define P_DE	1
#define P_HL	2
#define P_SP	3

/* Byte-register slots, in the 8080's r-field encoding.  6 is not a
 * register: it is the memory byte at (HL). */
#define R_B	0
#define R_C	1
#define R_D	2
#define R_E	3
#define R_H	4
#define R_L	5
#define R_M	6
#define R_A	7

/* 8080 flag byte: S Z 0 AC 0 P 1 CY.  Bits 5 and 3 read back as zero and
 * bit 1 as one on an 8080; the Z80 puts undefined copies of the result
 * in 5 and 3 instead, which is a stage-two divergence and is written
 * down at F_Z80X below rather than left to be discovered. */
#define F_CY	0x01
#define F_ONE	0x02		/* always set on an 8080		*/
#define F_PA	0x04		/* parity, EVEN = set			*/
#define F_AC	0x10		/* half carry				*/
#define F_ZE	0x40
#define F_SI	0x80

/* The five the lazy record owns.  F_ONE is a constant and never lazy. */
#define F_LAZY	(F_CY|F_PA|F_AC|F_ZE|F_SI)

/* The bits an 8080 reads back as zero.  PUSH PSW and a `POP PSW' round
 * trip are the only things that can see them, and period code does
 * compare a pushed flag byte, so the mask is applied on every store. */
#define F_Z80X	0x28		/* Z80 bits 5 and 3: not ours, cleared	*/

/* Lazy-flag operation classes (struct z80 .lz).  LZ_NONE means `f' is
 * complete and nothing is pending.  Every 8080 record is BYTE wide --
 * DAD is the only 16-bit flag-setting instruction and it touches CY
 * alone, so it is computed eagerly and there is no width field here.
 * That is a real simplification over i86exec.c and it is the reason this
 * file has no `lw'. */
#define LZ_NONE	0
#define LZ_ADD	1	/* r = a + b + lc  -- S Z AC P CY		*/
#define LZ_SUB	2	/* r = a - b - lc  -- ditto (CMP, SBB use it)	*/
#define LZ_AND	3	/* ANA: AC comes from (a|b) bit 3, see below	*/
#define LZ_LOG	4	/* ORA/XRA: CY = AC = 0				*/
#define LZ_INR	5	/* r = a + 1       -- CY PRESERVED		*/
#define LZ_DCR	6	/* r = a - 1       -- CY preserved		*/

/* The prefix-group classes.  These four have no 8080 form at all: the
 * encodings that set them do not exist on an 8080, so where the two
 * parts' flag rules differ these follow the Z80's -- there is nothing
 * else to follow.  (The AC/half-carry POLARITY is the one exception and
 * it is stated at LZ_CPBLK.)  All four keep the byte-wide record the
 * eight-bit classes above use; the 16-bit ADC/SBC HL are computed
 * eagerly at their case, exactly as DAD already is, which is why there
 * is still no width field here. */
#define LZ_ROT	7	/* CB rotate/shift: CY = lc, AC = 0, P = parity	*/
#define LZ_BIT	8	/* BIT b,r: CY PRESERVED, AC set, r = the masked */
			/* byte -- so P = parity is the Z80's P/V = Z	*/
#define LZ_LDBLK 9	/* LDI/LDD/LDIR/LDDR: S Z CY PRESERVED, AC = 0,	*/
			/* P = lc = (BC != 0 after the transfer)		*/
#define LZ_CPBLK 10	/* CPI/CPD/CPIR/CPDR: like LZ_SUB but CY is	*/
			/* PRESERVED and P = lc = (BC != 0), not parity	*/

struct z80 {
	z16	rp[4];		/* BC DE HL SP, high byte in the high half */
	z8	a;
	z8	f;		/* materialised flags			*/
	z16	pc;

	/* The Z80 additions.  IX/IY and the alternate set are declared
	 * here because the loader and the state dump have to be stable
	 * across the stage that fills them in; stage one writes AF' and
	 * reads it back (EX AF,AF' is in the corpus) and touches nothing
	 * else.  Z80-SHIM-FEASIBILITY.md §3 notes that IX/IY fit the
	 * target's R8/R9 and the alternate set does not; that is a
	 * register-allocation problem for the assembly path and no
	 * problem at all here. */
	z16	ix, iy;
	z16	arp[3];		/* BC' DE' HL'				*/
	z8	aa, af;		/* A' F'				*/
	z8	iff;		/* the interrupt enable; EI/DI set it	*/

	z8	lz;		/* LZ_*: pending record, or LZ_NONE	*/
	z8	lc;		/* its carry-in (ADC/SBB); 0 otherwise	*/
	z16	la, lb, lr;	/* its operands and result		*/

	z8	halt;		/* set by HLT and by a refusal		*/
	char	*m;		/* the guest's 64 KB, one host segment	*/
};

/* ------------------------------------------------------------------ */
/* decoder							       */

/* Operations.  One id per executable class, not per opcode: the ALU
 * eight, the rotate four and the eight conditions carry their variant in
 * .x exactly as the hardware encodes them, so the executor's switch is
 * over classes and its inner dispatch is over a field the opcode already
 * gave us. */
#define Z_BAD		0	/* not decodable as an instruction	*/
#define Z_NOP		1
#define Z_LDRR		2	/* MOV r,r'   .x = dst, .y = src	*/
#define Z_LDRI		3	/* MVI r,n    .x = dst, .imm = n	*/
#define Z_ALU		4	/* .x = 0..7 add adc sub sbb ana xra ora cmp */
				/* .y = src reg, or ZF_IMM and .imm	*/
#define Z_INR		5	/* .x = reg				*/
#define Z_DCR		6
#define Z_LXI		7	/* .x = rp, .imm = nn			*/
#define Z_DAD		8	/* .x = rp				*/
#define Z_INX		9
#define Z_DCX		10
#define Z_LDAX		11	/* .x = 0 (BC) or 1 (DE)		*/
#define Z_STAX		12
#define Z_LDA		13	/* .imm = address			*/
#define Z_STA		14
#define Z_LHLD		15
#define Z_SHLD		16
#define Z_ROT		17	/* .x = 0 RLC 1 RRC 2 RAL 3 RAR		*/
#define Z_DAA		18
#define Z_CMA		19
#define Z_STC		20
#define Z_CMC		21
#define Z_JMP		22	/* .imm = target			*/
#define Z_JCC		23	/* .x = condition 0..7, .imm = target	*/
#define Z_CALL		24
#define Z_CCC		25
#define Z_RET		26
#define Z_RCC		27
#define Z_RST		28	/* .x = 0..7; target is .x * 8		*/
#define Z_PCHL		29
#define Z_SPHL		30
#define Z_XTHL		31
#define Z_XCHG		32
#define Z_PUSH		33	/* .x = rp, 3 meaning PSW		*/
#define Z_POP		34
#define Z_IN		35	/* no port hardware here		*/
#define Z_OUT		36
#define Z_EI		37
#define Z_DI		38
#define Z_HLT		39
/* --- the Z80 base-map additions.  These eight opcodes are the whole of
 * the Z80 that DRI's own CP/M 3 corpus reaches (Z80-STAGE-ONE.md §1.2 --
 * 395 JR, one DJNZ, two EX AF,AF', no EXX, and no CB/ED/DD/FD at all,
 * over 50,800 statically reachable instructions in 22 binaries), which
 * is why they are here and the CB/ED/DD/FD groups below are not. */
#define Z_JR		40	/* .x = 4 unconditional, else cc 0..3	*/
#define Z_DJNZ		41
#define Z_EXAF		42	/* EX AF,AF'				*/
#define Z_EXX		43
/* --- decoded for their LENGTH and their name, never executed in stage
 * one.  A decoder that stopped at "the forms PIP uses" could not tell an
 * instruction we have not implemented from bytes that are not an
 * instruction, and those are different findings. */
#define Z_CB		44	/* CB xx:  rotates and bit operations	*/
#define Z_ED		45	/* ED xx:  block moves, 16-bit arithmetic */
#define Z_IX		46	/* DD/FD xx: the index-register overlay	*/
#define Z_IXCB		47	/* DD/FD CB d xx			*/
#define Z_HOOK		48	/* ED FE nn: OUR escape, see below	*/

/* ED FE nn is the shim's three-byte hook encoding: one dispatch path
 * serves BDOS, BIOS vectors, and exit. Z80 prefixes remain decodable. */
#define HOOK_BDOS	0	/* the CALL 5 bridge			*/
#define HOOK_BIOS	1	/* .. through 17: BIOS vectors 1-17	*/
#define HOOK_EXIT	31	/* the guest returned to the CCP	*/
#define HOOK_MAX	31

/* struct z80in .fl bits */
#define ZF_IMM		0x01	/* .imm holds an immediate operand	*/
#define ZF_MEM		0x02	/* an operand is the byte at (HL)	*/
#define ZF_ADDR		0x04	/* .imm is an address, not a datum	*/
#define ZF_PFX		0x08	/* a CB/ED/DD/FD prefix was consumed	*/

/* One decoded instruction.  Nothing here depends on register contents,
 * so a decode is reusable and a decode cache is possible later. */
struct z80in {
	z8	len;		/* total length, prefix included	*/
	z8	op;		/* Z_*					*/
	z8	fl;		/* ZF_*					*/
	z8	x, y;		/* per-op operand fields: see the Z_* list */
	z8	pfx;		/* the prefix byte, 0 if none		*/
	z8	sub;		/* the byte after the prefix		*/
	z16	imm;		/* immediate, or a branch target	*/
	z16	disp;		/* DD/FD displacement, sign-extended	*/
};

/* z80dec: decode the instruction at m[pc].  Fills *in and returns its
 * length, which is at least 1 even for Z_BAD so a linear sweep can step
 * past a byte that is not an instruction. */
extern int z80dec();
extern char *z80mnem();		/* mnemonic of a decoded instruction	*/

/* The eight condition codes in encoding order -- NZ Z NC C PO PE P M.
 * Lives beside the encodings that produce it because Jcc, Ccc, Rcc and
 * the four JR forms all consume it and a second copy would be a second
 * thing to get wrong.  (f, cc) -> 0 or 1. */
extern int z80cond();

/* ------------------------------------------------------------------ */
/* executor							       */

#define X_OK		0	/* executed; PC advanced		*/
#define X_UNIMP		1	/* decoded, not implemented -- refuse	*/
#define X_BAD		2	/* not an instruction -- refuse		*/
#define X_HOOK		3	/* an ED FE escape the caller must service; */
				/* the hook number is in z80hookno	*/
#define X_HALT		4	/* HLT					*/

extern int z80step();		/* decode + execute one instruction	*/
extern z8 z80flags();		/* materialise and return F		*/
extern int z80hookno;		/* hook left by X_HOOK			*/

/* Instruments.  Instructions executed, and lazy records actually
 * materialised.  Their ratio is the flag-read rate: the study's §4 calls
 * parity the one genuine gap and costs a fix-up at 27 target cycles on
 * every arithmetic instruction, so how often a flag is READ rather than
 * written is what decides whether that cost is paid or deferred.
 * Z80-STAGE-ONE.md §6 K2 asks for the number; these are how it is taken,
 * on the host, before any target code exists. */
extern z32 z80ninsn, z80nflag;

/* Byte and pair access, exported because the loader, the seam and the
 * tests all need them and because r = 6 meaning memory is a rule that
 * must be stated once. */
extern int z80getr();		/* (m, r) -> byte; r = 6 is (HL)	*/
extern int z80setr();		/* (m, r, v)				*/
extern int z80rb();		/* (m, addr) -> byte			*/
extern int z80wb();		/* (m, addr, v)				*/
extern z16 z80rw();		/* (m, addr) -> word, little-endian	*/
extern int z80ww();		/* (m, addr, v)				*/

/* ------------------------------------------------------------------ */
/* .COM loader							       */

/* The loader places a .COM at 0x100, builds page zero and hook stubs,
 * and identifies/refuses GENCOM RSX headers (leading 0xc9). */

#define COM_ORG		0x0100	/* where a .COM loads and starts	*/
#define COM_REC		128	/* a CP/M record; .COM files are padded	*/

/* Guest page-zero offsets, all CP/M-80 canon. */
#define PZ_WBOOT	0x0000	/* JMP to the BIOS warm-boot vector	*/
#define PZ_IOBYTE	0x0003
#define PZ_CDISK	0x0004
#define PZ_BDOS		0x0005	/* JMP to the BDOS entry		*/
#define PZ_FCB1		0x005c
#define PZ_FCB2		0x006c
#define PZ_DMA		0x0080	/* command tail, and the default DMA	*/

/* Where the shim's own furniture lives inside the guest segment.  Chosen
 * to leave the guest a TPA larger than a Kaypro's (study §1.3): 0x0100
 * to GUESTTOP is 57.6 KB. */
#define GUESTTOP	0xe400	/* first byte the guest TPA does not own */
#define FAKEBDOS	0xe406	/* the CALL 5 target: an ED FE 00 hook	*/
#define FAKEBIOS	0xf200	/* 17 JMPs, one per CP/M-80 BIOS vector	*/
#define NBIOSV		17

/* FAKEBDOS is GUESTTOP + 6 and the 6 is load-bearing.  A CP/M-80
 * program finds the top of the TPA by reading the ADDRESS FIELD of the
 * `JMP' at 5 -- `LHLD 6' -- and subtracting nothing: the word at 6 is
 * the BDOS entry, and the entry is six bytes above the first byte the
 * program may not use, because a real BDOS's serial number occupies
 * those six.  So placing the hook at GUESTTOP+6 makes `LHLD 6' answer
 * 0xE406 and the program compute a TPA ending at 0xE400, which is where
 * it does end.  Getting this off by six is not a crash; it is a program
 * writing its buffer over the first bytes of our furniture. */

/* The exit stub: three bytes of `ED FE 1F' at GUESTTOP, and the word
 * the guest's initial SP points at.  A .COM that terminates with a
 * plain RET -- which is the documented CP/M-80 way and what DUMP.COM
 * does -- lands here.  A .COM that terminates with BDOS function 0 or
 * a JMP 0 lands on the BDOS or WBOOT hook instead, and all three are a
 * clean exit. */
#define FAKEEXIT	GUESTTOP

/* z80load() return values.  Every one is a loud refusal; none is a
 * fallback.  CL_RSX is the 0xC9 prefix described above -- kill criterion
 * K1 (Z80-STAGE-ONE.md §6), and unlike CP/M-86's K1 it is a check that
 * DOES fire, on six real files we hold. */
#define CL_OK		0
#define CL_EMPTY	1	/* a zero-length file			*/
#define CL_BIG		2	/* the image does not fit under GUESTTOP */
#define CL_RSX		3	/* K1: a GENCOM-bound .COM with an RSX	*/
#define CL_NOTCOM	4	/* the first byte cannot begin a program	*/

extern int z80load();		/* place an image and build page zero	*/
extern char *z80lerr();		/* the refusal text for a CL_* code	*/
extern int z80tail();		/* build the tail and the two FCBs	*/
extern int z80furn();		/* plant the fake BDOS/BIOS and page zero */

/* GENCOM header layout follows cpm8000/ref/cpm3/loader3.asm:
 * program size at 1, SCB setup at 3, descriptors at 0x10 with 0x10 stride.
 * A zero descriptor offset ends the list; RET at image offset 0x100
 * identifies an RSX-only file. */
#define RSX_NDESC	8
#define RSX_HDRLEN	0x100	/* the header record, file 0x000-0x0FF	*/
#define RSX_DESC0	0x010	/* first descriptor, file offset	*/
#define RSX_DSTRIDE	0x010	/* loader3.asm:234 `lxi d,10h'		*/

struct comrsx {
	z16	comlen;		/* 0x001: the program's length in bytes	*/
	int	rsxonly;	/* 0xC9 at the image base: no program	*/
	int	n;		/* descriptors before the zero offset	*/
	z16	off[RSX_NDESC];	/* file offset of each RSX image	*/
	z16	len[RSX_NDESC];	/* its length in bytes			*/
	z8	nbank[RSX_NDESC];   /* +4: non-banked-only (rsxhdr.h:0F)  */
	char	name[RSX_NDESC][9]; /* +6: eight blank-padded characters */
};

extern int z80rsxhdr();		/* parse a 0xC9 prefix; 0 if not one	*/

/*
 * The loader's four entry points, with their arguments, because K&R
 * declarations carry none and a caller that guesses wrong here corrupts
 * the guest rather than failing to link.
 *
 *	int  z80load(m, mem, img, n)	struct z80 *m; char *mem, *img;
 *					long n;
 *		Bind `mem' -- 65,536 bytes the caller owns -- as the
 *		guest's address space, clear it, plant the furniture,
 *		place `n' bytes of `img' at 0x0100 and set the registers
 *		a .COM starts with.  Returns CL_OK or a refusal.
 *	int  z80tail(m, tail)		struct z80 *m; char *tail;
 *		Parse a command tail into 0x0080 and the two default
 *		FCBs, exactly as the CCP does.  Returns the tail length.
 *	int  z80furn(m)			struct z80 *m;
 *		Page zero, the fake BDOS, the 17-entry fake BIOS and the
 *		exit stub.  Called by z80load(); separate because the
 *		tests check it on its own.
 *	int  z80rsxhdr(img, n, r)	char *img; long n;
 *					struct comrsx *r;
 *		0 if `img' does not begin with the 0xC9 prefix, else 1
 *		with *r filled in.
 */
extern int z80load();
extern int z80tail();
extern int z80furn();

/* ------------------------------------------------------------------ */
/* the CALL 5 / BIOS seam (z80bdos.c)				       */

/* A CP/M-80 program calls the operating system with C = the function
 * number and DE = the parameter -- a byte in E, a word, or an address --
 * and reads its answer out of A (a byte) and HL (a word, with A = L and
 * B = H, which is what CP/M-80 itself leaves behind).  That is the whole
 * interface, and it is the reason this file is small: it maps one
 * calling convention onto another and hands the call to the native
 * BDOS, which reads the guest's FCB where the guest left it.  Nothing is
 * copied. */

/* What servicing a hook did.  Only B_RUN resumes the guest. */
#define B_RUN	0		/* serviced; carry on			*/
#define B_EXIT	1		/* the guest terminated			*/
#define B_FN	2		/* a BDOS function stage one does not map */
#define B_ADDR	3		/* its parameter ran off the top of the	*/
				/* guest's 64 KB			*/
#define B_BIOS	4		/* a BIOS vector stage one does not map	*/
#define B_HOOKNO 5		/* a hook number we never planted	*/

extern int z80bdos();		/* service the pending z80hookno	*/
extern int z80bdosinit();	/* the DMA address a program starts with */
extern int z80oflush();		/* send collected fn 2 output as one fn 111 */
extern char *z80berr();		/* one sentence about the last refusal	*/
extern int z80bdosfn;		/* the function that asked, or -1	*/
extern int z80biosfn;		/* the BIOS vector that asked, or -1	*/
extern z32 z80nbdos;		/* calls serviced			*/
extern z16 z80dma;		/* the guest's DMA address (fn 26)	*/
extern z16 z80ver;		/* what function 12 tells the guest	*/

/* Host address of `len' guest bytes at `off', or 0 when they do not all
 * lie below 0x10000.  The seam hands FCBs and DMA buffers to the native
 * BDOS by address, so this is the check that stands between a guest DMA
 * address of 0xFFC0 and our BDOS writing 128 bytes into the segment next
 * door.  It does NOT wrap, deliberately: guest wraparound is free for
 * the GUEST's own accesses (z80exec.c) because the hardware does it, but
 * the native BDOS is handed a flat pointer and would run straight on. */
extern char *z80addr();

/*
 * The one thing z80bdos.c does not contain: the native BDOS call.
 *
 *	int z80sys(fn, val, addr)
 *	int fn;  z16 val;  char *addr;
 *
 * `addr' is a HOST pointer into the guest's 64 KB when the function
 * takes an address and 0 otherwise, in which case `val' is the byte or
 * word parameter.  The target's main() is one line around __bdos(), and
 * the host tests supply a stub, which is what lets `make z80test'
 * exercise the whole mapping with no toolchain and no emulator.
 */
extern int z80sys();
