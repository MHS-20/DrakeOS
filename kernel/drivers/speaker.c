/* PC speaker: PIT channel 2 generates a square wave, port 0x61 gates it to the speaker. */
#include <arch.h>
#include <cpu.h>
#include <drivers.h>
#include <proc.h>

void speaker_tone(uint32_t hz)
{
    if (hz < 20 || hz > 20000) {
        speaker_off();
        return;
    }
    uint32_t div = 1193182 / hz;
    outb(0x43, 0xB6);                 /* channel 2, lo/hi byte, square wave, binary */
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    uint8_t gate = inb(0x61);
    if ((gate & 3) != 3)
        outb(0x61, gate | 3);         /* timer 2 gate + speaker data enable */
}

void speaker_off(void)
{
    outb(0x61, inb(0x61) & 0xFC);
}

void speaker_beep(uint32_t hz, uint32_t ms)
{
    speaker_tone(hz);
    process_sleep(ms);
    speaker_off();
}
