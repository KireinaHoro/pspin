#!/usr/bin/env bash

set -eux

fatal() {
    echo "$@" >&2
    exit 1
}

count_single=100
launches=20
pspin="10.0.0.1"
bypass="10.0.0.2"
data_root="data"
trials="$(seq 16 100 1416)"
pspin_utils="$(realpath ../../../../utils)"
interval=0.001
host_wait=3

mkdir -p $data_root/icmp

# environment
source ../../sourceme.sh

# build all
make host

# terminate the background jobs on exit
# https://stackoverflow.com/a/2173421/5520728
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

do_netns="sudo ip netns exec"

# check if netns is correctly setup
if ! $do_netns pspin ip a | grep $pspin; then
    fatal "PsPIN NIC not found in netns.  Please rerun setup"
fi

# start stdout capture
nohup sudo $pspin_utils/cat_stdout.py --dump-files --clean &>/dev/null &

# baseline - bypass
for sz in $trials; do
    out_file=$data_root/icmp/baseline-$sz-ping.txt
    rm -f $out_file
    for (( lid = 0; lid < $launches; lid++ )); do
        $do_netns bypass ping $pspin -i $interval -c $count_single -s $sz >> $out_file
    done
done

for do_host in true false; do
    make EXTRA_CFLAGS=-DDO_HOST=$do_host

    for sz in $trials; do
        nohup sudo host/icmp-ping -o $data_root/icmp/$do_host-$sz.csv -e $(($count_single * $launches)) -s $host_wait &>/dev/null &
        sleep 0.2
        out_file=$data_root/icmp/$do_host-$sz-ping.txt
        rm -f $out_file
        for (( lid = 0; lid < $launches; lid++ )); do
            $do_netns bypass ping $pspin -i $interval -c $count_single -s $sz >> $out_file
        done
        if [[ $do_host == "false" ]]; then
            sleep $host_wait
        fi
    done
done

echo Done!
