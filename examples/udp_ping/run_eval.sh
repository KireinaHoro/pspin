#!/usr/bin/env bash

set -eux

fatal() {
    echo "$@" >&2
    exit 1
}

expect=1000
pspin="10.0.0.1"
udp_port="15000" # arbitrary
bypass="10.0.0.2"
interval=0.001
data_root="data"
dgping_root="deps/stping"
dgping_bins="$dgping_root/build/bin"
trials_count="$(seq 16 100 1416)"
pspin_utils="$(realpath ../../../../utils)"

mkdir -p $data_root/udp

# environment
source ../../sourceme.sh

# build all
make host

# terminate the background jobs on exit
# https://stackoverflow.com/a/2173421/5520728
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

# build dgping
pushd $dgping_root
bmake -r
popd

do_netns="sudo ip netns exec"

# check if netns is correctly setup
if ! $do_netns pspin ip a | grep $pspin; then
    fatal "PsPIN NIC not found in netns.  Please rerun setup"
fi

# start stdout capture
sudo $pspin_utils/cat_stdout.py --dump-files --clean &>/dev/null &
CAT_STDOUT_PID=$!

# baseline - bypass
$do_netns pspin $dgping_bins/dgpingd $pspin $udp_port -q -f &>/dev/null &
DGPINGD_PID=$!
for sz in $trials_count; do
    $do_netns bypass $dgping_bins/dgping $pspin $udp_port -f -i $interval -c $expect -s $sz -q > $data_root/udp/baseline-$sz-ping.txt
done

sudo kill $DGPINGD_PID

for do_host in false true; do
    make EXTRA_CFLAGS=-DDO_HOST=$do_host

    for sz in $trials_count; do
        sudo host/udp-ping -o $data_root/udp/$do_host-$sz.csv -e $expect -s 2 &>/dev/null &
        sleep 0.2
        $do_netns bypass $dgping_bins/dgping $pspin $udp_port -f -i $interval -c $expect -s $sz -q > $data_root/udp/$do_host-$sz-ping.txt
        if [[ $do_host == "false" ]]; then
            sleep 2
        fi
    done
done

sudo kill $CAT_STDOUT_PID

echo Done!
