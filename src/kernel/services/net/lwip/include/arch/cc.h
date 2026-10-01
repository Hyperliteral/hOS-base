// a bunch of messy includes

#include "linux.h"
#include "malloc.h"
#include <string.h>
#include <stdint.h>
#include "system.h"
#include "task.h"
#include "types.h"
#include "util.h"

#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_LIMITS_H 1
#define LWIP_NO_CTYPE_H 1

#define X8_F  "x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "zu"