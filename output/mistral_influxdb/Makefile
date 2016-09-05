# ------------------------------------------------------------------------------
# Set up targets and default required PHONY rules

PLUGIN_NAME = mistral_influxdb

LDFLAGS = \
	-lcurl

TARGETS = \
	$(PLUGIN_NAME)

PLUGIN_FRAMEWORK_DIR = \
	../../common

STANDARD_OBJECTS = \
	$(PLUGIN_FRAMEWORK_DIR)/plugin_control.o

PLUGIN_OBJECTS = \
	$(PLUGIN_NAME).o

PLUGIN_DEPS = \
	$(PLUGIN_NAME).c

DIRECTORIES = \
	$(PLUGIN_FRAMEWORK_DIR)

CLEANDIRECTORIES = \
	$(addsuffix PHONYclean,$(DIRECTORIES))

.PHONY: all
all: dirs $(TARGETS)

.PHONY: clean
clean: $(CLEANDIRECTORIES)
	rm -f *.o $(TARGETS)

.PHONY: dirs $(DIRECTORIES)
dirs: $(DIRECTORIES)

$(DIRECTORIES):
	$(MAKE) -C $@

.PHONY: %PHONYclean
%PHONYclean:
	$(MAKE) -C $* clean

# ------------------------------------------------------------------------------
# GCC -- just set it to gcc

GCC ?= gcc
CC = $(GCC)

# ------------------------------------------------------------------------------
# CFLAGS -- compilation flags.

CFLAGS += \
	-std=gnu99 \
	-D_GNU_SOURCE \
	-Wall \
	-Wextra \
	-Werror \
	-Wcast-align \
	-Wformat=2 \
	-Wmissing-noreturn \
	-Wno-attributes \
	-Wpointer-arith \
	-Wredundant-decls \
	-Wshadow \
	-fPIC \
	-pthread \
	-I $(DIRECTORIES)


ifneq (,$(DEBUG))
CFLAGS += -gdwarf-2
LDFLAGS += -g
else
CFLAGS += \
	-O3 \
	-fvisibility=hidden
endif

# ------------------------------------------------------------------------------
# Set up a default rule that sets up a dependency on both .c and .h files

%.o: %.c %.h
	$(GCC) $(CFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# Set up dependencies for the plug-in

$(PLUGIN_NAME): $(STANDARD_OBJECTS) $(PLUGIN_OBJECTS)

$(PLUGIN_NAME).o: $(STANDARD_DEPS) $(PLUGIN_DEPS)

