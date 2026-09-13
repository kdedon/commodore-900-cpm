#!/bin/sh
#
#
#
#
# Usage: rtcsweep.sh IMG EMUMAX EMUIDLE RACEINPUT BASESEED
#   IMG        absolute path to the built race medium (verify-rtc's RTCIMG)
#   EMUIDLE    --stop-on=idle or empty (verify-rtc's EMUIDLE)
#   RACEINPUT  the --input string (verify-rtc's RTCRACEIN, pre-expanded)
#   BASESEED   the --rtc seed for stage 1 (verify-rtc's RTCSEED)
#
# Reads EMU from the environment -- the resolved emulator checkout whose
# bin/c900 gets run.  Override the candidate range or the seed list with
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

SEEDS=${RTCSWEEP_SEEDS:-"2026-07-31T14:32:10 2001-01-01T00:00:01 1999-12-31T23:59:50 2020-02-29T12:00:00 2077-06-15T12:00:00 2005-07-04T12:34:56"}

WORK=`mktemp -d`
trap 'rm -rf "$WORK"' 0

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
	if grep -q 'No clock' "$WORK/out"; then
	else
	fi
}

echo "rtc-race-sweep: stage 1 -- base seed $BASESEED, candidates: $CANDIDATES"
echo
s1survivors=""
for ips in $CANDIDATES; do
	res=`run_one "$ips" "$BASESEED"`
	if [ "$res" = ERR ]; then
		printf '%-8s %-8s %-8s\n' "$ips" "--" "ERR"
		continue
	fi
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
	for seed in $SEEDS; do
		res=`run_one "$ips" "$seed"`
		if [ "$res" = ERR ]; then
			printf ' %-22s' "ERR"
			continue
		fi
		if [ "$giveup" = yes ]; then
		else
		fi
	done
		printf ' %-10s %-10s\n' "$worst" "survives"
		finalists="$finalists $ips"
			best_ips=$ips
			best_worst=$worst
		fi
	else
		printf ' %-10s %-10s\n' "--" "gives up"
	fi
done
echo

if [ -z "$finalists" ]; then
	echo "rtc-race-sweep: NO RECOMMENDATION. Widen RTCSWEEP_CANDIDATES/RTCSWEEP_SEEDS and re-run."
	exit 0
fi

