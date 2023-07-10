CC ?= gcc
CFLAGS ?= -O3 -g -fsanitize=address -march=native
LIBS ?= $(shell realpath ../../../../../../../../lib)

IMG_PATH := build/icmp_ping

CFLAGS += -Wall -D__FPSPIN_HOST__
CPPFLAGS += -I$(LIBS) -D__IMG__=\"$(IMG_PATH)\"
LDFLAGS += -L$(LIBS)/fpspin
LDLIBS += -lfpspin

LIBFPSPIN = $(LIBS)/fpspin/libfpspin.a

BIN = icmp-ping

GENDEPFLAGS = -MD -MP -MF .$(@F).d

ALL_CFLAGS = $(CFLAGS) $(CPPFLAGS) $(GENDEPFLAGS)

all: $(BIN)

FORCE:

$(LIBFPSPIN): FORCE
	$(MAKE) -C $(dir $@) $(notdir $@)

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

icmp-ping: icmp-ping.o $(LIBFPSPIN)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(BIN)
	rm -f *.o
	rm -f .*.d

-include $(wildcard .*.d)

.PHONY: all clean FORCE
