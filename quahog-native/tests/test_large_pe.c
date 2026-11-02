/* Synthetic sparse files exercise real mapped IO without storing game executables. */
#define _POSIX_C_SOURCE 200809L
#include "qcore.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
static void le16(unsigned char *p, unsigned n) {
    p[0] = (unsigned char)n;
    p[1] = (unsigned char)(n >> 8);
}
static void le32(unsigned char *p, uint32_t n) {
    le16(p, n & 65535);
    le16(p + 2, n >> 16);
}
static void fixture(const char *path, uint64_t size, uint32_t symbols) {
    unsigned char header[512] = {0}, table[256] = {0};
    memcpy(header, "MZ", 2);
    le32(header + 60, 128);
    memcpy(header + 128, "PE", 2);
    le16(header + 132, 0x8664);
    le16(header + 134, 2);
    le32(header + 140, symbols);
    le32(header + 144, 3);
    le16(header + 148, 240);
    le32(header + 208, 0x5000);
    for (unsigned i = 0; i < 2; i++) {
        unsigned at = 392 + i * 40;
        le32(header + at + 8, 0x1000);
        le32(header + at + 12, i ? 0x3000 : 0x1000);
        le32(header + at + 16, 0x1000);
        le32(header + at + 36, i ? 0xc0000040 : 0x60000020);
    }
    const char *names[] = {"port_dvd_read_file", "port_dvd_override", "port_mem1_size"};
    unsigned offset = 4;
    for (unsigned i = 0; i < 3; i++) {
        unsigned at = i * 18;
        le32(table + at + 4, offset);
        le32(table + at + 8, 32 + i * 32);
        le16(table + at + 12, i == 2 ? 2 : 1);
        table[at + 16] = 2;
        strcpy((char *)table + 54 + offset, names[i]);
        offset += (unsigned)strlen(names[i]) + 1;
    }
    le32(table + 54, offset);
    FILE *f = qopen(path, "wb");
    assert(f);
    assert(!ftruncate(fileno(f), (off_t)size));
    assert(fwrite(header, 1, sizeof header, f) == sizeof header);
    assert(!fseeko(f, (off_t)symbols, SEEK_SET));
    assert(fwrite(table, 1, 54 + offset, f) == 54 + offset);
    assert(!fclose(f));
}
static void verify(const char *path, uint64_t size, int merge) {
    QCtx *c = calloc(1, sizeof *c), *parent = calloc(1, sizeof *parent);
    assert(c && parent);
    if (setjmp(c->failure)) {
        fprintf(stderr, "Unexpected map failure: %s\n", c->error);
        abort();
    }
    if (setjmp(parent->failure)) {
        fprintf(stderr, "Unexpected parent failure: %s\n", parent->error);
        abort();
    }
    QBytes b = qmap_readonly(c, path);
    assert(b.n == size && c->allocated < 4096 && c->maps);
    if (merge) {
        assert(qmerge(parent, c));
        qclear(c);
        assert(!c->maps && parent->maps);
    }
    QCtx *owner = merge ? parent : c;
    assert(qpe_symbol(owner, b, "port_dvd_read_file", 1) == 0x1020);
    assert(qpe_symbol(owner, b, "port_dvd_override", 1) == 0x1040);
    assert(qpe_symbol(owner, b, "port_mem1_size", 0) == 0x3060);
    assert(qpe_symbol(owner, b, "not_present", 1) == 0);
    qclear(c);
    qclear(parent);
    assert(!c->maps && !parent->maps);
    free(c);
    free(parent);
}
static void expected_error(const char *path, int map) {
    QCtx *c = calloc(1, sizeof *c);
    assert(c);
    int error = setjmp(c->failure);
    if (!error) {
        QBytes b = map ? qmap_readonly(c, path) : qread(c, path, Q_LIMIT, 0);
        if (map)
            qpe_symbol(c, b, "port_dvd_read_file", 1);
    }
    assert(error);
    qclear(c);
    assert(!c->maps && !c->alloc);
    free(c);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    char path[Q_PATH];
    assert(snprintf(path, sizeof path, "%s/large-pe-fixture.bin", argv[1]) < (int)sizeof path);
    const uint64_t sizes[] = {4096, 300ull * 1024 * 1024 + 4096, 3ull * 1024 * 1024 * 1024 + 4096};
    const uint32_t offsets[] = {600, 300u * 1024 * 1024, 3u * 1024u * 1024u * 1024u};
    for (size_t i = 0; i < 3; i++) {
        fixture(path, sizes[i], offsets[i]);
        if (i)
            expected_error(path, 0);
        verify(path, sizes[i], 0);
        verify(path, sizes[i], 1);
        printf("Mapped PE passed: %llu bytes, COFF table at %u, under 4 KiB heap.\n",
               (unsigned long long)sizes[i], offsets[i]);
    }
    /* Real map handles must also be reclaimed after parsing or opening errors. */
    FILE *f = qopen(path, "r+b");
    assert(f);
    fputc('X', f);
    fclose(f);
    for (int i = 0; i < 32; i++)
        expected_error(path, 1);
    assert(!unlink(path));
    expected_error(path, 1);
    puts("Large-executable checks passed: >256 MiB, >2 GiB, far symbol offsets, arena transfer and "
         "error cleanup.");
    return 0;
}
