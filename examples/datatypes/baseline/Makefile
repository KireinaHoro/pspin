CC = mpicc
CXX = mpicxx
CFLAGS ?= -O3 -g -march=native -I../mpitypes/install/include -fsanitize=address
LIBS ?= $(shell realpath ../../../../../../../../lib)

CFLAGS += -Wall
CPPFLAGS += -I$(LIBS) -D__FPSPIN_HOST__

LDFLAGS += -L../typebuilder/ -L../mpitypes/install/lib/
LDLIBS += -ltypebuilder -lmpitypes ../typebuilder/ddtparser/libddtparser.a

LDFLAGS += -L$(LIBS)/fpspin
LDLIBS += -lfpspin

LIBFPSPIN = $(LIBS)/fpspin/libfpspin.a

BIN = datatypes_baseline

GENDEPFLAGS = -MD -MP -MF .$(@F).d

ALL_CFLAGS = $(CFLAGS) $(CPPFLAGS) $(GENDEPFLAGS)

all: $(BIN)

FORCE:

$(LIBFPSPIN): FORCE
	$(MAKE) -C $(dir $@) $(notdir $@)

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

$(BIN): baseline.o $(LIBFPSPIN)
	$(CXX) $(ALL_CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -f $(BIN)
	rm -f *.o
	rm -f .*.d

-include $(wildcard .*.d)

.PHONY: all clean FORCE
