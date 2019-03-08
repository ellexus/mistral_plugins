# ------------------------------------------------------------------------------
# Set up targets and default required PHONY rules

TARGETS = \
	plugin_control.o

DEPENDENCIES = \
	plugin_control.c \
	plugin_control.h \
	mistral_plugin.h \
	Makefile

.PHONY: all
all: $(TARGETS)

.PHONY: clean
clean:
	rm -f *.o

# ------------------------------------------------------------------------------
# GCC -- If possible then use the same compiler as used to compile Mistral

GCC ?= gcc
CC = $(GCC)

# ------------------------------------------------------------------------------
# CFLAGS -- compilation flags on all target platforms.

CFLAGS += \
	-D_GNU_SOURCE \
	-Wall \
	-Wcast-align \
	-Werror \
	-Wextra \
	-Wformat=2 \
	-Wmissing-noreturn \
	-Wno-attributes \
	-Wpointer-arith \
	-Wredundant-decls \
	-Wshadow \
	-pthread \
	-std=gnu99

ifneq (,$(DEBUG))
CFLAGS += -gdwarf-2
LDFLAGS += -g
else
CFLAGS += -O3
endif

# ------------------------------------------------------------------------------
# Set up a default rule that sets up a dependency on both .c and .h files

%.o: %.c %.h
	$(GCC) $(CFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# and immediately override it for plugin_control!

plugin_control.o: $(DEPENDENCIES)
	$(GCC) $(CFLAGS) -c -o $@ $<
