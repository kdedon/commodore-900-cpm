/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * zsplit.h -- shared Z8001 nonsegmented-code decoder for the split-I/D
 * loader shim (Option 6, 0xEE0B support).
 *
 * One decode serves two consumers:
 *   - the load-time linear scanner (zscan1/zscan2 in zsplit.c), which walks
 *     a 0xEE0B binary's code segment and classifies every instruction as
 *     data-space-referencing (patched to an SC trap) or not;
 *   - the SC trap handler (splitsc.c), which re-decodes one patched
 *     instruction at trap time and emulates its data access against the
 *     data bank.
 *
 * The file compiles both with the MWC target pipeline (int = 16, K&R) and
 * with a host C compiler for offline validation (-DHOSTCC).  `zw' is
 * unsigned short: 16 bits in both worlds.
 */

#define zw	unsigned short

/* Classification of one instruction (zid.klass). */
#define ZK_NONE	0	/* no memory operand, or control flow / privileged:
			 * executes natively in the code bank */
#define ZK_PROG	1	/* memory operand in PROGRAM space (LDR family):
			 * native execution reads the code bank -- correct.
			 * The scanner records the referenced words as
			 * embedded text data (never to be patched). */
#define ZK_DATA	2	/* memory operand in DATA space: patched to SC */
#define ZK_ILL	3	/* not a Z8001 instruction (decode desync / data) */

/* Addressing mode of the memory operand (zid.mode). */
#define ZM_NONE	0
#define ZM_IR	1	/* @Rn        (rb = address register)		*/
#define ZM_DA	2	/* addr       (address = word 1)		*/
#define ZM_X	3	/* addr(Rn)   (word 1 + index register rb)	*/
#define ZM_BA	4	/* Rn(#disp)  (base register rb + word 1)	*/
#define ZM_BX	5	/* Rn(Rx)     (base rb + index (word1 >> 8))	*/

/* Operand width (zid.width). */
#define ZW_B	0
#define ZW_W	1
#define ZW_L	2

/* Operation for the emulate step (zid.op; meaningful when ZK_DATA). */
#define ZOP_LOAD	1	/* reg ra <- mem			*/
#define ZOP_STORE	2	/* mem <- reg ra			*/
#define ZOP_STIMM	3	/* mem <- immediate (last insn word)	*/
#define ZOP_CLR		4	/* mem <- 0				*/
#define ZOP_ALUR	5	/* reg ra <- reg ra <aop> mem (A_CP: flags) */
#define ZOP_ALUM	6	/* mem <- mem <aop> (RMW; aop selects)	*/
#define ZOP_TESTM	7	/* flags from mem (TEST/TESTL/BIT/CP-imm) */
#define ZOP_EX		8	/* swap reg ra <-> mem			*/
#define ZOP_MUL		9	/* reg group ra *= mem			*/
#define ZOP_DIV		10	/* reg group ra /= mem			*/
#define ZOP_LDM		11	/* load multiple from mem		*/
#define ZOP_STM		12	/* store multiple to mem		*/
#define ZOP_PUSH	13	/* push mem operand (sp reg = ra)	*/
#define ZOP_POP		14	/* pop to mem operand (sp reg = ra)	*/
#define ZOP_PUSHI	15	/* push immediate word 1 (sp reg = ra)	*/
#define ZOP_BLKT	16	/* LDI/LDD(R) block transfer		*/
#define ZOP_BLKC	17	/* CPI/CPD(R) block compare (reg vs mem) */
#define ZOP_BLKS	18	/* CPSI/CPSD(R) string compare (mem vs mem) */
#define ZOP_TRANS	19	/* TRxB/TRTxB translate			*/
#define ZOP_SC		20	/* SC (klass ZK_NONE; counted by tools)	*/

/* ALU sub-operation (zid.aop, for ZOP_ALUR / ZOP_ALUM / ZOP_TESTM). */
#define A_ADD	1
#define A_SUB	2
#define A_OR	3
#define A_AND	4
#define A_XOR	5
#define A_CP	6	/* flags only				*/
#define A_COM	7
#define A_NEG	8
#define A_TEST	9	/* flags only				*/
#define A_TSET	10
#define A_INC	11	/* count = ra + 1			*/
#define A_DEC	12	/* count = ra + 1			*/
#define A_SET	13	/* bit number = ra			*/
#define A_RES	14	/* bit number = ra			*/
#define A_BIT	15	/* flags only; bit number = ra		*/
#define A_CPIMM	16	/* CP mem,#imm (flags only)		*/

struct zid {
	short	len;	/* instruction length in words, 1..4	*/
	short	klass;	/* ZK_xxx				*/
	short	op;	/* ZOP_xxx (ZK_DATA only)		*/
	short	width;	/* ZW_xxx				*/
	short	mode;	/* ZM_xxx				*/
	short	ra;	/* register operand / sp / bit# / count	*/
	short	rb;	/* address base / index register	*/
	short	aop;	/* A_xxx for ALU forms			*/
	short	blk;	/* block-op variant bits (ZB_xxx)	*/
};

/* Block-op variant bits (zid.blk, for ZOP_BLKT/BLKC/BLKS/TRANS). */
#define ZB_DECR	1	/* pointers decrement			*/
#define ZB_REPT	2	/* repeating (until count exhausted)	*/

/* Scanner fast-classify table (zqk in zsplit.c), indexed by an
 * instruction's high byte.  Encodes everything the linear scanner needs
 * for the common families without a full zdecode(); splitchk.c verifies
 * every entry against zdecode() over all 65536 first words.
 *   ZQ_FULL    full zdecode() required (length or class varies by more
 *              than nibble 2, or the family has ZK_ILL/ZK_PROG members)
 *   1..3       no data/program reference: skip that many words
 *   ZQ_D1/D2   ZK_DATA of fixed length 1/2 for every low byte
 *   ZQ_I2D1    nibble 2 == 0: immediate form, skip 2; else ZK_DATA len 1
 *   ZQ_I3D1    nibble 2 == 0: immediate form, skip 3; else ZK_DATA len 1
 *   ZQ_B2      nibble 2 == 0: LDR family, full zdecode (ZK_PROG marking);
 *              else base-address form, ZK_DATA len 2
 * A ZK_DATA met through the table reaches the callback with only the
 * id's len and klass fields valid (the load-time patcher needs nothing
 * more; the trap handler re-decodes at trap time). */
#define ZQ_FULL	0
#define ZQ_D1	4
#define ZQ_D2	5
#define ZQ_I2D1	6
#define ZQ_I3D1	7
#define ZQ_B2	8

/* Scanner context: `text' is the code image (host copy or the loaded TPA
 * text via a far pointer), nw its size in words.  `mark' is a bitmap of
 * one bit per text word: bit set = word is program-space DATA (an LDR/LDRL
 * target) and must never be patched.  Its size is (nw + 7) / 8 bytes. */
struct zsctx {
	zw	*text;
	zw	nw;
	char	*mark;
};
