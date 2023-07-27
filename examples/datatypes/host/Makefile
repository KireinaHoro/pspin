CC = mpicc
CFLAGS ?= -O3 -g -fsanitize=address -march=native
LIBS ?= $(shell realpath ../../../../../../../../lib)

IMG_PATH := build/datatypes

CFLAGS += -Wall -D__FPSPIN_HOST__
CPPFLAGS += -I$(LIBS) -D__IMG__=\"$(IMG_PATH)\"
LDFLAGS += -L$(LIBS)/fpspin -L$(PWD)/typebuilder/ -L$(PWD)/mpitypes/install/lib/
LDLIBS += -lfpspin -ltypebuilder -lmpitypes -lopenblas

LIBFPSPIN = $(LIBS)/fpspin/libfpspin.a

BIN = datatypes

GENDEPFLAGS = -MD -MP -MF .$(@F).d

ALL_CFLAGS = $(CFLAGS) $(CPPFLAGS) $(GENDEPFLAGS)

all: $(BIN)

FORCE:

$(LIBFPSPIN): FORCE
	$(MAKE) -C $(dir $@) $(notdir $@)

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

datatypes: datatypes.o $(LIBFPSPIN)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(BIN)
	rm -f *.o
	rm -f .*.d

-include $(wildcard .*.d)

.PHONY: all clean FORCE
