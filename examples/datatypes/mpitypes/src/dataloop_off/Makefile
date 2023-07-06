CC_SPIN = aarch64-linux-gnu-gcc-8 # C compiler
CFLAGS_SPIN = -fPIC -Wall -Wextra -funroll-loops --static -march=armv8-a -O3 -g -I ../../../handlers/ -I ./ -DSPIN # C flags
LDFLAGS_SPIN = -shared  # linking flags

CC_HOST = mpicc
CFLAGS_HOST = -fPIC -Wall -Wextra -funroll-loops --static -O3 -g -I ../../../handlers -I ./ -DSPIN

RM = rm -f  # rm command

TARGET_LIB_SPIN = libdataloop_spin.a # target lib
TARGET_LIB_HOST = libdataloop_host.a

SRCS_SPIN = dataloop.c segment.c ../../../handlers/mpitypes_dataloop.c
SRCS_HOST = dataloop.c segment.c segment_packunpack.c ../../../handlers/mpitypes_dataloop.c

#subarray_support.c typesize_support.c darray_support.c content_support.c

OBJS_SPIN = $(SRCS_SPIN:.c=.spin.o)
OBJS_HOST = $(SRCS_HOST:.c=.host.o)

.PHONY: all
all: ${TARGET_LIB_SPIN} $(TARGET_LIB_HOST)

$(TARGET_LIB_SPIN): $(OBJS_SPIN)
	ar ru $@ $^
	ranlib $@

$(TARGET_LIB_HOST): $(OBJS_HOST)
	ar ru $@ $^
	ranlib $@

%.spin.o: %.c
	$(CC_SPIN) -c -o $@ $< $(CFLAGS_SPIN) 

%.host.o: %.c
	$(CC_HOST) -c -o $@ $< $(CFLAGS_HOST) 


include $(SRCS:.c)

.PHONY: clean
clean:
	-${RM} ${TARGET_LIB_SPIN} ${TARGET_LIB_HOST} ${OBJS_SPIN} $(OBJS_HOST) $(SRCS:.c=.d)
