#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

uint16_t read_le16(const unsigned char *p);
uint32_t read_le32(const unsigned char *p);
uint64_t read_le64(const unsigned char *p);
void write_le16(unsigned char *p, uint16_t v);
void write_le32(unsigned char *p, uint32_t v);

int str_to_u32(const char *s, uint32_t *out);
int str_to_u16(const char *s, uint16_t *out);
void format_unix_time(uint32_t timestamp, char *out, size_t out_size);

#endif
