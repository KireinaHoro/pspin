#!/usr/bin/env bash

set -eux

fatal() {
    echo "$@" >&2
    exit 1
}

count_single=100
launches=20
pspin="10.0.0.1"
udp_port="15000" # arbitrary
bypass="10.0.0.2"
interval=0.001
data_root="data"
dgping_root="deps/stping"
dgping_bins="$dgping_root/build/bin"
trials="$(seq 16 100 1416)"
pspin_utils="$(realpath ../../../../utils)"
host_wait=3

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
for sz in $trials; do
    out_file=$data_root/udp/baseline-$sz-ping.txt
    rm -f $out_file
    for (( lid = 0; lid < $launches; lid++ )); do
        $do_netns bypass $dgping_bins/dgping $pspin $udp_port -f -i $interval -c $count_single -s $sz >> $out_file
    done
done

sudo kill $DGPINGD_PID

for do_host in false true; do
    make EXTRA_CFLAGS=-DDO_HOST=$do_host

    for sz in $trials; do
        sudo host/udp-ping -o $data_root/udp/$do_host-$sz.csv -e $(($count_single * $launches)) -s $host_wait &>/dev/null &
        sleep 0.2
        out_file=$data_root/udp/$do_host-$sz-ping.txt
        rm -f $out_file
        for (( lid = 0; lid < $launches; lid++ )); do
            $do_netns bypass $dgping_bins/dgping $pspin $udp_port -f -i $interval -c $count_single -s $sz >> $out_file || echo "Packet loss"
        done
        if [[ $do_host == "false" ]]; then
            sleep $host_wait
        fi
    done
done

sudo kill $CAT_STDOUT_PID

echo Done!
