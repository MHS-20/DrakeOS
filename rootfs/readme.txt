Welcome to DrakeOS.

Files in / come from the initrd, built into the kernel image.
Files in /disk live on the DrakeFS data disk (run 'format' once on a new disk).

Try:
  run hello a b c       a user-mode program with arguments
  run ticker 5 &        a background process; then 'ps'
  run spin &           twice: preemptive multitasking without any yield
  run fault             memory protection (also: run fault cli / run fault null)
  run greet             keyboard input through read(0)
  run ucat /readme.txt  file I/O through system calls
  write note hello      then: cat disk/note
  gfx, logo, paint      VGA mode 13h, the logo, mouse painting
  beep 440 300, play    the PC speaker
