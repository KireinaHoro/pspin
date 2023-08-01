SPIN_APP_NAME = datatypes
SPIN_APP_SRCS = handlers/datatypes.c $(wildcard handlers/umm_malloc/src/*.c)
SPIN_CFLAGS = -Os -g -flto -Impitypes/install/include/ -Ihandlers/uthash/include/ -Ihandlers/include/
SPIN_LDFLAGS = -lm

include $(PSPIN_RT)/rules/spin-handlers.mk
include ../generic_driver/gdriver.mk

host:
	$(MAKE) -C host/
sender:
	$(MAKE) -C sender/
.PHONY: host sender all
