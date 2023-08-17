SPIN_APP_NAME = datatypes
SPIN_APP_SRCS = handlers/datatypes.c $(wildcard handlers/umm_malloc/src/*.c)
SPIN_CFLAGS = -O3 -g -flto -Impitypes/install/include/ -Ihandlers/uthash/include/ -Ihandlers/include/
SPIN_LDFLAGS = -lm

LIBS = $(shell realpath ../../../../../../../lib)

SPIN_DRIVER_CC = mpicc
SPIN_DRIVER_CFLAGS = -g -D_GNU_SOURCE -D__FPSPIN_HOST__ -D__VERILATOR__ -I$(LIBS)
SPIN_DRIVER_LDFLAGS = -L$(LIBS)/fpspin -L$(PWD)/typebuilder/ -L$(PWD)/mpitypes/install/lib/ -lpcap -lfpspin -ltypebuilder -lmpitypes

include $(PSPIN_RT)/rules/spin-handlers.mk
include ../generic_driver/gdriver.mk

host:
	$(MAKE) -C host/
sender:
	$(MAKE) -C sender/
baseline:
	$(MAKE) -C baseline/
.PHONY: host sender baseline all
