#!/usr/bin/env bash

set -eu

fatal() {
    echo "$@" >&2
    exit 1
}

launches=20
pcie_path="1d:00.0"
max_tries=$((launches * 10))
pspin="10.0.0.1"
bypass="10.0.0.2"
data_root="data"
start_sz=100
window_sizes=(1 4 16 64 256 512 1024)
thread_counts=(1 2 4 8 16 32 64)
largest_sz=$((256 * 1024 * 1024))
pspin_utils="$(realpath ../../../../utils)"

mkdir -p $data_root

# environment
source ../../sourceme.sh

# build all
make host sender
make

# terminate the background jobs on exit
# https://stackoverflow.com/a/2173421/5520728
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

do_netns="sudo ip netns exec"

kill_slmp() {
    sudo killall -9 slmp &>/dev/null || true

    sleep 0.5
}

scan_till_online() {
    while true; do
        sudo bash -c 'echo 1 > /sys/bus/pci/rescan'
        if [[ -L /sys/bus/pci/devices/0000:$pcie_path ]]; then
            echo Device back online!
            break
        else
            echo -n R
            sleep 5
        fi
    done
}

reset_device() {
    kill_slmp

    scan_till_online

    sudo $pspin_utils/mqnic-fw -d $pcie_path -b -y

    scan_till_online

    sudo $pspin_utils/setup-netns.sh off || true
    sudo $pspin_utils/setup-netns.sh on
}

launch_receiver_clean() {
    need_launches=$1

    kill_slmp

    # XXX: ideally we could use a tighter -m, but somehow this triggers IOMMU pagefaults
    #      we just use 512 MB for now
    sudo host/slmp -o $out_prefix.csv -m $((512 * 1024 * 1024)) -e $need_launches &> $out_prefix.log &
    sleep 0.2
}

# start stdout capture
sudo $pspin_utils/cat_stdout.py --dump-files --clean &>/dev/null &
CAT_STDOUT_PID=$!

# prepare large file
src_file=slmp-file-random.dat
dd if=/dev/urandom of=$src_file bs=$largest_sz count=1

reset_device

for wnd_sz in ${window_sizes[@]}; do
    for threads in ${thread_counts[@]}; do
        if (( wnd_sz < threads )); then
            # skip since each thread will have at least one window
            continue
        fi
        for (( sz = start_sz; sz <= largest_sz; sz *= 2 )); do
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
sudo kill $CAT_STDOUT_PID

echo Done!
