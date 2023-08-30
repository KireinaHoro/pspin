SPIN_APP_NAME = udp_ping
SPIN_APP_SRCS = handlers/udp_ping.c
SPIN_CFLAGS = -O3 -g -flto $(EXTRA_CFLAGS)
SPIN_LDFLAGS = -lm

include $(PSPIN_RT)/rules/spin-handlers.mk
include ../generic_driver/gdriver.mk

host:
	$(MAKE) -C host/
.PHONY: host all
