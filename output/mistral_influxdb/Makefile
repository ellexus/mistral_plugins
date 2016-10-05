# ------------------------------------------------------------------------------
# Set up targets and default required PHONY rules

PLUGIN_NAME = mistral_influxdb

LDFLAGS = \
	`curl-config --libs`
	# To compile statically to make the produced binary more portable it should
	# be simply a matter of using --static-libs above however this does not seem
	# to be well supported, either producing poor option lists, not being placed
	# in the correct position in the link command or just simply not being
	# supported at all.
	# 
	# Noteably CentOS doesn't support it. The following options have been
	# demonstrated to work on CentOS systems that install libcurl.a 
	# 
	# gcc mistral_influxdb.o ../../common/plugin_control.o -o mistral_influxdb \
	# -Wl,-Bstatic -lcurl -Wl,-Bdynamic -lgcc_s -Wl,-Bstatic -lssl -lcrypto \
	# -Wl,-Bdynamic -lidn -Wl,-z,relro -lgssapi_krb5 -lkrb5 -lz

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

