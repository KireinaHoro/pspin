CC = gcc
CFLAGS ?= -O3 -g -fsanitize=address -march=native
LIBS ?= $(shell realpath ../../../../../../../../lib)

CFLAGS += -Wall
CPPFLAGS += -I$(LIBS) -D__FPSPIN_HOST__
LDFLAGS += -L$(LIBS)/fpspin
LDLIBS += -lfpspin

BIN = slmp_sender

GENDEPFLAGS = -MD -MP -MF .$(@F).d

ALL_CFLAGS = $(CFLAGS) $(CPPFLAGS) $(GENDEPFLAGS)

all: $(BIN)

FORCE:

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

$(BIN): sender.o
	$(MAKE) -C $(LIBS)/fpspin
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(BIN)
	rm -f *.o
	rm -f .*.d

-include $(wildcard .*.d)

.PHONY: all clean FORCE
