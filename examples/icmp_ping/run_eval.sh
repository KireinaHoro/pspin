#!/usr/bin/env bash

set -eux

fatal() {
    echo "$@" >&2
    exit 1
}

expect=1000
pspin="10.0.0.1"
bypass="10.0.0.2"
data_root="data"
trials_count="$(seq 16 100 1416)"
pspin_utils="$(realpath ../../../../utils)"

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
for sz in $trials_count; do
    $do_netns bypass ping $pspin -f -c $expect -s $sz -q > $data_root/icmp/baseline-$sz-ping.txt
done

for do_host in true false; do
    make EXTRA_CFLAGS=-DDO_HOST=$do_host

    for sz in $trials_count; do
        nohup sudo host/icmp-ping -o $data_root/icmp/$do_host-$sz.csv -e $expect -s 1 &>/dev/null &
        sleep 0.2
        $do_netns bypass ping $pspin -f -c $expect -s $sz -q > $data_root/icmp/$do_host-$sz-ping.txt
        sleep 1
    done
done

echo Done!
