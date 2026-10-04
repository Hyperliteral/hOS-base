#include <termios.h>

// Baud rates: CBAUD carries values 0-15 directly; values 16+ set CBAUDEX
// with the remainder in the low bits (B57600 == CBAUDEX|01, etc.).
static speed_t baud_value(unsigned cflag) {
  static const speed_t table[] = {
    0,     50,    75,    110,   134,   150,   200,   300,
    600,   1200,  1800,  2400,  4800,  9600,  19200, 38400,
    57600, 115200, 230400, 460800, 500000, 576000, 921600, 1000000,
    1152000, 1500000, 2000000, 2500000, 3000000, 3500000, 4000000,
  };
  unsigned index = cflag & 017;
  if (cflag & CBAUDEX)
    index += 16;
  if (index >= sizeof(table) / sizeof(table[0]))
    return 38400;
  return table[index];
}

speed_t cfgetospeed(const struct termios *term) {
  return baud_value(term->c_cflag);
}

speed_t cfgetispeed(const struct termios *term) {
  // No separate input-speed field is tracked (musl stores it in CIBAUD
  // bits this driver does not interpret); input follows output speed.
  return cfgetospeed(term);
}
