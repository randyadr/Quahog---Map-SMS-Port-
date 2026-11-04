#include "qcore.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void le32(unsigned char *p, uint32_t n) {
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(n >> (8 * i));
}
static void le64(unsigned char *p, uint64_t n) {
    le32(p, (uint32_t)n); le32(p + 4, (uint32_t)(n >> 32));
}
static void word(unsigned char *p, uint32_t n, int be) {
    if (be) qput32(p, n); else le32(p, n);
}
static void wide(unsigned char *p, uint64_t n, int be) {
    if (be) { qput32(p, (uint32_t)(n >> 32)); qput32(p + 4, (uint32_t)n); }
    else le64(p, n);
}
static void fixture(unsigned char *b, uint32_t cpu) {
    memset(b, 0, 2048);
    le32(b, 0xfeedfacf); le32(b + 4, cpu); le32(b + 12, 2);
    le32(b + 16, 4); le32(b + 20, 152 * 2 + 24 + 24);
    for (int i = 0; i < 2; i++) {
        unsigned at = 32 + i * 152;
        uint64_t va = UINT64_C(0x100000000) + i * 0x4000;
        le32(b + at, 0x19); le32(b + at + 4, 152);
        le64(b + at + 24, va); le64(b + at + 32, 0x4000);
        le32(b + at + 60, i ? 3 : 5); le32(b + at + 64, 1);
        le64(b + at + 72 + 32, va + 0x1000); le64(b + at + 72 + 40, 0x1000);
    }
    le32(b + 336, 2); le32(b + 340, 24);
    le32(b + 344, 512); le32(b + 348, 3);
    le32(b + 352, 600); le32(b + 356, 256);
    le32(b + 360, 0x1b); le32(b + 364, 24); /* LC_UUID */
    const char *names[] = {"_port_dvd_read_file", "_port_dvd_override", "_port_mem1_size"};
    unsigned off = 1;
    for (int i = 0; i < 3; i++) {
        unsigned at = 512 + i * 16;
        le32(b + at, off); b[at + 4] = 0x0f; b[at + 5] = i == 2 ? 2 : 1;
        le64(b + at + 8, UINT64_C(0x100001000) + (i == 2 ? 0x4000 : (unsigned)i * 16));
        strcpy((char *)b + 600 + off, names[i]); off += (unsigned)strlen(names[i]) + 1;
    }
}
static uint64_t lookup(unsigned char *b, size_t n, uint32_t cpu, const char *name, int code,
                       int *failed) {
    QCtx *c = calloc(1, sizeof *c); assert(c);
    volatile uint64_t result = 0;
    *failed = setjmp(c->failure);
    if (!*failed) result = qmacho_symbol(c, (QBytes){b, n}, cpu, name, code);
    qclear(c); free(c); return result;
}
static void verify(unsigned char *b, size_t n, uint32_t cpu) {
    int error;
    assert(lookup(b, n, cpu, "port_dvd_read_file", 1, &error) == UINT64_C(0x100001000) && !error);
    assert(lookup(b, n, cpu, "port_dvd_override", 1, &error) == UINT64_C(0x100001010) && !error);
    assert(lookup(b, n, cpu, "port_mem1_size", 0, &error) == UINT64_C(0x100005000) && !error);
    assert(lookup(b, n, cpu, "missing", 1, &error) == 0 && !error);
    lookup(b, n, cpu, "port_mem1_size", 1, &error); assert(error);
}
int main(void) {
    unsigned char b[2048], fat[8192], changed[2048];
    const uint32_t cpus[] = {0x01000007, 0x0100000c};
    for (unsigned arch = 0; arch < 2; arch++) {
        fixture(b, cpus[arch]); verify(b, sizeof b, cpus[arch]);
        int error;
        lookup(b, sizeof b, cpus[1 - arch], "port_dvd_read_file", 1, &error); assert(error);
        for (size_t n = 0; n < 856; n++) {
            lookup(b, n, cpus[arch], "port_dvd_read_file", 1, &error); assert(error);
        }
        const unsigned bad_at[] = {16, 20, 36, 96, 344, 348, 352, 356, 512};
        for (size_t k = 0; k < sizeof bad_at / sizeof *bad_at; k++) {
            memcpy(changed, b, sizeof b); le32(changed + bad_at[k], UINT32_MAX);
            lookup(changed, sizeof changed, cpus[arch], "port_dvd_read_file", 1, &error);
            assert(error);
        }
        memcpy(changed, b, sizeof b); changed[517] = 3;
        lookup(changed, sizeof changed, cpus[arch], "port_dvd_read_file", 1, &error); assert(error);
        memcpy(changed, b, sizeof b); memset(changed + 600, 'X', 256);
        lookup(changed, sizeof changed, cpus[arch], "port_dvd_read_file", 1, &error); assert(error);
        memcpy(changed, b, sizeof b); le64(changed + 520, UINT64_C(0x100004000));
        lookup(changed, sizeof changed, cpus[arch], "port_dvd_read_file", 1, &error); assert(error);
        for (unsigned i = 0; i < 1600; i++) {
            memcpy(changed, b, sizeof b);
            changed[(i * 37u) % sizeof b] ^= (unsigned char)(1u << (i % 8));
            lookup(changed, sizeof changed, cpus[arch], "port_dvd_read_file", 1, &error);
        }
    }
    for (int f64 = 0; f64 < 2; f64++) for (int be = 0; be < 2; be++) {
        memset(fat, 0, sizeof fat);
        word(fat, f64 ? 0xcafebabf : 0xcafebabe, be); word(fat + 4, 2, be);
        for (int i = 0; i < 2; i++) {
            unsigned at = 8 + i * (f64 ? 32 : 20), offset = 2048 + i * 2048;
            word(fat + at, cpus[i], be);
            if (f64) { wide(fat + at + 8, offset, be); wide(fat + at + 16, 2048, be); }
            else { word(fat + at + 8, offset, be); word(fat + at + 12, 2048, be); }
            fixture(fat + offset, cpus[i]);
        }
        for (int i = 0; i < 2; i++) verify(fat, sizeof fat, cpus[i]);
        int error;
        word(fat + 8, cpus[1], be);
        lookup(fat, sizeof fat, cpus[1], "port_dvd_read_file", 1, &error); assert(error);
        word(fat + 4, UINT32_MAX, be);
        lookup(fat, sizeof fat, cpus[0], "port_dvd_read_file", 1, &error); assert(error);
    }
    puts("Mach-O: Intel/ARM64, universal 32/64-bit tables, both byte orders, bounds, permissions, "
         "missing symbols, truncations and 3200 mutations passed.");
    return 0;
}
