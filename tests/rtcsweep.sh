#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT

# rtcsweep.sh -- measure the band verify-rtc's race leg lives in.
#
# WHAT THIS USED TO BE.  RTCRACEIPS was once an AIM: the race leg ran three
# DATEs on one boot and the constant had to be chosen so that one of those
# three reads happened to be underway when a second carried.  That aim was
# (boot instructions mod RTCRACEIPS), so every change to resident text or to
# what is staged on A: re-aimed it, and this script existed to re-choose the
# constant -- which the file's history did six times.
#
# WHAT IT IS NOW.  The race leg runs DATE C, the continuous form, for a fixed
# instruction budget: thousands of reads that sweep the whole second, so a
# carry inside a register read is unavoidable rather than aimed at, and the
# constant is only a RATE.  Nothing here has to be re-run when unrelated code
# changes size.  What this script still does, and is still the right tool for,
# is SHOW THE BAND: where the give-up floor is, how much margin each rate has,
# and that the leg's setting sits in the middle of a wide flat region rather
# than on a spike.  Run it when rtc900.c's read loop itself changes length, or
# when you want the evidence behind the table in tests/verify.mk.
#
# The measure is EXCESS = reads - 26 x (times printed).  A clean, unretried
# read of the thirteen time registers is two passes = 26 register reads and
# prints one line, so excess counts the register reads spent on RETRIES:
# 26 = one retried read, 0 = the carry never landed inside a read.  (A read
# cut short by the instruction budget can leave up to 25 reads unpaired with
# a line, which is why one full retry, not one read, is the threshold.)
#
# Stage 1: walk a range of --rtc-ips candidates, one session each, on the BASE
# seed.  A candidate SURVIVES when the session did not give up ("No clock"
# printed, i.e. four disagreeing pairs in a row) AND excess >= 26.
# Stage 2: re-run every survivor against six seeds (a normal date, a century
# year, a non-leap Dec 31, a leap Feb 29, the top of the 1978-2077 year window
# and a mid-year date).  A candidate that never gives up and always retries is
# a FINAL survivor; the recommendation is the one with the largest WORST-CASE
# excess, i.e. the most margin over the one retry the leg needs.
#
# Usage: rtcsweep.sh IMG EMUMAX EMUIDLE RACEINPUT BASESEED
#   IMG        absolute path to the built race medium (verify-rtc's RTCIMG)
#   EMUMAX     --max budget (verify-rtc's RTCRACEMAX)
#   EMUIDLE    --stop-on=idle or empty (verify-rtc's EMUIDLE)
#   RACEINPUT  the --input string (verify-rtc's RTCRACEIN, pre-expanded)
#   BASESEED   the --rtc seed for stage 1 (verify-rtc's RTCSEED)
#
# Reads EMU from the environment -- the resolved emulator checkout whose
# bin/c900 gets run.  Override the candidate range or the seed list with
# RTCSWEEP_CANDIDATES / RTCSWEEP_SEEDS (space-separated).
#
# This is a MEASUREMENT tool, not a pass/fail gate. It exits 0 whenever it
# reaches a verdict -- including "no candidate survived", which is a real
# outcome and is never dressed up as a recommendation. It exits 2 only for
# a usage or setup error (bad args, no emulator).

set -u

IMG=${1:-}; EMUMAX=${2:-}; EMUIDLE=${3:-}; RACEIN=${4:-}; BASESEED=${5:-}
[ -n "$IMG" ] && [ -n "$BASESEED" ] || {
	echo "usage: rtcsweep.sh IMG EMUMAX EMUIDLE RACEINPUT BASESEED" >&2
	exit 2
}
[ -n "${EMU:-}" ] && [ -x "$EMU/bin/c900" ] || {
	echo "rtc-race-sweep: FAIL -- no emulator resolved (EMU='${EMU:-}')" >&2
	exit 2
}

CANDIDATES=${RTCSWEEP_CANDIDATES:-"4000 5000 5500 6000 8000 10000 15000 20000 30000 50000 100000 200000 500000"}
SEEDS=${RTCSWEEP_SEEDS:-"2026-07-31T14:32:10 2001-01-01T00:00:01 1999-12-31T23:59:50 2020-02-29T12:00:00 2077-06-15T12:00:00 2005-07-04T12:34:56"}

WORK=`mktemp -d`
trap 'rm -rf "$WORK"' 0

# run_one IPS SEED -- prints "EXCESS GIVEUP" (GIVEUP = yes|no) on stdout, or
# "ERR" if the emulator itself did not exit cleanly (a setup problem, not a
# measurement -- treated as a non-survivor either way).
run_one() {
	_ips=$1; _seed=$2
	( cd "$EMU/bin" && ./c900 --disk="$IMG" --rtc="$_seed" --rtc-ips="$_ips" \
		--input="$RACEIN" --max="$EMUMAX" $EMUIDLE \
		>"$WORK/out" 2>"$WORK/err" )
	_st=$?
	if [ $_st -ne 0 ]; then echo "ERR"; return; fi
	_reads=`sed -n 's/.*, \([0-9]*\) reads,.*/\1/p' "$WORK/err" | tail -1`
	[ -n "$_reads" ] || _reads=0
	_lines=`tr -d '\r' < "$WORK/out" \
		| grep -cE '^(Sun|Mon|Tue|Wed|Thu|Fri|Sat) [0-9][0-9]/'`
	_excess=`expr "$_reads" - 26 \* "$_lines"`
	if grep -q 'No clock' "$WORK/out"; then
		echo "$_excess yes"
	else
		echo "$_excess no"
	fi
}

echo "rtc-race-sweep: stage 1 -- base seed $BASESEED, candidates: $CANDIDATES"
echo "rtc-race-sweep: excess = register reads spent on retries; 26 = one retried read"
echo
printf '%-8s %-8s %-8s\n' ips excess giveup
s1survivors=""
for ips in $CANDIDATES; do
	res=`run_one "$ips" "$BASESEED"`
	if [ "$res" = ERR ]; then
		printf '%-8s %-8s %-8s\n' "$ips" "--" "ERR"
		continue
	fi
	excess=${res% *}; giveup=${res#* }
	printf '%-8s %-8s %-8s\n' "$ips" "$excess" "$giveup"
	if [ "$giveup" = no ] && [ "$excess" -ge 26 ]; then
		s1survivors="$s1survivors $ips"
	fi
done
echo
if [ -z "$s1survivors" ]; then
	echo "rtc-race-sweep: NO CANDIDATE SURVIVED STAGE 1 -- every value either gave up or never retried."
	echo "rtc-race-sweep: NO RECOMMENDATION. Widen RTCSWEEP_CANDIDATES and re-run."
	exit 0
fi
echo "rtc-race-sweep: stage 1 survivors:$s1survivors"
echo

echo "rtc-race-sweep: stage 2 -- survivors x six seeds: $SEEDS"
echo
printf '%-8s' ips
for seed in $SEEDS; do printf ' %-22s' "$seed"; done
printf ' %-10s %-10s\n' worst final
finalists=""
best_ips=""
best_worst=""
for ips in $s1survivors; do
	printf '%-8s' "$ips"
	worst=""
	bad=no
	for seed in $SEEDS; do
		res=`run_one "$ips" "$seed"`
		if [ "$res" = ERR ]; then
			printf ' %-22s' "ERR"
			bad=yes
			continue
		fi
		excess=${res% *}; giveup=${res#* }
		if [ "$giveup" = yes ]; then
			printf ' %-22s' "${excess}!"
			bad=yes
		else
			printf ' %-22s' "$excess"
			[ "$excess" -lt 26 ] && bad=yes
			if [ -z "$worst" ] || [ "$excess" -lt "$worst" ]; then
				worst=$excess
			fi
		fi
	done
	if [ "$bad" = no ]; then
		printf ' %-10s %-10s\n' "$worst" "survives"
		finalists="$finalists $ips"
		if [ -z "$best_ips" ] || [ "$worst" -gt "$best_worst" ]; then
			best_ips=$ips
			best_worst=$worst
		fi
	else
		printf ' %-10s %-10s\n' "--" "gives up"
	fi
done
echo

if [ -z "$finalists" ]; then
	echo "rtc-race-sweep: NO CANDIDATE SURVIVED STAGE 2 -- every stage-1 survivor gave up or failed to retry on at least one seed."
	echo "rtc-race-sweep: NO RECOMMENDATION. Widen RTCSWEEP_CANDIDATES/RTCSWEEP_SEEDS and re-run."
	exit 0
fi

echo "rtc-race-sweep: final survivors (always retry, never give up, on every seed):$finalists"
echo "rtc-race-sweep: THE BAND is the result here, not the pick: any setting inside it"
echo "rtc-race-sweep: works, because RTCRACEIPS positions nothing any more."
echo "rtc-race-sweep: RECOMMEND RTCRACEIPS = $best_ips -- largest worst-case excess ($best_worst) across all six seeds, i.e. the most margin over the one retry the leg needs."
