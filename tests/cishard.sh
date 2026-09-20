#!/bin/sh
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT
#
# cishard.sh -- split the verify suite into N balanced shards, so CI can run
# them on separate runners.
#
#   sh tests/cishard.sh N I         print shard I of N, one target per line
#   sh tests/cishard.sh --check N   check the N shards cover the suite exactly
#
# Balance comes from tests/verify-times, the seconds a real run took.  That
# file only steers the packing: a target missing from it is assumed short, so
# it may lag behind the makefile without anything breaking.
#
# A target that is another's prerequisite runs inside the same make as it
# (verify-boot with verify-bootgate), so the two travel as one unit and land
# in the same shard.  A unit costs its longest member, not the sum: one make
# runs the lot and the recorded time of each member is that make's.

set -u

TIMES=tests/verify-times

# What a target with no recorded time is assumed to cost.  Short, so a new
# target perturbs the packing as little as possible.
DEFAULT=10

# The suite's own targets, read from the makefile, as verify-all reads them.
suite() {
	sed -n 's/^\(verify-[a-z0-9-]*\):.*/\1/p' tests/verify.mk |
		grep -Ev '^verify-(all|zcc)$' | sort -u
}

# The verify-* prerequisites of one target, if any.
vdeps() {
	sed -n "s/^$1:[ 	]*\(.*\)/\1/p" tests/verify.mk |
		tr ' \011' '\012\012' | grep '^verify-' | sort -u
}

# One line per unit: the targets that must stay together.
units() {
	merged=''
	for t in `suite`; do
		d=`vdeps $t`
		[ -n "$d" ] || continue
		echo `echo $d` $t
		merged="$merged `echo $d` $t"
	done
	for t in `suite`; do
		case " $merged " in *" $t "*) continue;; esac
		echo $t
	done
}

# Longest-first greedy: each unit goes to the lightest shard so far.  Sorting
# by cost and then by name makes the assignment the same every run.
pack() {
	units | awk -v times="$TIMES" -v dflt="$DEFAULT" '
		BEGIN {
			while ((getline l < times) > 0) {
				n = split(l, a, " ")
				if (n >= 2) cost[a[2]] = a[1] + 0
			}
		}
		{
			c = 0
			for (i = 1; i <= NF; i++) {
				v = ($i in cost) ? cost[$i] : dflt
				if (v > c) c = v
			}
			print c, $0
		}' |
	LC_ALL=C sort -k1,1nr -k2,2 |
	awk -v n="$1" '
		{
			b = 1
			for (i = 2; i <= n; i++) if (load[i] < load[b]) b = i
			load[b] += $1
			for (i = 2; i <= NF; i++) shard[$i] = b
		}
		END {
			for (t in shard) print shard[t], t
			for (i = 1; i <= n; i++) printf "load %d %d\n", i, load[i]
		}'
}

usage() {
	echo "usage: cishard.sh N I | cishard.sh --check N" >&2
	exit 2
}

case ${1:-} in
--check)
	n=${2:-} ; [ -n "$n" ] || usage
	out=`pack "$n"`
	echo "$out" | sed -n 's/^load //p' | while read -r i s; do
		echo "shard $i/$n: ${s}s"
	done
	got=`echo "$out" | grep -v '^load ' | awk '{print $2}' | LC_ALL=C sort`
	want=`suite | LC_ALL=C sort`
	if [ "$got" != "$want" ] || [ -n "`echo \"$got\" | uniq -d`" ]; then
		echo "cishard: the shards do not match the suite:" >&2
		{ echo "$got"; echo "$want"; } | LC_ALL=C sort | uniq -u >&2
		exit 1
	fi
	echo "cishard: $n shards, `echo "$want" | wc -l` targets, each once"
	;;
*)
	n=${1:-} ; i=${2:-}
	[ -n "$n" ] && [ -n "$i" ] || usage
	# An out-of-range shard would print nothing, and an empty VERIFYSET
	# means the whole suite.
	[ "$i" -ge 1 ] 2>/dev/null && [ "$i" -le "$n" ] 2>/dev/null || usage
	pack "$n" | awk -v i="$i" '$1 == i { print $2 }' |
		LC_ALL=C sort
	;;
esac
