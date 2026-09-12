/* CP/M 3 SCB byte offsets (ref/cpm3/resbdos.asm and scb.asm). Use a byte
 * array to avoid struct padding; word values are little-endian. */

/* Expansion area */
#define SCB_HASHL	0x00	/* hash length (0,2,3)			*/
#define SCB_HASH	0x01	/* hash entry (4 bytes)			*/
#define SCB_VERSION	0x05	/* 31h = CP/M 3.1			*/

/* Utilities section */
#define SCB_UTILFLGS	0x06	/* util$flgs (2 words)			*/
#define SCB_DSPLFLGS	0x0a	/* dspl$flgs				*/

/* Command line processor section */
#define SCB_CLPFLGS	0x0e	/* clp$flgs				*/
#define SCB_ERRCDE	0x10	/* clp$errcde = program return code	*/

/* CCP section */
#define SCB_CCPCOMLEN	0x12	/* ccp$comlen				*/
#define SCB_CCPCURDRV	0x13	/* ccp$curdrv				*/
#define SCB_CCPCURUSR	0x14	/* ccp$curusr				*/
#define SCB_CCPCONBUF	0x15	/* ccp$conbuff				*/
#define SCB_CCPFLGS	0x17	/* ccp$flgs (80h chain, 40h chain-env)	*/

/* Device I/O section */
#define SCB_CONWIDTH	0x1a	/* console width in columns		*/
#define SCB_COLUMN	0x1b	/* current console column		*/
#define SCB_CONPAGE	0x1c	/* console page length			*/
#define SCB_CONLINE	0x1d	/* line count on this page		*/
#define SCB_CONBUFADD	0x1e	/* console buffer address		*/
#define SCB_CONBUFLEN	0x20	/* console buffer length		*/
#define SCB_CIVEC	0x22	/* console input redirection vector	*/
#define SCB_COVEC	0x24	/* console output redirection vector	*/
#define SCB_AIVEC	0x26	/* auxiliary input redirection vector	*/
#define SCB_AOVEC	0x28	/* auxiliary output redirection vector	*/
#define SCB_LOVEC	0x2a	/* list output redirection vector	*/
#define SCB_PAGEMODE	0x2c	/* page$mode				*/
#define SCB_PMDEFAULT	0x2d	/* pm$default				*/
#define SCB_CTLHACT	0x2e	/* ctlh$act				*/
#define SCB_RUBOUTACT	0x2f	/* rubout$act				*/
#define SCB_TYPEAHEAD	0x30	/* type$ahead				*/
#define SCB_CONTRAN	0x31	/* contran				*/
#define SCB_CONMODE	0x33	/* console mode (function 109)		*/
#define SCB_BNKBF	0x35	/* banked-BIOS 128-byte buffer address	*/
#define SCB_OUTDELIM	0x37	/* function 9 output delimiter		*/
#define SCB_LISTCP	0x38	/* listcp				*/
#define SCB_QFLAG	0x39	/* qflag				*/

/* BDOS section */
#define SCB_SCBADD	0x3a	/* address of this image		*/
#define SCB_CRDMA	0x3c	/* current DMA address			*/
#define SCB_CRDSK	0x3e	/* currently selected disk		*/
#define SCB_VINFO	0x3f	/* BDOS variable INFO			*/
#define SCB_RESEL	0x41	/* FCB reselect flag			*/
#define SCB_RELOG	0x42	/* relog flag				*/
#define SCB_FX		0x43	/* function number in progress		*/
#define SCB_USRCD	0x44	/* current user code			*/
#define SCB_DCNT	0x45	/* directory scan position		*/
#define SCB_SEARCHA	0x47	/* address of the search FCB		*/
#define SCB_SEARCHL	0x49	/* search length			*/
#define SCB_MLTIO	0x4a	/* multi-sector count (function 44)	*/
#define SCB_ERMDE	0x4b	/* BDOS error mode (function 45)	*/
#define SCB_SRCHCHAIN	0x4c	/* drive search chain (4 bytes)		*/
#define SCB_TEMPDRIVE	0x50	/* temporary drive			*/
#define SCB_ERDSK	0x51	/* drive of the last BDOS error		*/
#define SCB_MEDIA	0x54	/* media (door-open) flag		*/
#define SCB_BFLGS	0x57	/* 80h = long error messages		*/
#define SCB_DATE	0x58	/* days since 1 Jan 78			*/
#define SCB_HOUR	0x5a	/* hour, BCD				*/
#define SCB_MIN		0x5b	/* minute, BCD				*/
#define SCB_SEC		0x5c	/* second, BCD				*/
#define SCB_COMMONBASE	0x5d	/* base of common memory		*/
#define SCB_ERJMP	0x5f	/* BDOS error message jump		*/
#define SCB_MXTPA	0x62	/* top of the user TPA			*/

#define SCBLEN		0x64	/* the image is 100 bytes		*/
#define SCBMAX		99	/* function 49 refuses offset >= 99	*/

/*  Function-49 parameter block, 4 bytes, exactly the CP/M 3 form:
    offset, set flag, then the value LOW BYTE FIRST.  */

#define SCBPB_OFF	0	/* offset into the SCB			*/
#define SCBPB_SET	1	/* 0FFh set byte, 0FEh set word, else get */
#define SCBPB_VALUE	2	/* value, low byte then high byte	*/
#define SCBPBLEN	4

#define SCBSET_BYTE	0xff
#define SCBSET_WORD	0xfe

/*  Values this port fixes rather than derives.  */

#define SCB_VER_VALUE	0x31	/* CP/M 3.1, as resbdos.asm ships it	*/
#define SCB_WIDTH_VALUE	80	/* console width; gencpm-c900 will own it */
#define SCB_BFLGS_VALUE	0x80	/* long error messages, always on here	*/
