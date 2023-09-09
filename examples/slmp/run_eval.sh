#!/usr/bin/env bash

set -eu

launches=20
max_tries=$((launches * 10))
pspin="10.0.0.1"
bypass="10.0.0.2"
data_root="data"
start_sz=100
window_sizes=(1 4 16 64 256 512 1024)
thread_counts=(1 2 4 8 16 32 64)
largest_sz=$((256 * 1024 * 1024))

FPSPIN_PCIE_PATH="1d:00.0"
FPSPIN_UTILS="$(realpath ../../../../utils)"
FPSPIN_PAYLOAD="slmp"

source $FPSPIN_UTILS/eval_lib.sh

continue_sz=0
continue_wnd=0
continue_thr=0

while getopts "s:w:t:" OPTION; do
    case $OPTION in
        s) continue_sz=$OPTARG ;;
        w) continue_wnd=$OPTARG ;;
        t) continue_thr=$OPTARG ;;
        *) fatal "Incorrect options provided" ;;
    esac
done

mkdir -p $data_root

# environment
source ../../sourceme.sh

# build all
make host sender
make

# terminate the background jobs on exit
# https://stackoverflow.com/a/2173421/5520728
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

launch_receiver_clean() {
    need_launches=$1

    kill_payload

    # XXX: ideally we could use a tighter -m, but somehow this triggers IOMMU pagefaults
    #      we just use 512 MB for now
    sudo host/slmp -o $out_prefix.csv -m $((512 * 1024 * 1024)) -e $need_launches &> $out_prefix.log &
    sleep 0.2
}

# prepare large file
src_file=slmp-file-random.dat
dd if=/dev/urandom of=$src_file bs=$largest_sz count=1

reset_device

capture_stdout

for wnd_sz in ${window_sizes[@]}; do
    for threads in ${thread_counts[@]}; do
        if (( wnd_sz < threads )); then
            # skip since each thread will have at least one window
            continue
        fi
        for (( sz = start_sz; sz <= largest_sz; sz *= 2 )); do
            if (( wnd_sz > continue_wnd )); then
                skip=0
            elif (( wnd_sz < continue_wnd )); then
                skip=1
            else
                if (( threads > continue_thr )); then
                    skip=0
                elif (( threads < continue_thr )); then
                    skip=1
                else
                    if (( sz < continue_sz )); then
                        skip=1
                    else
                        skip=0
                    fi
                fi
            fi
            if (( skip == 1 )); then
                continue
            fi

            if (( wnd_sz * 1408 > sz )); then
                # skip since we'll never saturate the window
                continue
            fi
            out_prefix=$data_root/$sz-$wnd_sz-$threads

            echo "Size $sz; window size $wnd_sz; threads $threads"

            rm -f $out_prefix-sender.txt

            launch_receiver_clean $launches

            good_runs=0
            for (( lid = 0; lid < max_tries; lid++ )); do
                if $do_netns bypass sender/slmp_sender -f $src_file -s $pspin -l $sz -w $wnd_sz -t $threads &>> $out_prefix-sender.txt; then
                    echo -n .
                    if (( ++good_runs >= launches )); then
                        echo Achieved required $launches runs
                        break
                    fi
                else
                    retval=$?
                    echo "Timed out!"
                    # Rationale: always start clean
                    # for SYN failures: reset the device first
                    if (( retval < 255 )); then
                        reset_device
                    fi

                    launch_receiver_clean $((launches - good_runs))
                fi
            done
        done
    done
done

rm $src_file
kill_stdout

echo Done!
