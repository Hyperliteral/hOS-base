#include <errno.h>
#include <stddef.h>
#include <termios.h>

// Baud index from a speed_t, inverse of the baud_value table in
// cfgetspeed.c. Unknown speeds fail with EINVAL like musl does.
static int baud_index(speed_t speed) {
  static const speed_t speeds[] = {
    0,     50,    75,    110,   134,   150,   200,   300,
    600,   1200,  1800,  2400,  4800,  9600,  19200, 38400,
    57600, 115200, 230400, 460800, 500000, 576000, 921600, 1000000,
    1152000, 1500000, 2000000, 2500000, 3000000, 3500000, 4000000,
  };
  for (size_t i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
    if (speeds[i] == speed)
      return (int)i;
  }
  return -1;
}

int cfsetospeed(struct termios *term, speed_t speed) {
  int index = baud_index(speed);
  if (index < 0) {
    *__errno_location() = EINVAL;
    return -1;
  }
  unsigned encoded =
      index < 16 ? (unsigned)index : CBAUDEX | (unsigned)(index - 16);
  term->c_cflag = (term->c_cflag & ~CBAUD) | encoded;
  return 0;
}

int cfsetispeed(struct termios *term, speed_t speed) {
  // See cfgetspeed.c: input follows output speed.
  return cfsetospeed(term, speed);
}
