/* Authored fixtures only. Does not need a game, ISO, Python or third-party package. */
#include "qcore.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static int rejected(QBytes b, int kind) {
    QCtx *c = calloc(1, sizeof *c);
    int bad = setjmp(c->failure);
    if (!bad) {
        if (kind == 0)
            qparse(c, b);
        else if (kind == 1)
            qunpack(c, b);
        else if (kind == 2)
            qyaz(c, b);
        else if (kind == 3)
            qstages(c, b);
        else
            qpe_symbol(c, b, "port_dvd_read_file", 1);
    }
    qclear(c);
    free(c);
    return bad != 0;
}
static QBytes str(const char *s) { return (QBytes){(unsigned char *)s, strlen(s)}; }
static void put_le16(unsigned char *p, unsigned n) {
    p[0] = (unsigned char)n;
    p[1] = (unsigned char)(n >> 8);
}
static void put_le32(unsigned char *p, uint32_t n) {
    put_le16(p, n & 65535);
    put_le16(p + 2, n >> 16);
}
static QBytes pe_fixture(QCtx *c) {
    QBytes b = {qa(c, 2048), 2048};
    unsigned char *p = b.p;
    memcpy(p, "MZ", 2);
    put_le32(p + 60, 128);
    memcpy(p + 128, "PE", 2);
    put_le16(p + 132, 0x8664);
    put_le16(p + 134, 2);
    put_le32(p + 140, 600);
    put_le32(p + 144, 3);
    put_le16(p + 148, 240);
    put_le32(p + 208, 0x5000);
    for (int i = 0; i < 2; i++) {
        unsigned at = 392 + (unsigned)i * 40;
        put_le32(p + at + 8, 0x1000);
        put_le32(p + at + 12, i ? 0x3000 : 0x1000);
        put_le32(p + at + 16, 0x1000);
        put_le32(p + at + 36, i ? 0xc0000040 : 0x60000020);
    }
    const char *names[] = {"port_dvd_read_file", "port_dvd_override", "port_mem1_size"};
    unsigned offset = 4;
    for (int i = 0; i < 3; i++) {
        unsigned at = 600 + (unsigned)i * 18;
        put_le32(p + at + 4, offset);
        put_le32(p + at + 8, 32 + (unsigned)i * 32);
        put_le16(p + at + 12, i == 2 ? 2 : 1);
        p[at + 16] = 2;
        strcpy((char *)p + 654 + offset, names[i]);
        offset += (unsigned)strlen(names[i]) + 1;
    }
    put_le32(p + 654, offset);
    return b;
}
static void memory_tests(QCtx *c) {
    QBytes out;
    assert(qmemory_text(
        c, str("FPS = 120\r\nSMS_MEM_MB = 64\r\nOther = yes\r\nSMS_MEM_MB = 32\r\n"), &out));
    const char *expected = "FPS = 120\r\nSMS_MEM_MB = 1024\r\nOther = yes\r\n";
    assert(out.n == strlen(expected) && !memcmp(out.p, expected, out.n));
    assert(!qmemory_text(c, str("SMS_MEM_MB = 1024\nOther = yes\n"), &out));
    assert(qmemory_text(c, str("FPS = 120"), &out));
    expected = "FPS = 120\nSMS_MEM_MB = 1024\n";
    assert(out.n == strlen(expected) && !memcmp(out.p, expected, out.n));
    assert(qmemory_text(c, str("\xef\xbb\xbfSMS_MEM_MB = 1024\n"), &out));
    expected = "\xef\xbb\xbf\nSMS_MEM_MB = 1024\n";
    assert(out.n == strlen(expected) && !memcmp(out.p, expected, out.n));
}
int main(int argc, char **argv) {
    assert(argc == 3);
    QCtx *c = calloc(1, sizeof *c);
    if (setjmp(c->failure)) {
        fprintf(stderr, "FAILED: %s\n", c->error);
        qclear(c);
        free(c);
        return 1;
    }
    char hash[65];
    qsha(str(""), hash);
    assert(!strcmp(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    qsha(str("abc"), hash);
    assert(!strcmp(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    qsha(str("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), hash);
    assert(!strcmp(hash, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    assert(qfnv(str("")) == UINT64_C(14695981039346656037));
    assert(qsafe("maps/quahog.arc") && !qsafe("../x") && !qsafe("a//x") && !qsafe("/a") &&
           !qsafe("a/../x") && !qsafe("C:x") && !qsafe("a\\x"));
    memory_tests(c);
    QBytes pe = pe_fixture(c);
    assert(qpe_symbol(c, pe, "port_dvd_read_file", 1) == 0x1020);
    assert(qpe_symbol(c, pe, "port_dvd_override", 1) == 0x1040);
    assert(qpe_symbol(c, pe, "port_mem1_size", 0) == 0x3060);
    assert(qpe_symbol(c, pe, "absent", 1) == 0);
    QBytes scene = qread(c, qjoin(c, argv[1], "scene.bin"), Q_LIMIT, 0);
    QBytes encoded = qencode(c, qparse(c, scene));
    assert(encoded.n == scene.n && !memcmp(encoded.p, scene.p, scene.n));
    QBytes raw = qread(c, qjoin(c, argv[1], "scaffold.arc"), Q_LIMIT, 0);
    QArchive a = qunpack(c, raw);
    QBytes packed = qpack(c, &a);
    assert(packed.n == raw.n && !memcmp(packed.p, raw.p, raw.n));
    QBytes padded = {qa(c, raw.n + 32), raw.n + 32};
    memcpy(padded.p, raw.p, raw.n);
    QArchive padded_arc = qunpack(c, padded);
    assert(padded_arc.count == a.count);
    const char *sjis_names[] = {"scene/message/\x95\x5c.bin", "scene/message/\x83\x41.bin"};
    for (size_t i = 0; i < 2; i++)
        qsetfile(c, &padded_arc, sjis_names[i], str("authored SJIS resource"));
    QArchive sjis_arc = qunpack(c, qpack(c, &padded_arc));
    for (size_t i = 0; i < 2; i++) {
        QFile *f = qunique(c, &sjis_arc, sjis_names[i]);
        assert(f->data.n == strlen("authored SJIS resource"));
    }
    QBytes compressed = qread(c, qjoin(c, argv[1], "game/data/scene/dolpic_ex0.szs"), Q_LIMIT, 0),
           decoded = qyaz(c, compressed);
    assert(decoded.n == raw.n && !memcmp(decoded.p, raw.p, raw.n));
    QBytes wrapped = qread(c, qjoin(c, argv[1], "game/data/stagearc.bin"), Q_LIMIT, 0),
           direct = qread(c, qjoin(c, argv[1], "direct-stage.bin"), Q_LIMIT, 0);
    QStages s = qstages(c, wrapped), d = qstages(c, direct);
    assert(s.area == 23 && s.episode == 1 && s.count == 4 && d.area == 23 && d.episode == 1 &&
           d.count == 4);
    QLocation p = qmarket(c, raw);
    assert(p.valid && fabsf(p.x) < .001f && fabsf(p.y) < .001f && fabsf(p.z - 425) < .001f &&
           fabsf(p.yaw - 180) < .001f);
    QBytes expected = qread(c, qjoin(c, argv[1], "expected.sha256"), 128, 0);
    QBytes built = qbuild(c, compressed, argv[2], (QBytes){0});
    qsha(built, hash);
    assert(expected.n == 65 && !memcmp(expected.p, hash, 64));
    printf("C archive matches original assembler byte-for-byte: %s\n", hash);
    unsigned char yaz[21] = {'Y', 'a', 'z', '0', 0, 0,    0,   20, 0, 0, 0,
                             0,   0,   0,   0,   0, 0x80, 'A', 0,  0, 1};
    QBytes run = qyaz(c, (QBytes){yaz, sizeof yaz});
    assert(run.n == 20);
    for (size_t i = 0; i < run.n; i++)
        assert(run.p[i] == 'A');
    yaz[17] = 0;
    yaz[16] = 0;
    assert(rejected((QBytes){yaz, sizeof yaz}, 2));
    assert(rejected((QBytes){scene.p, scene.n - 1}, 0));
    assert(rejected((QBytes){raw.p, 63}, 1));
    assert(rejected((QBytes){compressed.p, compressed.n - 1}, 2));
    unsigned char bad[64] = {0};
    assert(rejected((QBytes){bad, sizeof bad}, 4));
    /* Parser mutation checks with ASan/UBSan: malformed framing, offsets, recursion. */
    unsigned seed = 0x12345678;
    QBytes inputs[] = {scene, raw, compressed, wrapped};
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < 200; j++) {
            QBytes in = inputs[k];
            unsigned char *copy = malloc(in.n);
            memcpy(copy, in.p, in.n);
            seed = seed * 1664525u + 1013904223u;
            size_t at = seed % in.n;
            seed = seed * 1664525u + 1013904223u;
            copy[at] ^= (unsigned char)((seed >> 24) | 1);
            (void)rejected((QBytes){copy, in.n}, k == 3 ? 3 : k);
            free(copy);
        }
    qclear(c);
    free(c);
    puts("Core checks passed: hashes, settings, scene framing, stage indices, Yaz0, RARC, market "
         "geometry, 800 mutated inputs.");
    return 0;
}
