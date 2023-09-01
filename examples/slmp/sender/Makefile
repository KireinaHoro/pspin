CC = gcc
CFLAGS ?= -O3 -g -fsanitize=address -march=native
LIBS ?= $(shell realpath ../../../../../../../../lib)

CFLAGS += -Wall -fopenmp
CPPFLAGS += -I$(LIBS) -D__FPSPIN_HOST__
LDFLAGS += -L$(LIBS)/fpspin
LDLIBS += -lfpspin

LIBFPSPIN = $(LIBS)/fpspin/libfpspin.a

BIN = slmp_sender

GENDEPFLAGS = -MD -MP -MF .$(@F).d

ALL_CFLAGS = $(CFLAGS) $(CPPFLAGS) $(GENDEPFLAGS)

all: $(BIN)

FORCE:

$(LIBFPSPIN): FORCE
	$(MAKE) -C $(dir $@) $(notdir $@)

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

$(BIN): sender.o $(LIBFPSPIN)
	$(MAKE) -C $(LIBS)/fpspin
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

clean:
	rm -f $(BIN)
	rm -f *.o
	rm -f .*.d

-include $(wildcard .*.d)

.PHONY: all clean FORCE
