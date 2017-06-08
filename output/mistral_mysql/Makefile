# ------------------------------------------------------------------------------
# Set up targets and default required PHONY rules

PLUGIN_NAME = mistral_mysql

# ------------------------------------------------------------------------------
# HOST_ARCH -- Native architecture, normalized to elide i386/i686
# distinction.

HOST_ARCH := $(patsubst i%86,i386,$(shell uname -m))
$(eval $(call check_arch,$(HOST_ARCH)))

LDLIBS := \
	$(shell mysql_config --libs) \
	-luuid

INCLUDE_FLAGS := \
	$(shell mysql_config --include)

TARGETS = \
	$(PLUGIN_NAME)

PACKAGE = \
	$(PLUGIN_NAME).$(HOST_ARCH)

PLUGIN_FRAMEWORK_DIR = \
	../../common

STANDARD_OBJECTS = \
	$(PLUGIN_FRAMEWORK_DIR)/plugin_control.o

PLUGIN_OBJECTS = \
	$(PLUGIN_NAME).o

PLUGIN_DEPS = \
	$(PLUGIN_NAME).c \
	Makefile

DIRECTORIES = \
	$(PLUGIN_FRAMEWORK_DIR)

CLEANDIRECTORIES = \
	$(addsuffix PHONYclean,$(DIRECTORIES))

.PHONY: all
all: dirs $(TARGETS)

.PHONY: package
package: dirs $(TARGETS) $(PACKAGE)

.PHONY: clean
clean: $(CLEANDIRECTORIES)
	rm -f *.o $(TARGETS) $(PACKAGE)

.PHONY: dirs $(DIRECTORIES)
dirs: $(DIRECTORIES)

$(DIRECTORIES):
	$(MAKE) -C $@

.PHONY: %PHONYclean
%PHONYclean:
	$(MAKE) -C $* clean

# ------------------------------------------------------------------------------
# GCC -- If possible then use the same compiler as used to compile Mistral

GCC ?= /usr/local/install/gcc-4.8.2/bin/gcc
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
	-I $(PLUGIN_FRAMEWORK_DIR) \
	$(INCLUDE_FLAGS)


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
# Set up dependencies for the plug-in

$(PLUGIN_NAME): $(STANDARD_OBJECTS) $(PLUGIN_OBJECTS)

$(PLUGIN_NAME).o: $(STANDARD_DEPS) $(PLUGIN_DEPS)

# Try to build a statically linked version of the plugin. (The build machines
# have all the necessary static libraries installed, but the libraries may not
# be installed on other machines.)

$(PLUGIN_NAME).$(HOST_ARCH) : $(PLUGIN_NAME) $(STANDARD_OBJECTS) $(PLUGIN_OBJECTS)
	$(GCC) -o $@ $(STANDARD_OBJECTS) $(PLUGIN_OBJECTS) \
		$(shell ../../tools/static-link.sh $(PLUGIN_NAME) $(LDLIBS))
