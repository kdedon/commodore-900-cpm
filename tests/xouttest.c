/*  The segment number every x_sg entry gets.  Zero for the non-segmented
    images below, which never have it looked at; a SEGMENTED image is
    placed by raw CPU segment number, and pgmld refuses one whose entries
    do not name the TPA segment the MRT advertises, so the segmented
    checks in main() set this to 0x32 (c900cfg.h TPASEG) first.  */

static int gsgno = 0;

		s.x_sg_no = (char) gsgno;
	if (getenv("XOUTDBG")) {	/* for working on this harness	*/

		/*  The base page's geometry.  BPLEN is sizeof (struct
		    b_page) and the entry frame is sizeof (struct sstack) /
		    (struct ustack), all of which are HOST sizes here and
		    not the target's -- which is why every check in main()
		    is written against those names rather than against the
		    target's 0x100/0x200/8.  */

		if (k == 0) {
			struct b_page *d = (struct b_page *) xlate(lpb.bpaddr);

			fprintf(stderr,
				"[dbg] BPLEN=%ld DEFSTACK=%ld sstack=%ld"
				" ustack=%ld\n"
				"[dbg] bpaddr=%lx htpa=%lx lbss+bsslen=%lx"
				" freelen=%lx\n",
				(long) BPLEN, (long) DEFSTACK,
				(long) sizeof (struct sstack),
				(long) sizeof (struct ustack),
				(long) lpb.bpaddr, (long) d->htpa,
				(long) (d->lbss + d->bsslen),
				(long) d->freelen);
		}
	}
	struct b_page *bpp;		/* the base page the loader wrote */
	/*  FREELEN IS THE SPAN FROM THE END OF BSS TO THE STACK POINTER.
	    DRI defines the field as "Length of free memory after bss"
	    (pg/pgmac.tex:60), with the user stack at the highest address of
	    the TPA and the stack's maximum size equal to "the address of
	    the stack pointer minus the last address of the program"
	    (pg/pgm4f.tex:516-518).  So the end of the free span IS htpa,
	    the program's own initial SP.  Written as an equation rather
	    than a constant, because it has to hold whatever the image's
	    sizes are and whatever is resident above it.  */

	bpp = (struct b_page *) xlate(lpb.bpaddr);
	check(bpp->lbss + bpp->bsslen + bpp->freelen == bpp->htpa,
	      "freelen must run from the end of bss to the initial stack"
	      " pointer: the program owns everything between, and nothing above");

	/*  A SPLIT-I/D PROGRAM IS THE ONE FORMAT THAT KEEPS THE OLD
	    EXPRESSION, and it must: its bss is in the D bank (0x35) and its
	    stack is in the code bank (0x32), so htpa - (lbss + bsslen)
	    would subtract offsets in two DIFFERENT segments and produce
	    nonsense.  Its free span is the room left in the D bank, which
	    is what seglim - segsiz measures there.  With nothing resident
	    the D segment's limit is 0x10000 - BPLEN - DEFSTACK = 0xFE00,
	    and this image puts 0x180 of data and bss in it.  */

	bpp = (struct b_page *) xlate(lpb.bpaddr);
	check(bpp->freelen == (long) GSEGSZ - BPLEN - DEFSTACK - 0x180L,
	      "a split-I/D program's freelen must measure the room left in its"
	      " DATA BANK, not a distance to a stack pointer in another segment");

	/* ---- A SEGMENTED (0xEE01) IMAGE: THE CASE THE FREELEN FIX IS FOR.
	   Every segment's limit starts at SEGLEN - rsvd, and on this path
	   nothing used to reduce it for the base page and the stack: the
	   only reduction written is loadseg's X_SG_STK case, and it never
	   runs, because lout2cpm emits COD, DAT and BSS and no stack
	   segment.  So freelen reached 0x200 bytes past the top of what the
	   program may touch -- over its own base page and stack -- and
	   PIP's and STAT's sbrk ceiling (src/cmd/pipmain.c) went with it.
	   The anchors are exact: with nothing resident the stack goes at
	   SEGLEN - BPLEN - DEFSTACK and the segmented entry frame is 8
	   bytes, so the SP is 0xFDF8 -- the value src/cmd/crt0.s has always
	   documented as the entry rr14. */

	gsgno = 0x32;			/* c900cfg.h TPASEG: pgmld refuses a
					   segmented image whose segments
					   name any other		*/
	mkimage(X_SX_MAGIC, 3, 3, gtyp, glen, 0x280);
	k = load();
	gsgno = 0;
	check(k == 0, "a well-formed segmented image must load");
	bpp = (struct b_page *) xlate(lpb.bpaddr);
	check(bpp->htpa == GTPABASE + (long) GSEGSZ - BPLEN - DEFSTACK
			   - (long) sizeof (struct sstack),
	      "a segmented program's initial SP must sit below the base page"
	      " and the default stack the loader reserved for it");
	check(bpp->lbss + bpp->bsslen + bpp->freelen == bpp->htpa,
	      "A SEGMENTED PROGRAM'S FREELEN MUST STOP AT ITS OWN STACK"
	      " POINTER: counting to the segment limit handed it its own base"
	      " page and stack as free memory");
	check(bpp->freelen == bpp->htpa - (GTPABASE + 0x380L),
	      "a segmented program's freelen must be exactly the span from the"
	      " end of bss to the stack pointer");

	/*  And the reservation that makes the equation above satisfiable: an
	    image whose text+data+bss would collide with the base page and
	    the stack is now REFUSED, where before it was loaded on top of
	    them and freelen went negative.  0xFF00 of segment leaves no
	    room for the 0x200 bytes the loader puts at the top.  The
	    refusal comes before any data is read, so the image needs no
	    real content.  */

	gsgno = 0x32;
	btyp[0] = X_SG_COD;
	blen[0] = 0xFF00;
	mkimage(X_SX_MAGIC, 1, 1, btyp, blen, 0x200);
	k = load();
	gsgno = 0;
	check(k == NOMEM,
	      "a segmented image that would overwrite the base page and stack"
	      " the loader is about to write must be refused, not loaded");

		       "          segment arrays, good images load before and after, a\n"
		       "          untouched, and freelen ends at the stack pointer for\n"
		       "          segmented and non-segmented images alike\n",
