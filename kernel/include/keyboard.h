/* PS/2 keyboard (scan code set 1) with a ring buffer of decoded characters. */
#ifndef DRAKE_KEYBOARD_H
#define DRAKE_KEYBOARD_H

#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DELETE 0x86

void keyboard_init(void);
int kbd_getchar(void);         /* blocks the calling process until a key arrives */
int kbd_trygetchar(void);      /* -1 when the buffer is empty */

#endif
