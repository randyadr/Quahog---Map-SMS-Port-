#include "qcore.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <wchar.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
struct QMapped {
    struct QMapped *next;
    QBytes bytes;
#ifdef _WIN32
    HANDLE file, mapping;
#else
    int file;
#endif
};
struct QAlloc {
    struct QAlloc *next;
    max_align_t align;
};
_Noreturn void qfail(QCtx *c, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vsnprintf(c->error, sizeof c->error, fmt, a);
    va_end(a);
    longjmp(c->failure, 1);
}
void *qa(QCtx *c, size_t n) {
    if (n > Q_LIMIT || c->allocated > 768u * 1024u * 1024u - n)
        qfail(c, "Preparation memory limit exceeded");
    QAlloc *a = calloc(1, sizeof(*a) + (n ? n : 1));
    if (!a)
        qfail(c, "Out of memory preparing Quahog");
    a->next = c->alloc;
    c->alloc = a;
    c->allocated += n;
    return a + 1;
}
void qclear(QCtx *c) {
    /* Mappings may outlive a failed parse, but never the owning startup context. */
    while (c->maps) {
        QMapped *view = c->maps;
        c->maps = view->next;
#ifdef _WIN32
        if (view->bytes.p)
            UnmapViewOfFile(view->bytes.p);
        if (view->mapping)
            CloseHandle(view->mapping);
        if (view->file != INVALID_HANDLE_VALUE)
            CloseHandle(view->file);
#else
        if (view->bytes.p)
            munmap(view->bytes.p, view->bytes.n);
        if (view->file >= 0)
            close(view->file);
#endif
    }
    while (c->alloc) {
        QAlloc *a = c->alloc;
        c->alloc = a->next;
        free(a);
    }
    c->allocated = 0;
}
/* Move a successful child arena without duplicating a large cached map. */
int qmerge(QCtx *to, QCtx *from) {
    if (from->allocated > 768u * 1024u * 1024u - to->allocated)
        return 0;
    if (from->maps) {
        QMapped *last = from->maps;
        while (last->next)
            last = last->next;
        last->next = to->maps;
        to->maps = from->maps;
        from->maps = NULL;
    }
    if (from->alloc) {
        QAlloc *last = from->alloc;
        while (last->next)
            last = last->next;
        last->next = to->alloc;
        to->alloc = from->alloc;
        to->allocated += from->allocated;
        from->alloc = NULL;
        from->allocated = 0;
    }
    return 1;
}
char *qs(QCtx *c, const void *p, size_t n) {
    char *s = qa(c, n + 1);
    if (n)
        memcpy(s, p, n);
    return s;
}
char *qfmt(QCtx *c, const char *fmt, ...) {
    if (!fmt)
        qfail(c, "Missing format string");
    va_list a, b;
    va_start(a, fmt);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, fmt, a);
    va_end(a);
    if (n < 0 || n > 65536)
        qfail(c, "Invalid text size");
    char *s = qa(c, (size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, b);
    va_end(b);
    return s;
}
int qends(const char *s, const char *end) {
    size_t a = strlen(s), b = strlen(end);
    return a >= b && !memcmp(s + a - b, end, b);
}
int qsafe(const char *s) {
    if (!s || !*s || *s == '/' || strchr(s, '\\') || strchr(s, ':'))
        return 0;
    const char *p = s;
    for (;;) {
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (!n || (n == 1 && *p == '.') || (n == 2 && p[0] == '.' && p[1] == '.'))
            return 0;
        if (!e)
            return 1;
        p = e + 1;
    }
}
uint16_t qbe16(const unsigned char *p) { return (uint16_t)((unsigned)p[0] * 256 + p[1]); }
uint32_t qbe32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
void qput16(unsigned char *p, uint16_t n) {
    p[0] = (unsigned char)(n >> 8);
    p[1] = (unsigned char)n;
}
void qput32(unsigned char *p, uint32_t n) {
    p[0] = (unsigned char)(n >> 24);
    p[1] = (unsigned char)(n >> 16);
    p[2] = (unsigned char)(n >> 8);
    p[3] = (unsigned char)n;
}
uint64_t qfnv(QBytes b) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < b.n; i++)
        h = (h ^ b.p[i]) * UINT64_C(1099511628211);
    return h;
}
void qb_add(QBuf *b, const void *p, size_t n) {
    if (n > Q_LIMIT - b->n)
        qfail(b->c, "Output too large");
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n) {
            if (cap > Q_LIMIT / 2) {
                cap = Q_LIMIT;
                break;
            }
            cap *= 2;
        }
        unsigned char *v = qa(b->c, cap);
        if (b->n)
            memcpy(v, b->p, b->n);
        b->p = v;
        b->cap = cap;
    }
    if (n && p)
        memcpy(b->p + b->n, p, n);
    else if (n)
        memset(b->p + b->n, 0, n);
    b->n += n;
}
void qb_u32(QBuf *b, uint32_t n) {
    unsigned char p[4];
    qput32(p, n);
    qb_add(b, p, 4);
}
void qb_str(QBuf *b, const void *p, size_t n) {
    if (n > 65535)
        qfail(b->c, "Scene string too long");
    unsigned char z[2];
    qput16(z, (uint16_t)n);
    qb_add(b, z, 2);
    qb_add(b, p, n);
}
QBytes qb_bytes(QBuf *b) { return (QBytes){b->p, b->n}; }
/* SHA-256, FIPS 180-4. Used for shipped assets and persisted prepared archives. */
static uint32_t rr(uint32_t x, int n) { return x >> n | x << (32 - n); }
static void sha_block(uint32_t h[8], const unsigned char p[64]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = qbe32(p + i * 4);
    for (int i = 16; i < 64; i++)
        w[i] = w[i - 16] + (rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
               (rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^ (w[i - 2] >> 10));
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], z = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t = z + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t u = (rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        z = g;
        g = f;
        f = e;
        e = d + t;
        d = c;
        c = b;
        b = a;
        a = t + u;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += z;
}
void qsha(QBytes b, char out[65]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t n = b.n;
    const unsigned char *p = b.p;
    while (n >= 64) {
        sha_block(h, p);
        p += 64;
        n -= 64;
    }
    unsigned char tail[128] = {0};
    if (n)
        memcpy(tail, p, n);
    tail[n] = 128;
    size_t size = n < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)b.n * 8;
    for (int i = 0; i < 8; i++)
        tail[size - 1 - i] = (unsigned char)(bits >> (i * 8));
    sha_block(h, tail);
    if (size == 128)
        sha_block(h, tail + 64);
    for (int i = 0; i < 8; i++)
        snprintf(out + i * 8, 9, "%08x", h[i]);
}
#ifdef _WIN32
static int wide_path(const char *s, wchar_t *w, int cap) {
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, cap);
}
FILE *qopen(const char *path, const char *mode) {
    wchar_t p[32768], m[16];
    if (!wide_path(path, p, 32768) || !wide_path(mode, m, 16))
        return NULL;
    return _wfopen(p, m);
}
int qexists(const char *p) {
    wchar_t w[32768];
    return wide_path(p, w, 32768) && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}
void qremove(const char *p) {
    wchar_t w[32768];
    if (wide_path(p, w, 32768))
        DeleteFileW(w);
}
#else
FILE *qopen(const char *p, const char *m) { return fopen(p, m); }
int qexists(const char *p) {
    struct stat s;
    return stat(p, &s) == 0;
}
void qremove(const char *p) { remove(p); }
#endif
QBytes qmap_readonly(QCtx *c, const char *path) {
    /* PE debug/symbol data can make a rebuilt game EXE much larger than a map.
       Mapping does not allocate/copy the whole file into the preparation arena. */
    QMapped *view = qa(c, sizeof *view);
#ifdef _WIN32
    view->file = INVALID_HANDLE_VALUE;
#else
    view->file = -1;
#endif
    view->next = c->maps;
    c->maps = view;
#ifdef _WIN32
    wchar_t w[32768];
    if (!wide_path(path, w, 32768))
        qfail(c, "Invalid UTF-8 executable path");
    view->file = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (view->file == INVALID_HANDLE_VALUE)
        qfail(c, "Cannot open executable for read-only inspection (Windows error %lu): %s",
              (unsigned long)GetLastError(), path);
    LARGE_INTEGER length;
    if (!GetFileSizeEx(view->file, &length) || length.QuadPart <= 0 ||
        (uint64_t)length.QuadPart > SIZE_MAX)
        qfail(c, "Cannot determine executable size: %s", path);
    view->bytes.n = (size_t)length.QuadPart;
    view->mapping = CreateFileMappingW(view->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!view->mapping)
        qfail(c, "Cannot map executable metadata (Windows error %lu): %s",
              (unsigned long)GetLastError(), path);
    view->bytes.p = MapViewOfFile(view->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!view->bytes.p)
        qfail(c, "Cannot view executable metadata (Windows error %lu): %s",
              (unsigned long)GetLastError(), path);
#else
    view->file = open(path, O_RDONLY);
    if (view->file < 0)
        qfail(c, "Cannot open executable for read-only inspection: %s", path);
    struct stat status;
    if (fstat(view->file, &status) || status.st_size <= 0 || (uint64_t)status.st_size > SIZE_MAX)
        qfail(c, "Cannot determine executable size: %s", path);
    view->bytes.n = (size_t)status.st_size;
    void *data = mmap(NULL, view->bytes.n, PROT_READ, MAP_PRIVATE, view->file, 0);
    if (data == MAP_FAILED)
        qfail(c, "Cannot map executable metadata: %s", path);
    view->bytes.p = data;
#endif
    return view->bytes;
}

QBytes qread(QCtx *c, const char *path, size_t limit, int optional) {
    FILE *f = qopen(path, "rb");
    if (!f) {
        if (optional)
            return (QBytes){0};
        qfail(c, "Cannot open: %s", path);
    }
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        qfail(c, "Cannot seek: %s", path);
    }
    long n = ftell(f);
    if (n < 0 || (uint64_t)n > limit) {
        fclose(f);
        qfail(c, "Invalid file size: %s", path);
    }
    rewind(f); /* Allocate after closing/reopening so an allocation failure cannot leak a file
                  handle. */
    fclose(f);
    QBytes b = {qa(c, (size_t)n), (size_t)n};
    f = qopen(path, "rb");
    if (!f)
        qfail(c, "File disappeared: %s", path);
    int ok = fread(b.p, 1, b.n, f) == b.n;
    int extra = fgetc(f);
    int err = ferror(f);
    fclose(f);
    if (!ok || extra != EOF || err)
        qfail(c, "File changed or read failed: %s", path);
    return b;
}
char *qjoin(QCtx *c, const char *a, const char *b) { return qfmt(c, "%s/%s", a, b); }
void qmkdir(QCtx *c, const char *path) {
    char *p = qs(c, path, strlen(path));
    size_t n = strlen(p);
    size_t root_end = 0;
#ifdef _WIN32
    if (n >= 2 && p[1] == ':')
        root_end = 2;
    else if (n >= 2 && (p[0] == '/' || p[0] == '\\') && (p[1] == '/' || p[1] == '\\')) {
        /* UNC paths: the server and share must already exist. */
        size_t k = 2;
        for (int component = 0; component < 2 && k < n; component++) {
            while (k < n && p[k] != '/' && p[k] != '\\')
                k++;
            root_end = k;
            if (k < n)
                k++;
        }
    }
#endif
    for (size_t i = 1; i <= n; i++) {
        if (p[i] != '/' && p[i] != '\\' && p[i])
            continue;
        char saved = p[i];
        p[i] = 0;
        if (i <= root_end) {
            p[i] = saved;
            continue;
        }
#ifdef _WIN32
        wchar_t w[32768];
        if (!wide_path(p, w, 32768))
            qfail(c, "Invalid UTF-8 directory");
        if (!CreateDirectoryW(w, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            p[i] = saved;
            qfail(c, "Cannot create directory: %s", p);
        }
#else
        if (mkdir(p, 0700) && errno != EEXIST) {
            p[i] = saved;
            qfail(c, "Cannot create directory: %s", p);
        }
#endif
        p[i] = saved;
    }
}
void qwrite(QCtx *c, const char *p, QBytes b) {
    FILE *f = qopen(p, "wb");
    if (!f)
        qfail(c, "Cannot write: %s", p);
    int ok = fwrite(b.p, 1, b.n, f) == b.n;
    if (fflush(f))
        ok = 0;
    if (fclose(f))
        ok = 0;
    if (!ok)
        qfail(c, "Incomplete write: %s", p);
}
void qatomic(QCtx *c, const char *p, QBytes b) {
#ifdef _WIN32
    char *tmp = qfmt(c, "%s.%lu.tmp", p, (unsigned long)GetCurrentProcessId());
#else
    char *tmp = qfmt(c, "%s.%ld.tmp", p, (long)getpid());
#endif
    qwrite(c, tmp, b);
#ifdef _WIN32
    wchar_t from[32768], to[32768];
    if (!wide_path(tmp, from, 32768) || !wide_path(p, to, 32768) ||
        !MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        qremove(tmp);
        qfail(c, "Cannot commit prepared file: %s", p);
    }
#else
    if (rename(tmp, p)) {
        qremove(tmp);
        qfail(c, "Cannot commit prepared file: %s", p);
    }
#endif
}
QBytes qhost_read(QCtx *c, QHost h, const char *path, int optional) {
    size_t n = 0;
    void *raw = h.read(path, &n);
    if (!raw) {
        if (optional)
            return (QBytes){0};
        qfail(c, "Active game cannot read %s", path);
    }
    if (n > Q_LIMIT) {
        h.release(raw);
        qfail(c, "Game file exceeds size limit: %s", path);
    } /* Keep host allocations out of the DLL allocator. */
    QAlloc *a = calloc(1, sizeof(*a) + (n ? n : 1));
    if (!a) {
        h.release(raw);
        qfail(c, "Out of memory reading game archive");
    }
    if (c->allocated > 768u * 1024u * 1024u - n) {
        free(a);
        h.release(raw);
        qfail(c, "Preparation memory limit exceeded");
    }
    a->next = c->alloc;
    c->alloc = a;
    c->allocated += n;
    QBytes b = {(unsigned char *)(a + 1), n};
    if (n)
        memcpy(b.p, raw, n);
    h.release(raw);
    return b;
}
