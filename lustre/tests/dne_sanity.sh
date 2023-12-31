#!/bin/bash
#
# run tests on different directories at the same time
#

set -e

SRCDIR=$(dirname $0)

LUSTRE=${LUSTRE:-$(dirname $0)/..}
. $LUSTRE/tests/test-framework.sh
init_test_env "$@"
init_logging

ALWAYS_EXCEPT="$DNE_SANITY_EXCEPT "
build_test_filter

PARALLEL_RUNS=${PARALLEL_RUNS:-2}
FAIL_ON_ERROR=false

check_and_setup_lustre

DIR=${DIR:-$MOUNT}

ORIGIN_DIR=$DIR

prepare_running_directories()
{
	local mdtidx
	local rc=0
	local i

	for i in $(seq $PARALLEL_RUNS); do
		rm -rf $ORIGIN_DIR/dir$i
		if [ $MDSCOUNT -gt 1 ]; then
			mdtidx=$((i % MDSCOUNT))
			$LFS mkdir -i $mdtidx $ORIGIN_DIR/dir$i || rc=$?
		else
			mkdir -p $ORIGIN_DIR/dir$i
		fi

		if [ $rc != 0 ]; then
			echo "can not create dir$i"
			break
		fi
	done

	return $rc
}

prepare_running_directories || error "Can not create running directories"

cleanup_running_directories()
{
	local i
	local rc=0

	for i in $(seq $PARALLEL_RUNS); do
		rm -rf $ORIGIN_DIR/dir$i || rc=$?
	done

	return $rc
}

test_sanity()
{
	[ ! -f sanity.sh ] && skip_env "No sanity.sh skipping"

	local index
	local pid
	local rpids
	local rc=0
	local rrc=0

	for index in $(seq $PARALLEL_RUNS); do
		DIR=$ORIGIN_DIR/dir${index} PARALLEL=yes \
		EXT2_DEV="$TMP/SANITY.LOOP_${index}" \
		LOGDIR="${LOGDIR}_${index}" YAML_LOG="" bash sanity.sh &
		pid=$!
		echo start sanity: $pid
		rpids="$rpids $pid"
	done

	echo sanity pids: $rpids
	for pid in $rpids; do
		wait $pid
		rc=$?
		echo "pid=$pid rc=$rc"
		if [ $rc != 0 ]; then
			rrc=$((rrc + 1))
		fi
	done
	return $rrc
}

run_test sanity "Run sanity parallel on different directories at the same time"

cleanup_running_directories || error "unlink running directories"

complete_test $SECONDS
check_and_cleanup_lustre
exit_status
