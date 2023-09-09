#!/usr/bin/env bash

set -eu

mpiexec="mpiexec"
pspin="10.0.0.1"
bypass="10.0.0.2"
trials_par="$(seq 1 16)"
count_par=20
trials_count="$(seq 2 2 20)"
par_count=16
data_parent_dir="data"
datatypes=("hvec(2 1 18432)[hvec(2 1 12288)[hvec(2 1 6144)[vec(32 6 8)[ctg(18)[float]]]]]" "ctg(27648)[float]")
datatype_bin="ddt.bin"

# 60% peak GEMM validation, allow up to 5 misses, require 5 hits
# start with dim=1500, poll 20 times per iteration
tune_opts="-g 0.6 -m 5 -h 5 -b 1500 -i 20"

FPSPIN_PCIE_PATH="1d:00.0"
FPSPIN_UTILS="$(realpath ../../../../utils)"
FPSPIN_PAYLOAD="datatypes"

source $FPSPIN_UTILS/eval_lib.sh

do_parallel=0
do_msg_size=0
do_reset=0
do_baseline=0
vanilla_corundum=0

continue_parallel=0
continue_count=0

while getopts "bpmvt:c:l:r" OPTION; do
    case $OPTION in
    b) do_baseline=1 ;;
    p) do_parallel=1 ;;
    m) do_msg_size=1 ;;
    t) datatype_idx=$OPTARG ;;
    v) vanilla_corundum=1 ;;
    c) continue_count=$OPTARG ;;
    l) continue_parallel=$OPTARG ;;
    r) do_reset=1 ;;
    *) fatal "Incorrect options provided" ;;
    esac
done

if [[ -z ${datatype_idx+x} ]]; then
    fatal "Need to specify datatype ID (-t)"
fi

if (( datatype_idx >= ${#datatypes[@]} )); then
    fatal "Datatype ID $datatype_idx not found!"
fi

datatype_str="${datatypes[$datatype_idx]}"
data_root="$data_parent_dir/dt-$datatype_idx/"
mkdir -p $data_root

# environment
source ../../sourceme.sh

# terminate the background jobs on exit
# https://stackoverflow.com/a/2173421/5520728
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

do_netns="sudo LD_LIBRARY_PATH=typebuilder/ ip netns exec"

run_with_retry() {
    set +e
    retries=200
    for (( i=retries; i>=1; --i )); do
        if $do_netns pspin host/datatypes "$@"; then
            echo "Successful!"
            break
        else
            retval=$?
            if (( retval == 128 )); then
                echo "App returned EXIT_FATAL! Stopping..."
                return 128
            elif (( retval == 2 )); then
                echo "App returned EXIT_RETRY, resetting device and retrying..."
                reset_device
            else
                echo "Unknown return value $retval"
                return 128
            fi
        fi
    done
    set -e
}

run_baseline() {
    echo "Running baseline with args $@ ..."
    ctrl_port=$(awk -F'[: ]' '{print $6; exit;}' < <($do_netns pspin mpiexec -np 2 -launcher manual -host $pspin,$bypass \
        -outfile-pattern baseline.%r.out -errfile-pattern baseline.%r.err baseline/datatypes_baseline "$@" |& tee mpiexec.out))

    $do_netns pspin /usr/bin/hydra_pmi_proxy --control-port $pspin:$ctrl_port --rmk user \
        --launcher manual --demux poll --pgid 0 --retries 10 --usize -2 --proxy-id 0 &> hydra.pspin.out &

    $do_netns bypass /usr/bin/hydra_pmi_proxy --control-port $pspin:$ctrl_port --rmk user \
        --launcher manual --demux poll --pgid 0 --retries 10 --usize -2 --proxy-id 1 &> hydra.bypass.out &

    wait $(jobs -rp)
    echo "Successful!"
}

# build all
make
make host sender baseline

# compile typebuilder
pushd typebuilder
./compile.sh
popd

if (( do_reset == 1 )); then
    reset_device
fi

if (( do_baseline == 0 )); then
    capture_stdout

    # start sender
    $do_netns bypass sender/datatypes_sender "$datatype_str" &> sender.out &

    echo "Started sender"
fi

if (( vanilla_corundum == 0 )); then
    key=b
else
    key=v
fi

if (( do_parallel == 1 )); then
    # compile datatype for parallelism
    typebuilder/typebuilder "$datatype_str" $count_par "$count_par.$datatype_bin"

    echo "Compiled datatype: $datatype_str; parallel $count_par"

    # run trials with varying parallelism
    for pm in $trials_par; do
        if (( pm < continue_parallel )); then
            continue
        fi
        echo "Parallel count: $pm"

        if (( do_baseline == 0 )); then
            run_with_retry "$count_par.$datatype_bin" $datatype_idx \
                -o $data_root/p-$pm.csv \
                -q $bypass \
                -p $pm $tune_opts
        else
            run_baseline "$datatype_str" $datatype_idx \
                -o $data_root/${key}p-$pm.csv \
                -p $pm -e $count_par
        fi
    done
fi

if (( do_msg_size == 1 )); then
    # run trials with varying message size
    for ms in $trials_count; do
        if (( ms < continue_count )); then
            continue
        fi
        echo "Message size: $ms"
        if (( do_baseline == 0 )); then
            # compile datatype
            typebuilder/typebuilder "$datatype_str" $ms "$ms.$datatype_bin"

            run_with_retry "$ms.$datatype_bin" $datatype_idx \
                -o $data_root/m-$ms.csv \
                -q $bypass \
                -p $par_count $tune_opts
        else
            run_baseline "$datatype_str" $datatype_idx \
                -o $data_root/${key}m-$ms.csv \
                -p $par_count -e $ms
        fi
    done
fi

echo Trial finished!
