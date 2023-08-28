#!/usr/bin/env bash

set -eux

mpiexec="mpiexec"
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
# start with dim=1500, poll 20 times per iteration
tune_opts="-g 0.6 -m 5 -h 5 -b 1500 -i 20"

fatal() {
    echo "$@" >&2
    exit 1
}

do_parallel=0
do_msg_size=0
do_baseline=0
vanilla_corundum=0
while getopts "bpmv" OPTION; do
    case $OPTION in
    b)
        do_baseline=1
        ;;
    p)
        do_parallel=1
        ;;
    m)
        do_msg_size=1
        ;;
    v)
        vanilla_corundum=1
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

do_netns="sudo LD_LIBRARY_PATH=typebuilder/ ip netns exec"

run_with_retry() {
    set +e
    retries=5
    for (( i=retries; i>=1; --i )); do
        $do_netns pspin host/datatypes "$@"
        if [[ $? != 1 ]]; then
            break
        fi
        echo Retrying...
    done
    set -e
}

run_baseline() {
    echo "Running baseline with args $@ ..."
    ctrl_port=$(awk -F'[: ]' '{print $6; exit;}' < <($do_netns pspin mpiexec -np 2 -launcher manual -host $pspin,$bypass \
        -outfile-pattern baseline.%r.out -errfile-pattern baseline.%r.err baseline/datatypes_baseline "$@" |& tee mpiexec.out))

    nohup $do_netns pspin /usr/bin/hydra_pmi_proxy --control-port $pspin:$ctrl_port --rmk user \
        --launcher manual --demux poll --pgid 0 --retries 10 --usize -2 --proxy-id 0 &> hydra.pspin.out &

    nohup $do_netns bypass /usr/bin/hydra_pmi_proxy --control-port $pspin:$ctrl_port --rmk user \
        --launcher manual --demux poll --pgid 0 --retries 10 --usize -2 --proxy-id 1 &> hydra.bypass.out &

    wait $(jobs -rp)
}

# build all
make
make host sender baseline

# compile typebuilder
pushd typebuilder
./compile.sh
popd

# check if netns is correctly setup
if ! $do_netns pspin ip a | grep $pspin; then
    fatal "PsPIN NIC not found in netns.  Please rerun setup"
fi

if [[ $do_baseline == 0 ]]; then
    # start stdout capture
    nohup sudo $pspin_utils/cat_stdout.py --dump-files --clean &>/dev/null &

    # start sender
    nohup $do_netns bypass sender/datatypes_sender "$datatype_str" &> sender.out &
fi

if [[ $vanilla_corundum == 0 ]]; then
    key=b
else
    key=v
fi

if [[ $do_parallel == 1 ]]; then
    # compile datatype for parallelism
    typebuilder/typebuilder "$datatype_str" $count_par "$count_par.$datatype_bin"

    # run trials with varying parallelism
    for pm in $trials_par; do
        if [[ $do_baseline == 0 ]]; then
            run_with_retry "$count_par.$datatype_bin" \
                -o $data_root/p-$pm.csv \
                -q $bypass \
                -p $pm $tune_opts
        else
            run_baseline "$datatype_str" \
                -o $data_root/${key}p-$pm.csv \
                -p $pm -e $count_par
        fi
    done
fi

if [[ $do_msg_size == 1 ]]; then
    # run trials with varying message size
    for ms in $trials_count; do
        if [[ $do_baseline == 0 ]]; then
            # compile datatype
            typebuilder/typebuilder "$datatype_str" $ms "$ms.$datatype_bin"

            run_with_retry "$ms.$datatype_bin" \
                -o $data_root/m-$ms.csv \
                -q $bypass \
                -p $par_count $tune_opts
        else
            run_baseline "$datatype_str" \
                -o $data_root/${key}m-$ms.csv \
                -p $par_count -e $ms
        fi
    done
fi

echo Trial finished!
