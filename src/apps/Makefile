TARGET_CFLAGS += -ffreestanding -nostdlib
TARGET_LINKFLAGS += -nostdlib

#
# libc
#

LIBC_DIR = libc
LIBC_INCLUDE = $(LIBC_DIR)/include

LIBC_SRC := $(shell find $(LIBC_DIR)/src -name "*.c")
LIBC_OBJ := $(patsubst $(LIBC_DIR)/src/%.c,$(BUILD_DIR)/libc/%.o,$(LIBC_SRC))

LIBC = $(BUILD_DIR)/libc/libc.a
CRT0 = $(BUILD_DIR)/libc/crt0.o

#
# program detection
#

# exclude libc/ from program folders
PROGRAM_DIRS := $(filter-out libc/,$(wildcard */))
PROGRAMS := $(patsubst %/,%,$(PROGRAM_DIRS))

#
# phony
#

.PHONY: all clean libc programs

all: libc programs

#
# build libc
#

libc: $(LIBC) $(CRT0)

$(LIBC): $(LIBC_OBJ)
	@mkdir -p $(@D)
	@ar rcs $@ $^
	@echo "--> Created libc.a"

$(BUILD_DIR)/libc/%.o: $(LIBC_DIR)/src/%.c
	@mkdir -p $(dir $@)
	@$(TARGET_CC) $(TARGET_CFLAGS) -I$(LIBC_INCLUDE) -c $< -o $@
	@echo "--> Compiled libc: $<"

$(CRT0): $(LIBC_DIR)/crt0.c
	@mkdir -p $(@D)
	@$(TARGET_CC) $(TARGET_CFLAGS) -I$(LIBC_INCLUDE) -c $< -o $@
	@echo "--> Compiled crt0"

#
# programs
#

programs: $(PROGRAMS)

define PROGRAM_template

$1_SRC := $$(shell find $1 -name "*.c")
$1_OBJ := $$(patsubst $1/%.c,$$(BUILD_DIR)/apps/$1/%.o,$$($1_SRC))
$1_BIN := $$(BUILD_DIR)/apps/$1.bin

$1: $$($1_BIN)

$$($1_BIN): $$($1_OBJ) $(CRT0) $(LIBC)
	@mkdir -p $$(@D)
	@$$(TARGET_LD) $$(TARGET_LINKFLAGS) -no-pie -o $$@ $(CRT0) $$($1_OBJ) $(LIBC) $$(TARGET_LIBS)
	@echo "--> Created program: $1 (no-pie)"

$$(BUILD_DIR)/apps/$1/%.o: $1/%.c
	@mkdir -p $$(dir $$@)
	@$$(TARGET_CC) $$(TARGET_CFLAGS) -I$(LIBC_INCLUDE) -c $$< -o $$@
	@echo "--> Compiled $1: $$<"

endef

$(foreach prog,$(PROGRAMS),$(eval $(call PROGRAM_template,$(prog))))

#
# clean
#

clean:
	rm -rf $(BUILD_DIR)/apps
	rm -rf $(BUILD_DIR)/libc