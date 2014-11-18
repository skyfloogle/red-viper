#ifndef ROM_DB_H_
#define ROM_DB_H_

struct ROM_INFO;

typedef struct ROM_INFO
{
    char title[32];
    unsigned long crc32;
    char status[32];
} ROM_INFO;

extern ROM_INFO rom_db[];

int db_find(unsigned long crc32);
void gen_table(void);
unsigned long get_crc(int romsize);   

#endif
