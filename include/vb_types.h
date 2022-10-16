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

// TODO: Ideally we shouldn't need this
#ifndef __3DS__
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;

typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef u32 Handle;
typedef s32 Result;

#define linearAlloc malloc
#define linearFree free
#define RGB565(r,g,b)  (((b)&0x1f)|(((g)&0x3f)<<5)|(((r)&0x1f)<<11))
#define GFX_LEFT 0
#define GFX_RIGHT 1
#endif

#define dprintf(level, ...) \
    do { if (level <= DEBUGLEVEL) fprintf(stderr, __VA_ARGS__); } while (0)

#endif // Header Include
