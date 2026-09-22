#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

#
# rtcmutate.sh -- the mutation gate for the host clock tests.
#
# A check that cannot fail cannot be merged.  This rebuilds tests/rtctest.c
# against deliberately broken copies of src/bios/rtc900.c and src/cmd/date.c -- one
# defect at a time, each one a plausible mistake rather than a random edit
# -- and requires every one of them to be caught.  It prints which check
# caught each defect, and at the end the set of checks that were ever seen
# to fail: anything a reviewer expects in that list and does not find is a
# check proving nothing.
#
# Run from the repository root as `make verify-rtc-mutants'.
#
# A mutation that does not change the file is itself a failure: it means
# the code moved and the defect is no longer being injected.
#
# Mutates driver sources AND rtcchip.c: leap-year inference must not fail
# when the model is broken.

CC=${1:-cc}
work=build/rtcmut
rm -rf $work
mkdir -p $work/src

fail=0
seen=$work/seen

build() {
	$CC -std=gnu89 -w -Itests/rtcinc -c $work/src/rtc900.c \
		-o $work/rtc900.o 2>$work/cc.log &&
	$CC -std=gnu89 -w -Isrc/lib -Dstatic= -Dmain=date_main \
		-c $work/src/date.c -o $work/date.o 2>>$work/cc.log &&
	$CC -std=gnu89 -w -Itests -c $work/src/rtcchip.c -o $work/rtcchip.o \
		2>>$work/cc.log &&
	$CC -std=gnu89 -w -Itests -c tests/rtctest.c -o $work/rtctest.o \
		2>>$work/cc.log &&
	$CC -o $work/rtctest $work/rtctest.o $work/rtcchip.o \
		$work/rtc900.o $work/date.o 2>>$work/cc.log
}

# mutate NAME FILE SED-SCRIPT DESCRIPTION
mutate() {
	name=$1; file=$2; script=$3; desc=$4

	cp src/bios/rtc900.c $work/src/rtc900.c
	cp src/cmd/date.c $work/src/date.c
	cp tests/rtcchip.c $work/src/rtcchip.c
	sed -e "$script" $work/src/$file > $work/tmp.c
	if cmp -s $work/tmp.c $work/src/$file; then
		echo "MUTANT $name: the defect did not apply -- $desc"
		fail=`expr $fail + 1`
		return
	fi
	mv $work/tmp.c $work/src/$file

	if ! build; then
		echo "MUTANT $name: would not compile"
		sed -n 1,3p $work/cc.log
		fail=`expr $fail + 1`
		return
	fi
	if $work/rtctest > $work/run.log 2>&1; then
		echo "SURVIVED $name: $desc"
		fail=`expr $fail + 1`
		return
	fi
	sed -n 's/^FAIL \[\([a-z0-9-]*\)\].*/\1/p' $work/run.log | sort -u \
		>> $seen
	echo "caught  $name -- `sed -n 's/^FAIL \[\([a-z0-9-]*\)\].*/\1/p' \
		$work/run.log | sort -u | tr '\n' ' '`"
}

: > $seen

# ---- the two findings this gate exists for ----
mutate leap-surplus rtc900.c \
	's/(((4 - (y & 3)) & 3) << 2)/((y \& 3) << 2)/' \
	"D10 leap-year selection written as the raw surplus, not the datasheet countdown"
mutate datelo-mask rtc900.c \
	's/(UWORD)(tod\[TOD_DATELO\] & 0xff)/(UWORD)tod[TOD_DATELO]/' \
	"date word low byte not masked: the signed-char trap"
# The matching mask on the HIGH byte has no mutant, and cannot have one:
# the byte is shifted left by eight into a 16-bit UWORD, which discards
# every bit a sign extension could have set.  It is kept for symmetry with
# the low byte, where the mask is load-bearing, and removing it is an
# equivalent mutation rather than a defect.

# ---- the year window ----
mutate window-off-by-one rtc900.c \
	's/y >= WRAPYEAR/y > WRAPYEAR/' \
	"read window shifted: yy = 78 becomes 2078"
mutate date-window date.c \
	's/(y >= 78)/(y >= 79)/' \
	"DATE's own window shifted: 78 parses as 2078"
mutate date-hi-mask date.c \
	's/(tod\[TOD_DATEHI\] & 0xff)/tod[TOD_DATEHI]/' \
	"DATE's display drops the date-word mask"

# ---- the read-while-ticking race ----
mutate no-retry rtc900.c \
	's/try < 4/try < 1/' \
	"one try instead of four: a straddled read is served"
mutate no-compare rtc900.c \
	's/if (regs\[i\] != alt\[i\])/if (0)/' \
	"the two images are never compared"

# ---- reading ----
mutate no-12h rtc900.c \
	's/if ((buf\[R_H10\] & H10_24H) == 0)/if (0)/' \
	"12-hour images decoded as if they were 24-hour"
mutate no-pm rtc900.c \
	's/h += 12;/h += 0;/' \
	"the PM flag ignored"
mutate day-range rtc900.c \
	's/d > mlen(m, y)/d > 31/' \
	"any day 1..31 accepted in any month"
mutate date-day-range date.c \
	's/d > mlen(m, y)/d > 31/' \
	"DATE accepts 30 February"
mutate time-range rtc900.c \
	's/if (h > 23 || mi > 59 || s > 59)/if (0)/' \
	"out-of-range hour, minute and second accepted on a set"

# ---- setting ----
mutate no-readback rtc900.c \
	's/if (alt\[i\] != regs\[i\])/if (0)/' \
	"the set is never verified against the chip"
mutate no-reset rtc900.c \
	's/rtcwr(R_RESET, 0, PB_STOP);/;/' \
	"the post-stage reset register is never written"
mutate no-stop rtc900.c \
	's/, PB_STOP)/, 0)/g' \
	"STOP is not held across the reload"
mutate wday rtc900.c \
	's/(int)((day - 1) % 7)/(int)(day % 7)/' \
	"the day of week is off by one"

# ---- the pin protocol ----
mutate no-cs rtc900.c \
	's/& ~PC_CS/| PC_CS/' \
	"/CS never asserted"
mutate pc-ddr rtc900.c \
	's/define PC_DDR.*0x05/define PC_DDR 0x07/' \
	"PCDD left as the ROM leaves it, so PC1 cannot drive /CS"
mutate pc3 rtc900.c \
	's/inb(P_PCDATA) & 0x0f/inb(P_PCDATA) \& 0x07/g' \
	"the keyboard's PC3 acknowledge level is dropped"
mutate no-input rtc900.c \
	's/inb(P_PBDD) | PB_D)/inb(P_PBDD) \& ~PB_D)/' \
	"D0..D3 never turned round for the READ pulse"
mutate no-adwr rtc900.c \
	's/pbset(hold | (reg & PB_D) | PB_ADWR);/;/' \
	"the ADDRESS WRITE strobe never pulses"

# ---- the New Year, and the leap-year selection across it ----
# These four break the MODEL, not the driver.  They are here because the
# t_newyear checks are the only ones in the suite that assert what the
# register file does at a year carry, and the datasheet never states
# whether the chip advances the leap-year selection field itself -- only
# that it is "automatically adjustable" -- so this behaviour is inferred,
# not documented, and the checks standing in for that inference have to
# be shown to bite.
mutate leap-no-advance rtcchip.c \
	's/leap = (leap + 3) \& 3;/;/' \
	"the leap-year selection is not advanced at a year carry"
mutate leap-counts-up rtcchip.c \
	's/leap = (leap + 3) \& 3;/leap = (leap + 1) \& 3;/' \
	"the leap-year selection counts up instead of down"
mutate no-year-carry rtcchip.c \
	's/y = (y + 1) % 100;/;/' \
	"December 31st rolls the month but not the year"
mutate no-feb-29 rtcchip.c \
	's/return (mo == 2 \&\& leap) ? 29 : d\[mo\];/return d[mo];/' \
	"February is 28 days long in a leap year too"
mutate leap-from-year rtcchip.c \
	's/leap = (r\[8\] >> 2) \& 3;/leap = 0;/' \
	"the 29th of February is allowed regardless of the selection field"

# ---- and one on the driver: the date must not depend on the field ----
mutate isleap rtc900.c \
	's/return ((y \& 3) == 0);/return ((y \& 3) == 1);/' \
	"the driver's own leap rule shifted by a year"

echo
if [ -s $seen ]; then
	echo "checks observed failing: `sort -u $seen | tr '\n' ' '`"
fi
if [ $fail -ne 0 ]; then
	echo "verify-rtc-mutants: FAIL -- $fail defect(s) not caught"
	exit 1
fi
echo "verify-rtc-mutants: PASS -- every injected defect was caught"
exit 0
