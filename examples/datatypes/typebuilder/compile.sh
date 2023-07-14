#!/bin/bash

set -eux

MPITYPES_SRC="../mpitypes/src"
MPITYPES_ROOT="../mpitypes/install"
LIBLSB_ROOT="../liblsb/install"

make -C ddtparser clean
make -C ddtparser

mpic++ -I $MPITYPES_ROOT/include/ -I $MPITYPES_SRC/dataloop/ typebuilder_main.cc ddt_io_read.cc ddt_io_write.cc typebuilder.cc ddtparser/libddtparser.a -o typebuilder -L $MPITYPES_ROOT/lib/ -lmpitypes -Wno-address-of-packed-member -fpermissive -g


mpic++ -I $MPITYPES_ROOT/include/ -I $MPITYPES_SRC/dataloop/ -I$LIBLSB_ROOT/include -L$LIBLSB_ROOT/lib ddt_io_read.cc ddt_io_write.cc typetester.cc ddtparser/libddtparser.a -o typetester -L $MPITYPES_ROOT/lib/ -lmpitypes -llsb -lpapi -Wno-address-of-packed-member -fpermissive -g

mpicc -I $MPITYPES_ROOT/include/ -I $MPITYPES_SRC/dataloop/ -c -Wall -Werror -fpic typebuilder.cc -Wno-address-of-packed-member -fpermissive -g
mpicc -I $MPITYPES_ROOT/include/ -I $MPITYPES_SRC/dataloop/ -c -fpic ddt_io_write.cc -Wno-address-of-packed-member -fpermissive -g
mpicc -I $MPITYPES_ROOT/include/ -I $MPITYPES_SRC/dataloop/ -c -fpic ddt_io_read.cc -Wno-address-of-packed-member -fpermissive -g
mpicc -shared -o libtypebuilder.so typebuilder.o ddt_io_write.o ddt_io_read.o -Wno-address-of-packed-member -fpermissive -g
