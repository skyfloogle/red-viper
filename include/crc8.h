#include <3ds.h>


u8 crc8(const void *buf, size_t len);
u8 crc8_loop(const void *container_start, size_t container_size, const void *segment, size_t segment_size);
