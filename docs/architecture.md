# DrakeOS architecture

## Boot

Both boot paths end in the same state, so the kernel has a single entry point:
32-bit protected mode, flat segments, paging off, `EAX = 0x2BADB002`, `EBX` = physical
address of a Multiboot information structure, kernel image at physical 1 MB.

**DrakeOS bootloader** (`build/drakeos.img`):

| Sector | Content |
|---|---|
| 0 | `stage1.asm`: sets up segments and stack, reads stage 2 (CHS, first track) to 0x8000 |
| 1–8 | `stage2.asm` (≤ 4 KB) |
| 9– | `kernel.bin` (`objcopy -O binary` of `kernel.elf`, starts at physical 0x100000) |

Stage 2 enables A20 (BIOS, then port 0x92; verified by the wrap-around test), fills a
`multiboot_info_t` at 0x6000 (memory sizes from int 0x12 / 0xE801, E820 map at 0x6100,
loader name), reads the kernel one sector at a time into a staging buffer at 0x10000 (BIOS
extended read when available, CHS with the drive geometry otherwise; at most 512 KB), loads
a flat GDT, sets CR0.PE, far-jumps to 32-bit code, copies the kernel to 1 MB and jumps to it.

**GRUB 2**: `grub.cfg` runs `multiboot /boot/kernel.elf`; GRUB reads the ELF program headers.

**Kernel entry** (`kernel/arch/i386/boot.asm`): the `.boot` section is linked at its physical
address. It zeroes `.bss`, loads a static page directory that maps the first 16 MB twice
(identity and at 0xC0000000, 4 MB pages), enables PSE and paging, and jumps to the higher
half, where `kmain(magic, mbi)` runs on a 16 KB boot stack.

## Memory map

| Virtual range | Use |
|---|---|
| 0x00000000–0x00000FFF | unmapped (NULL dereferences fault) |
| 0x08048000– | user program (ELF segments) |
| 0xBFFFB000–0xBFFFEFFF | user stack (4 pages) |
| 0xC0000000–0xCFFFFFFF | physical 0–256 MB, 4 MB pages, kernel only. The kernel is at 0xC0100000; page tables and frames are reached through `P2V()`. |
| 0xD0000000–0xD3FFFFFF | kernel heap. Its 16 page tables are created at boot, so kernel PDEs never change after a process copies them. |

Physical frames come from the E820 map (`pmm.c`, one bit per 4 KB frame, first 256 MB).
The first megabyte and the kernel image are never allocated. Each process has its own page
directory; entries 768–1023 are copied from the kernel directory.

## Interrupts

* GDT: null, kernel code/data, user code/data (DPL 3), TSS (only `esp0`/`ss0` are used).
* IDT: vectors 0–31 exceptions, 32–47 IRQs (PIC remapped), 0x80 system calls (DPL 3).
  All gates are interrupt gates, so the kernel runs with interrupts disabled during
  interrupt and system call handling. Kernel threads run with interrupts enabled; shared
  structures (heap, frame bitmap, run queue, keyboard buffer, console, disk) are protected
  with `irq_save()`/`irq_restore()`.
* One assembly stub per vector pushes a uniform `registers_t` frame and calls
  `interrupt_dispatch(registers_t *)`. IRQs are acknowledged before their handler runs,
  because the timer handler may switch tasks; spurious IRQ 7/15 are detected.
* An exception in ring 3 kills only that process (`process_check_signals` runs at the end
  of every interrupt); an exception in ring 0 panics.

## Processes

`struct process` (`proc.h`) holds the saved kernel `esp`, a 16 KB kernel stack, the page
directory, the state and file descriptors. pid 0 is the boot context, which becomes the
idle task (`sti; hlt`).

* **Switching**: `switch_context(&old->esp, new->esp)` pushes and pops the callee-saved
  registers and swaps stacks. Before the switch, the scheduler loads the next page directory
  and sets `TSS.esp0` to its kernel stack.
* **New threads and processes** get a hand-made kernel stack whose first `ret` lands in
  `kthread_entry` (calls the function, then `process_exit`) or `user_entry` (loads user data
  segments and `iret`s into ring 3 using a prepared frame: ss, esp, eflags with IF, cs, eip).
* **Scheduling**: round robin over the process table. The PIT (100 Hz) preempts the
  running process every 2 ticks. `process_block(channel)` / `process_wakeup(channel)`
  implement waiting (keyboard input, `wait()`); sleeping processes are woken by the tick.
* **Exit**: a process becomes a zombie. `wait()` reaps it; detached processes (`run … &`,
  kernel threads, orphans) are reaped by the scheduler once they are no longer running.

User argument passing: `process_spawn` copies argv strings and pointers to the top of the
user stack and leaves argc and argv at `[esp]` and `[esp+4]` for `crt0`.

## Files

| Path | Backend |
|---|---|
| `/NAME` | initrd: built by `tools/mkinitrd` from `rootfs/*` and the user programs, embedded in the kernel with `incbin` so both boot paths have it |
| `/disk/NAME` | DrakeFS on the ATA primary-slave disk |
| `/dev/console` | read: keyboard line input with echo; write: the console |

DrakeFS: sector 0 is the superblock (`DRAKEFS1`); sectors 1–8 hold the directory (64 entries
of 64 bytes: name[48], start, size, used); each file is one contiguous extent allocated
first-fit from sector 9. Files opened for writing are buffered in memory and committed on
close. The ATA driver polls (nIEN set), checks BSY/DRQ/ERR and flushes the write cache.

## Graphics

`gfx.c` switches between 80×25 text mode and 320×200×256 by writing the standard VGA
register sets (misc, sequencer, CRTC, graphics controller, attribute controller). The text
font (plane 2) and the DAC palette are saved at boot, and the text screen is saved on each
switch, because mode 13h overwrites all three. Drawing goes to a back buffer copied to
0xA0000 by `gfx_present()`. Text in graphics mode uses the saved 8×16 VGA font.

## Debugging

* `make debug` / `make debug-grub`: QEMU starts halted with a gdb stub; `tools/gdbinit`
  connects and breaks at `kmain`. Use `set architecture i8086` and `b *0x7c00` to step
  through the boot sectors.
* All console output and `klog()` messages go to COM1 (`-serial stdio`).
* For early crashes: `qemu-system-i386 … -d int,cpu_reset -no-reboot -D qemu.log`.
