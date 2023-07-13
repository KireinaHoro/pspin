CC = gcc
CFLAGS ?= -O3 -g -fsanitize=address -march=native
LIBS ?= $(shell realpath ../../../../../../../../lib)

CFLAGS += -Wall
CPPFLAGS += -I$(LIBS) -D__FPSPIN_HOST__

BIN = slmp_sender

GENDEPFLAGS = -MD -MP -MF .$(@F).d

ALL_CFLAGS = $(CFLAGS) $(CPPFLAGS) $(GENDEPFLAGS)

all: $(BIN)

FORCE:

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

$(BIN): sender.o
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(BIN)
	rm -f *.o
	rm -f .*.d

-include $(wildcard .*.d)

.PHONY: all clean FORCE
