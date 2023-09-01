#!/usr/bin/env bash

set -eux

fatal() {
    echo "$@" >&2
    exit 1
}

launches=50
pspin="10.0.0.1"
bypass="10.0.0.2"
data_root="data"
start_sz=100
largest_sz=2000000
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

# check if netns is correctly setup
if ! $do_netns pspin ip a | grep $pspin; then
    fatal "PsPIN NIC not found in netns.  Please rerun setup"
fi

# start stdout capture
sudo $pspin_utils/cat_stdout.py --dump-files --clean &>/dev/null &
CAT_STDOUT_PID=$!

# prepare large file
src_file=slmp-file-random.dat
dd if=/dev/urandom of=$src_file bs=$largest_sz count=1

sudo killall slmp || true # cleanup stale failures

for do_ack in 0 1; do
    for (( sz = $start_sz; sz <= $largest_sz; sz *= 2 )); do
        # XXX: ideally we could use a tighter -m, but somehow this triggers IOMMU pagefaults
        #      we just use 128 MB for now
        sudo host/slmp -o $data_root/$do_ack-$sz.csv -m $((128 * 1024 * 1024)) -e $launches &>/dev/null &
        sleep 1

        out_file=$data_root/$do_ack-$sz-sender.txt
        rm -f $out_file
        ack=
        if [[ $do_ack == 1 ]]; then
            ack="-a"
        fi
        for (( lid = 0; lid < $launches; lid++ )); do
            $do_netns bypass sender/slmp_sender -f $src_file -s $pspin -l $sz $ack >> $out_file || echo "Timed out!"
            sleep 0.1
        done

        sleep 1
    done
done

rm $src_file
sudo kill $CAT_STDOUT_PID

echo Done!
