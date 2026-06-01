#include "util.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

uint16_t read_le16(const unsigned char *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

uint64_t read_le64(const unsigned char *p) {
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

void write_le16(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

void write_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

int str_to_u32(const char *s, uint32_t *out) {
    char *end = NULL;
    unsigned long v;

    if (!s || !*s || !out) {
        return -1;
    }

    errno = 0;
    v = strtoul(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0' || v > 0xffffffffUL) {
        return -1;
    }

    *out = (uint32_t)v;
    return 0;
}

int str_to_u16(const char *s, uint16_t *out) {
    uint32_t v = 0;

    if (str_to_u32(s, &v) != 0 || v > 0xffffU) {
        return -1;
    }

    *out = (uint16_t)v;
    return 0;
}

void format_unix_time(uint32_t timestamp, char *out, size_t out_size) {
    time_t raw_time;
    struct tm tm_value;

    if (!out || out_size == 0) {
        return;
    }

    raw_time = (time_t)timestamp;
    if (localtime_r(&raw_time, &tm_value) == NULL) {
        snprintf(out, out_size, "%u", timestamp);
        return;
    }

    if (strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &tm_value) == 0) {
        snprintf(out, out_size, "%u", timestamp);
    }
}
