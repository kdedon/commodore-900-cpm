/* SDB - boolean expression evaluator */ /* -*-c,save-*- */

#include "sdbio.h"

/* #define DEBUG	1	/* debug hackery */
#ifdef DEBUG
#define LOCAL
#else
#define LOCAL static
#endif


LOCAL struct operand *stack[STACKMAX],**sptr;
LOCAL union codecell *cptr;
LOCAL int stkovf;               /* the push ran out of stack (c900) */

/* db_interpret - interpret a boolean expression */
int db_interpret(slptr)
  register struct sel *slptr;
{
    register struct operand *result;
    register int r;
    register union codecell *tcptr;

    /* check for empty where clause */
    if ((cptr = slptr->sl_where) == NULL)
        return (TRUE);

    /* setup stack */
    sptr = stack;
    stkovf = FALSE;

    /* execute the code */
    do {
	tcptr = cptr;
	cptr++;
	} while ((*(*tcptr).c_operator)());

    /*  If db_xpush ran out of stack it stopped the loop early, so there is
	no result on top -- only unconsumed operands.  Discard them and say
	the tuple does not match.  db_compile refuses such an expression
	before it ever gets here, so this path is defence and not the fix.
	(c900)  */
    if (stkovf) {
	while (sptr != stack) {
	    sptr -= 1;
	    if ((*sptr)->o_type == TEMP)
		free(*sptr);
	}
	return (FALSE);
    }

    /* get the result from the top of stack */
    result = *--sptr;
    r = result->o_value.ov_boolean;
    if (result->o_type == TEMP)
        free(result);

    /*  Make sure the stack is empty.  This used to free *sptr BEFORE
	decrementing, so the first thing it freed was `result' again and
	stack[0] was never freed at all; it was harmless only because a
	well-formed expression always left sptr == stack here and the loop
	never ran once.  (c900)  */
    while (sptr != stack) {
	sptr -= 1;
        if ((*sptr)->o_type == TEMP)
            free(*sptr);
    }

    /* return result */
    return (r);
}

int db_xstop()
{
#ifdef DEBUG
    printf("*** xstop()\n");
#endif
    return (FALSE);
}

/*  The push had no depth check against stack[STACKMAX], so an expression
    that left more than STACKMAX operands pending wrote past the array and
    on into whatever follows it in the data segment.

    CODEMAX does NOT already bound this, though it nearly does: COM.C
    compiles left-associatively, so a flat `a>1 & a>1 & ...' never holds
    more than three operands, and nesting built out of COMPARISONS costs
    six code cells a level (two pushes and an operator, then the `&'), so
    CODEMAX's hundred cells refuse it at depth seventeen -- three short.
    What gets past STACKMAX is that relat()'s comparison loop is optional:
    a bare factor is one push plus one `&', three cells a level, and
    `"x" & ("x" & ( ... ))' thirty deep fits inside CODEMAX and pushes
    thirty-one operands.  (c900)  */

int db_xpush()
{
#ifdef DEBUG
    printf("*** xpush(): sptr = %08X, cptr = %08X\n", sptr, cptr);
#endif
    if (sptr >= &stack[STACKMAX]) {
        stkovf = TRUE;
        return (FALSE);         /* stops the do-while in db_interpret */
    }
    *sptr++ = (*cptr++).c_operand;
    return(TRUE);
}

int db_xand()
{
    return (boolean('&'));
}

int db_xor()
{
    return (boolean('|'));
}

LOCAL int boolean(opr)
{
    register struct operand *lval,*rval,*result;
    register int lv,rv,r;

    rval = *--sptr; lval = *--sptr;
    lv = lval->o_value.ov_boolean;
    rv = rval->o_value.ov_boolean;
#ifdef DEBUG
    printf("*** boolean(%c): lval = %08X, rval = %08X, lv = %d, rv = %d\n",
	   opr, lval, rval, lv, rv);
#endif

    if ((result = malloc(sizeof(struct operand))) == NULL)
        return (db_ferror(INSMEM));
    result->o_type = TEMP;
    switch (opr) {
    case '&':   r = (lv && rv);
                break;
    case '|':   r = (lv || rv);
                break;
    }
    result->o_value.ov_boolean = r;
    *sptr++ = result;
    if (lval->o_type == TEMP)
        free(lval);
    if (rval->o_type == TEMP)
        free(rval);
    return (TRUE);
}

int db_xnot()
{
    struct operand *val,*result;

    val = *--sptr;
#ifdef DEBUG
    printf("*** db_xnot(): vval = %08X\n", val);
#endif
    if ((result = malloc(sizeof(struct operand))) == NULL)
        return (db_ferror(INSMEM));
    result->o_type = TEMP;
    result->o_value.ov_boolean = !val->o_value.ov_boolean;
    *sptr++ = result;
    if (val->o_type == TEMP)
        free(val);
    return (TRUE);
}

int db_xlss()
{
    return (compare(LSS));
}

int db_xleq()
{
    return (compare(LEQ));
}

int db_xeql()
{
    return (compare(EQL));
}

int db_xgeq()
{
    return (compare(GEQ));
}

int db_xgtr()
{
    return (compare(GTR));
}

int db_xneq()
{
    return (compare(NEQ));
}

LOCAL int compare(cmp)
{
    struct operand *lval,*rval,*result;
    int i;

    rval = *--sptr; lval = *--sptr;
#ifdef DEBUG
    printf("*** compare(%d): lval = %08X, rval = %08X\n",
	   cmp, lval, rval);
    printf("***            : lval->o_value.ov_char.ovc_type = %d\n",
	   lval->o_value.ov_char.ovc_type);
#endif
    if ((result = malloc(sizeof(struct operand))) == NULL)
        return (db_ferror(INSMEM));
    result->o_type = TEMP;

    if (lval->o_value.ov_char.ovc_type == TCHAR)
        i = comp(lval,rval);
    else
        i = ncomp(lval,rval);

    switch (cmp) {
    case LSS:   i = (i < 0);
                break;
    case LEQ:   i = (i <= 0);
                break;
    case EQL:   i = (i == 0);
                break;
    case GEQ:   i = (i >= 0);
                break;
    case GTR:   i = (i > 0);
                break;
    case NEQ:   i = (i != 0);
                break;
    }
    result->o_value.ov_boolean = i;
    *sptr++ = result;
    if (lval->o_type == TEMP)
        free(lval);
    if (rval->o_type == TEMP)
        free(rval);
    return (TRUE);
}

LOCAL int comp(lval,rval)
  struct operand *lval,*rval;
{
    register char *lptr,*rptr; register int lctr,rctr;
    register int len;

    lptr = lval->o_value.ov_char.ovc_string;
    lctr = lval->o_value.ov_char.ovc_length;
    rptr = rval->o_value.ov_char.ovc_string;
    rctr = rval->o_value.ov_char.ovc_length;

#ifdef DEBUG
    printf("*** comp(): lptr = %s, lctr = %d, rptr = %s, rctr = %d\n",
	   lptr, lctr, rptr, rctr);
#endif
    while (lctr > 0 && (lptr[lctr-1] == 0 || lptr[lctr-1] == ' '))
        lctr--;
    while (rctr > 0 && (rptr[rctr-1] == 0 || rptr[rctr-1] == ' '))
        rctr--;

    if (lctr < rctr)
        len = lctr;
    else
        len = rctr;

    while ((len--) > 0) {
        if (*lptr++ != *rptr++)
            if (*--lptr < *--rptr)
                return (-1);
            else
                return (1);
    }

    if (lctr == rctr)
        return (0);
    else if (lctr < rctr)
        return (-1);
    else
        return (1);
}

LOCAL int ncomp(lval,rval)
  struct operand *lval,*rval;
{
    char lstr[NUMBERMAX+1],rstr[NUMBERMAX+1];
    int len;

    strncpy(lstr,lval->o_value.ov_char.ovc_string,
          (len = lval->o_value.ov_char.ovc_length)); lstr[len] = EOS;
    strncpy(rstr,rval->o_value.ov_char.ovc_string,
          (len = rval->o_value.ov_char.ovc_length)); rstr[len] = EOS;

#ifdef DEBUG
    printf("*** ncomp(): lstr = %s, rstr = %s\n", lstr, rstr);
#endif
    return (db_cmp(lstr,rstr));
}
