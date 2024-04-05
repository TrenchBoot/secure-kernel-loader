# Start with empty flags
CFLAGS  :=
LDFLAGS :=

ifeq ($(DEBUG),y)
CFLAGS  += -DDEBUG
endif

ifeq ($(LTO),y)
CFLAGS  += -flto
LDFLAGS += -flto
endif

BITS ?= 64
ifeq ($(BITS),32)
CFLAGS  += -m32 -mregparm=3 -fno-plt -freg-struct-return
LDFLAGS += -m32
else ifeq ($(BITS),64)
CFLAGS  += -m64
LDFLAGS += -m64
else
$(error Bad $$(BITS) value '$(BITS)')
endif

# There is a 64k total limit, so optimise for size.  The binary may be loaded
# at an arbitrary location, so build it as position independent, but link as
# non-pie as all relocations are internal and there is no dynamic loader to
# help.
CFLAGS  += -Os -g -MMD -MP -mno-sse -mno-mmx -fpie -fomit-frame-pointer
CFLAGS  += -Iinclude -ffreestanding -fno-common -Wall -Werror
LDFLAGS += -nostdlib -no-pie -Wl,--build-id=none,--fatal-warnings,--defsym=BITS=$(BITS)

CFLAGS_TPMLIB := -include boot.h -include errno-base.h -include byteswap.h -DEBADRQC=EINVAL

# Derive AFLAGS from CFLAGS
AFLAGS := -D__ASSEMBLY__ $(filter-out -std=%,$(CFLAGS))

ALL_SRC := $(wildcard *.c) $(wildcard tpmlib/*.c)
TESTS := $(filter test-%,$(ALL_SRC:.c=))

# Collect objects for building.  For simplicity, we take all ASM/C files except tests
ASM := $(wildcard *.S)
SRC := $(filter-out test-%,$(ALL_SRC))
OBJ := $(ASM:.S=.o) $(SRC:.c=.o)

.PHONY: all
all: skl.bin

-include Makefile.local

# Generate a flat binary
#
# As a sanity check, look for the SKL UUID at its expected offset in the binary
# image.  One reason this might fail is if the linker decides to put an
# unreferenced section ahead of .text, in which case link.lds needs adjusting.
skl.bin: skl Makefile
	objcopy -O binary -S -R '.note.*' -R '.got.plt' $< $@
	@./sanity_check.sh

skl: link.lds $(OBJ) Makefile
	$(CC) -Wl,-T,link.lds $(LDFLAGS) $(OBJ) -o $@

tpmlib/%.o: tpmlib/%.c Makefile
	$(CC) $(CFLAGS) $(CFLAGS_TPMLIB) -o $@ -c $<

%.o: %.c Makefile
	$(CC) $(CFLAGS) -o $@ -c $<

%.o: %.S Makefile
	$(CC) $(AFLAGS) -c $< -o $@

# Helpers for debugging.  Preprocess and/or compile only.
%.E: %.c Makefile
	$(CC) $(CFLAGS) -E $< -o $@
%.S: %.c Makefile
	$(CC) $(CFLAGS) -S $< -o $@

# Helpers for building and running tests on the current host
test-%: test-%.c Makefile
	$(CC) $(filter-out -ffreestanding -march%,$(CFLAGS)) $(if $(COV),-fprofile-arcs -ftest-coverage) $< -o $@

.PHONY: run-test-%
.SECONDARY:
run-test-%: test-% Makefile
	./$<

# Wrapper for building and running every test-*.c we find.
.PHONY: tests
tests: $(addprefix run-,$(TESTS))

.PHONY: cscope
cscope:
	find . -name "*.[hcsS]" > cscope.files
	cscope -b -q -k

.PHONY: clean
clean:
	rm -f skl.bin skl $(TESTS) *.d *.o *.gcov *.gcda *.gcno tpmlib/*.d tpmlib/*.o cscope.*

# Compiler-generated header dependencies.  Should be last.
-include $(OBJ:.o=.d) $(TESTS:=.d)
