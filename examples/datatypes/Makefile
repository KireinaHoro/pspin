SPIN_APP_NAME = datatypes
SPIN_APP_SRCS = handlers/datatypes.c
SPIN_CFLAGS = -O3 -g -flto
SPIN_LDFLAGS = -lm

include $(PSPIN_RT)/rules/spin-handlers.mk
include ../generic_driver/gdriver.mk

host:
	$(MAKE) -C host/
.PHONY: host all
