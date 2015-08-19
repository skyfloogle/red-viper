#ifndef VB_TYPES_H_
#define VB_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

#define WORD  uint32_t    //Full 32bit
#define HWORD uint16_t   //16bit
#define BYTE  uint8_t    //8bit

#define VB_INLINE inline //Let Allegro handle the INLINE statements

#define INT64 int64_t //64 bit int
#define INT64U uint64_t //64 bit Unsigned

#define dprintf(level, fmt, ...) \
    do { if (level <= DEBUGLEVEL) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#endif // Header Include
