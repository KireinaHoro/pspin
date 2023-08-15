#!/usr/bin/env bash

set -eux

pspin="10.0.0.1"
bypass="10.0.0.2"
pspin_utils="$(realpath ../../../../utils)"
trials_par="$(seq 1 16)"
count_par=20
trials_count="1 $(seq 2 2 20)"
par_count=16
data_root="data"
datatype_str="hvec(2 1 18432)[hvec(2 1 12288)[hvec(2 1 6144)[vec(32 6 8)[ctg(18)[float]]]]]"
datatype_bin="ddt.bin"

# 60% peak GEMM validation, allow up to 5 misses, require 5 hits
# start with dim=1100, poll 20 times per iteration
tune_opts="-g 0.6 -m 5 -h 5 -b 1100 -i 20"

fatal() {
    echo "$@" >&2
    exit 1
}

do_parallel=0
do_msg_size=0
while getopts "pm" OPTION; do
    case $OPTION in
    p)
        do_parallel=1
        ;;
    m)
        do_msg_size=1
        ;;
    *)
        fatal "Incorrect options provided"
        ;;
    esac
done

# environment
source ../../sourceme.sh

# terminate the background jobs on exit
# https://stackoverflow.com/a/2173421/5520728
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

do_netns() {
    netns=$1
    shift
    sudo LD_LIBRARY_PATH=typebuilder/ ip netns exec $netns "$@"
}
export -f do_netns

run_with_retry() {
    retries=5
    for (( i=retries; i>=1; --i )); do
        do_netns pspin host/datatypes "$@"
        if [[ $? != 1 ]]; then
            break
        fi
        echo Retrying...
    done
}

# build handlers, host app and sender
make
make host sender

# compile typebuilder
pushd typebuilder
./compile.sh
popd

# check if netns is correctly setup
if ! do_netns pspin ip a | grep $pspin; then
    fatal "PsPIN NIC not found in netns.  Please rerun setup"
fi

# start stdout capture
nohup sudo $pspin_utils/cat_stdout.py --dump-files --clean &>/dev/null &

# start sender
nohup bash -c "do_netns bypass sender/datatypes_sender \"$datatype_str\"" &> sender.out &

if [[ $do_parallel == 1 ]]; then
    # compile datatype for parallelism
    typebuilder/typebuilder "$datatype_str" $count_par "$count_par.$datatype_bin"

    # run trials with varying parallelism
    for pm in $trials_par; do
        run_with_retry "$count_par.$datatype_bin" \
            -o $data_root/p-$pm.csv \
            -q $bypass \
            -p $pm $tune_opts
    done
fi

if [[ $do_msg_size == 1 ]]; then
    # run trials with varying message size
    for ms in $trials_count; do
        # compile datatype
        typebuilder/typebuilder "$datatype_str" $ms "$ms.$datatype_bin"

        run_with_retry "$ms.$datatype_bin" \
            -o $data_root/m-$ms.csv \
            -q $bypass \
            -p $par_count $tune_opts
    done
fi
