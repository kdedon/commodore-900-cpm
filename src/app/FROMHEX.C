/* -*-c,save-*- */
#include <stdio.h>
#define FAST register
#define LOCAL static
#define GLOBAL extern
#define BLOCK_SIZE 32


main(argc,argv)
int argc;
char *argv[];
{FAST FILE *infile,*outfile;
 FILE *fopenb(),*fopen();
 char block_buff[BLOCK_SIZE];
 FAST int mrec,bytes;
 
 	if (argc != 3) {
	    fprintf(stderr,"Usage: fromhex infile outfile\n");
	    abort(1);
	    }
	infile = fopen(argv[1],"r");
	if (infile == NULL) {
	    perror("fromhex: fopen error (input file)");
	    abort(2);
	    }
	outfile = fopenb(argv[2],"w");
	if (outfile == NULL) {
	    perror("fromhex: fopen error (output file)");
	    abort(2);
	    }
	mrec = 0;
	while ((bytes = get_hex(&block_buff[0],mrec,BLOCK_SIZE,infile)) > 0) {
	    fwrite(&block_buff[0],1,bytes,outfile);
	    mrec++;
	    }
	fclose(infile);
	fclose(outfile);
	printf("Blocks read: %04x\n",mrec);
	}

get_hex(buff,recnum,buffsiz,infile)
FAST char *buff;
FAST int buffsiz;
int recnum;
FAST FILE *infile;
{
    FAST int n,chk2,b;
    int checksum,rec_num,block_len,byte;
    
    /*  The semicolon search used to be `while (getc(infile) != ';') ;'
	with no EOF test, so a file with no semicolon left in it -- a
	truncated transfer, or any file that is not hex at all -- spun for
	ever on getc()'s -1.  A well-formed stream ends with the
	zero-length record TOHEX.C:35 always writes, not with end of file,
	which is how this got away with it; end of file now returns 0 and
	main()'s `> 0' loop stops.  (c900)  */
    while ((n = getc(infile)) != ';')
	if (n == EOF)		/* stdio.h -> portab.h:44, the same EOF
				   SORTFL.C and KILLDU.C already use */
	    return (0);
    fscanf(infile,"%02x%04x",&block_len,&rec_num);
    chk2 = block_len + (rec_num & 0x00ff) + ((rec_num >> 8) & 0x00ff);
    b = 0;
    /*  ... and the limit used to be tested AFTER the store, so a record
	whose length byte was bigger than the buffer wrote byte 33 of a
	32-byte buffer and only then said "Buffer overflow".  Test first.
	(c900)  */
    while ((block_len--)>0) {
	if ((buffsiz--)<1) {fprintf(stderr,"Buffer overflow\n");abort(3);}
	fscanf(infile,"%02x",&byte);
	byte &= 0x00ff;
	chk2 += byte;
	*buff++ = byte;
	b++;
	}
    fscanf(infile,"%04x",&checksum);

    if (checksum != chk2) {
	fprintf(stderr,"Checksum error, recnum=%04x\n",rec_num);
	abort(4);
	}
    if (recnum != rec_num) {
	fprintf(stderr,"Phase error, expect record %04x, got record  %04x\n",
		recnum,rec_num);
	abort(5);
	}
    return(b);
    }
