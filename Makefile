# DrakeOS build. `make` builds both boot images:
#   build/drakeos.img  raw disk with the DrakeOS two-stage bootloader
#   build/drakeos.iso  GRUB 2 (Multiboot) CD image
# See `make help` for the run and debug targets.

# Toolchain: the i686-elf cross compiler when installed, else the host gcc in 32-bit freestanding mode.
CROSS   ?= $(if $(shell command -v i686-elf-gcc 2>/dev/null),i686-elf-,)
CC      := $(CROSS)gcc
LD      := $(if $(shell command -v i686-elf-ld 2>/dev/null),i686-elf-ld,ld)
OBJCOPY := $(if $(shell command -v i686-elf-objcopy 2>/dev/null),i686-elf-objcopy,objcopy)
NASM    := nasm
HOSTCC  := cc
QEMU    := qemu-system-i386
GDB     := gdb

BUILD   := build
GCCINC  := $(shell $(CC) -m32 -print-file-name=include)
LIBGCC  := $(shell $(CC) -m32 -print-libgcc-file-name)

CFLAGS  := -m32 -march=i686 -std=gnu11 -O2 -g -ffreestanding -fno-pie -fno-stack-protector \
           -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -mgeneral-regs-only \
           -nostdinc -isystem $(GCCINC) -Wall -Wextra -Werror
LDFLAGS := -m elf_i386 -nostdlib -z noexecstack --no-warn-rwx-segments
NASMFLAGS := -f elf32 -g -F dwarf

# ---------------------------------------------------------------- kernel
KERNEL_C   := $(shell find kernel -name '*.c')
KERNEL_ASM := $(shell find kernel -name '*.asm')
KERNEL_OBJ := $(KERNEL_C:%.c=$(BUILD)/%.o) $(KERNEL_ASM:%.asm=$(BUILD)/%.o)
KERNEL_CFLAGS := $(CFLAGS) -Ikernel/include

# ---------------------------------------------------------------- user programs
ULIB_C     := $(wildcard user/lib/*.c)
ULIB_OBJ   := $(ULIB_C:%.c=$(BUILD)/%.o) $(BUILD)/user/lib/crt0.o
USER_PROGS := $(patsubst user/programs/%.c,%,$(wildcard user/programs/*.c))
USER_ELFS  := $(USER_PROGS:%=$(BUILD)/rootfs/%)
USER_CFLAGS := $(CFLAGS) -Iuser/include

# ---------------------------------------------------------------- outputs
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
INITRD     := $(BUILD)/initrd.img
DISK_IMG   := $(BUILD)/drakeos.img
ISO        := $(BUILD)/drakeos.iso
DATA_IMG   := $(BUILD)/data.img
STAGE1     := $(BUILD)/boot/stage1.bin
STAGE2     := $(BUILD)/boot/stage2.bin
STAGE2_SECTORS := 8

.PHONY: all help kernel iso img run run-grub run-direct debug debug-grub test clean distclean

all: $(DISK_IMG) $(ISO) $(DATA_IMG)

help:
	@echo "make               build/drakeos.img (own bootloader), build/drakeos.iso (GRUB), build/data.img"
	@echo "make run           boot the raw disk image with the DrakeOS bootloader in QEMU"
	@echo "make run-grub      boot the ISO through GRUB in QEMU"
	@echo "make run-direct    boot kernel.elf with QEMU's built-in Multiboot loader"
	@echo "make debug         like run, stopped at the first instruction with gdb attached"
	@echo "make debug-grub    like run-grub, with gdb attached"
	@echo "make test          headless boot tests (both boot paths)"
	@echo "Variables: AUDIO=pa|pipewire|alsa|none (PC speaker backend), MEM=128M"

# ---------------------------------------------------------------- kernel build
$(BUILD)/kernel/%.o: kernel/%.c
	@mkdir -p $(@D)
	$(CC) $(KERNEL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/kernel/%.o: kernel/%.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) -MD $(@:.o=.d) -MP -I$(BUILD)/ -o $@ $<

# The initrd is embedded in the kernel image so both boot paths have it.
$(BUILD)/kernel/fs/initrd_blob.o: $(INITRD)

$(KERNEL_ELF): $(KERNEL_OBJ) kernel/linker.ld
	$(LD) $(LDFLAGS) -T kernel/linker.ld -o $@ $(KERNEL_OBJ) $(LIBGCC)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

kernel: $(KERNEL_ELF)

# ---------------------------------------------------------------- user programs and initrd
$(BUILD)/user/%.o: user/%.c
	@mkdir -p $(@D)
	$(CC) $(USER_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/user/lib/crt0.o: user/lib/crt0.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(BUILD)/rootfs/%: $(BUILD)/user/programs/%.o $(ULIB_OBJ) user/user.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -T user/user.ld -o $@ $(BUILD)/user/lib/crt0.o $< $(filter-out $(BUILD)/user/lib/crt0.o,$(ULIB_OBJ)) $(LIBGCC)

$(BUILD)/tools/mkinitrd: tools/mkinitrd.c
	@mkdir -p $(@D)
	$(HOSTCC) -O2 -Wall -Wextra -o $@ $<

$(INITRD): $(BUILD)/tools/mkinitrd $(USER_ELFS) $(wildcard rootfs/*)
	$(BUILD)/tools/mkinitrd $@ $(wildcard rootfs/*) $(USER_ELFS)

# ---------------------------------------------------------------- DrakeOS bootloader disk image
$(STAGE1): boot/stage1.asm
	@mkdir -p $(@D)
	$(NASM) -f bin -Iboot/ -DSTAGE2_SECTORS=$(STAGE2_SECTORS) -o $@ $<

$(STAGE2): boot/stage2.asm $(wildcard boot/*.inc) $(KERNEL_BIN)
	@mkdir -p $(@D)
	$(NASM) -f bin -Iboot/ -DSTAGE2_SECTORS=$(STAGE2_SECTORS) \
	    -DKERNEL_SECTORS=$$(( ($$(stat -c %s $(KERNEL_BIN)) + 511) / 512 )) -o $@ $<
	@test $$(stat -c %s $@) -le $$(( $(STAGE2_SECTORS) * 512 )) || { echo "stage2 too large"; exit 1; }

img: $(DISK_IMG)
$(DISK_IMG): $(STAGE1) $(STAGE2) $(KERNEL_BIN)
	@test $$(stat -c %s $(KERNEL_BIN)) -le 524288 || { echo "kernel.bin exceeds the 512 KB loader buffer"; exit 1; }
	dd if=/dev/zero of=$@ bs=1M count=4 status=none
	dd if=$(STAGE1) of=$@ conv=notrunc status=none
	dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=$$((1 + $(STAGE2_SECTORS))) conv=notrunc status=none

# ---------------------------------------------------------------- GRUB ISO
iso: $(ISO)
$(ISO): $(KERNEL_ELF) boot/grub.cfg
	@mkdir -p $(BUILD)/isodir/boot/grub
	cp $(KERNEL_ELF) $(BUILD)/isodir/boot/kernel.elf
	cp boot/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	grub-file --is-x86-multiboot $(BUILD)/isodir/boot/kernel.elf
	grub-mkrescue -o $@ $(BUILD)/isodir 2>/dev/null

# ---------------------------------------------------------------- data disk (DrakeFS, created once)
$(DATA_IMG):
	@mkdir -p $(@D)
	dd if=/dev/zero of=$@ bs=1M count=4 status=none

# ---------------------------------------------------------------- running
MEM   ?= 128M
AUDIO ?= pa
QEMU_COMMON := -m $(MEM) -serial stdio -no-reboot \
    -drive file=$(DATA_IMG),format=raw,if=ide,index=1 \
    -audiodev $(AUDIO),id=snd0 -machine pc,pcspk-audiodev=snd0
QEMU_DISK := -drive file=$(DISK_IMG),format=raw,if=ide,index=0
QEMU_CD   := -cdrom $(ISO) -boot d

run: $(DISK_IMG) $(DATA_IMG)
	$(QEMU) $(QEMU_COMMON) $(QEMU_DISK)

run-grub: $(ISO) $(DATA_IMG)
	$(QEMU) $(QEMU_COMMON) $(QEMU_CD)

run-direct: $(KERNEL_ELF) $(DATA_IMG)
	$(QEMU) $(QEMU_COMMON) -kernel $(KERNEL_ELF)

debug: $(DISK_IMG) $(DATA_IMG)
	$(QEMU) $(QEMU_COMMON) $(QEMU_DISK) -s -S &
	$(GDB) -q -x tools/gdbinit $(KERNEL_ELF)

debug-grub: $(ISO) $(DATA_IMG)
	$(QEMU) $(QEMU_COMMON) $(QEMU_CD) -s -S &
	$(GDB) -q -x tools/gdbinit $(KERNEL_ELF)

test: all
	python3 tests/boot_test.py

clean:
	rm -rf $(filter-out $(DATA_IMG),$(wildcard $(BUILD)/*))

distclean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
