#ifndef _WIN32
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#define _POSIX_C_SOURCE 200809L
#endif
#include "qcore.h"
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <wchar.h>
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
static void pneed(QCtx *c, QBytes b, size_t p, size_t n) {
    if (p > b.n || n > b.n - p)
        qfail(c, "Truncated PE/COFF metadata");
}
static uint16_t le16(QCtx *c, QBytes b, size_t p) {
    pneed(c, b, p, 2);
    return (uint16_t)(b.p[p] | (unsigned)b.p[p + 1] << 8);
}
static uint32_t le32(QCtx *c, QBytes b, size_t p) {
    return le16(c, b, p) | (uint32_t)le16(c, b, p + 2) << 16;
}
uint32_t qpe_symbol(QCtx *c, QBytes b, const char *wanted, int code) {
    if (b.n < 64 || le16(c, b, 0) != 0x5a4d)
        qfail(c, "Not a Windows executable");
    size_t pe = le32(c, b, 60);
    pneed(c, b, pe, 24);
    if (le32(c, b, pe) != 0x4550 || le16(c, b, pe + 4) != 0x8664)
        qfail(c, "Requires a Windows x64 game");
    size_t nsec = le16(c, b, pe + 6), table = le32(c, b, pe + 12), count = le32(c, b, pe + 16),
           optional = pe + 24, sections = optional + le16(c, b, pe + 20);
    if (!table || !count || count > 10000000)
        return 0;
    pneed(c, b, table, count * 18);
    size_t strings = table + count * 18;
    unsigned size = le32(c, b, strings);
    pneed(c, b, strings, size);
    pneed(c, b, sections, nsec * 40);
    for (size_t i = 0; i < count;) {
        size_t s = table + i * 18;
        const unsigned char *name = b.p + s;
        size_t len = 0;
        if (le32(c, b, s) == 0) {
            unsigned off = le32(c, b, s + 4);
            if (off >= 4 && off < size) {
                name = b.p + strings + off;
                const unsigned char *end = memchr(name, 0, size - off);
                if (!end)
                    qfail(c, "Unterminated COFF name");
                len = (size_t)(end - name);
            }
        } else {
            while (len < 8 && name[len])
                len++;
        }
        int sec = (int16_t)le16(c, b, s + 12);
        if (len == strlen(wanted) && !memcmp(name, wanted, len) && sec > 0 && (size_t)sec <= nsec) {
            size_t sh = sections + (size_t)(sec - 1) * 40;
            uint32_t flags = le32(c, b, sh + 36), vs = le32(c, b, sh + 8),
                     raw = le32(c, b, sh + 16), v = le32(c, b, s + 8), base = le32(c, b, sh + 12);
            if (code && !(flags & 0x20000000))
                qfail(c, "Native symbol is outside executable code: %s", wanted);
            if (v >= (vs > raw ? vs : raw) || base > UINT32_MAX - v ||
                base + v >= le32(c, b, optional + 56))
                qfail(c, "Native symbol exceeds its image: %s", wanted);
            return base + v;
        }
        size_t next = i + 1 + b.p[s + 17];
        if (next > count)
            qfail(c, "Invalid COFF auxiliary records");
        i = next;
    }
    return 0;
}
#ifdef _WIN32
static char *utf8(QCtx *c, const wchar_t *s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (!n)
        qfail(c, "Invalid Windows path");
    char *p = qa(c, (size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, p, n, NULL, NULL);
    return p;
}
static wchar_t *wide(QCtx *c, const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n)
        qfail(c, "Invalid UTF-8 path");
    wchar_t *p = qa(c, (size_t)n * sizeof *p);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n);
    return p;
}
static void *resolve(QCtx *c, QBytes b, unsigned char *image, const char *name, int code) {
    FARPROC p = code ? GetProcAddress((HMODULE)image, name) : NULL;
    if (p)
        return (void *)p;
    uint32_t rva = qpe_symbol(c, b, name, code);
    if (!rva)
        qfail(c,
              "Game lacks native symbol %s. Use an official unstripped Windows x64 launcher build.",
              name);
    MEMORY_BASIC_INFORMATION m;
    void *v = image + rva;
    if (!VirtualQuery(v, &m, sizeof m) || m.State != MEM_COMMIT || m.AllocationBase != image ||
        (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        qfail(c, "Game symbol is not mapped: %s", name);
    return v;
}
static int has_bytes(QBytes b, const char *s) {
    size_t n = strlen(s);
    for (size_t i = 0; i + n <= b.n; i++)
        if (!memcmp(b.p + i, s, n))
            return 1;
    return 0;
}
QHost qresolve(QCtx *c) {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, 32768);
    if (!n || n >= 32768)
        qfail(c, "Cannot identify game executable");
    QBytes b = qmap_readonly(c, utf8(c, path));
    unsigned char *image = (unsigned char *)GetModuleHandleW(NULL);
    typedef int (*External)(void);
    External external = (External)GetProcAddress((HMODULE)image, "SunshineSkinAuto_ExternalWorlds");
    if (!(external && external() == 1) &&
        (has_bytes(b, "wardrobe_v41.ini") || has_bytes(b, "[mandy]") ||
         has_bytes(b, "skinselector_quahog")))
        qfail(c, "This game has the old Skin Selector world hooks. Use a clean official game build "
                 "or the separate-world Skin Selector edition.");
    QHost h = {0};
    h.read = (void *(*)(const char *, size_t *))resolve(c, b, image, "port_dvd_read_file", 1);
    h.replace =
        (int (*)(const char *, const void *, uint32_t))resolve(c, b, image, "port_dvd_override", 1);
    h.memory_size = (uint32_t *)resolve(c, b, image, "port_mem1_size", 0);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    size_t image_size = nt->OptionalHeader.SizeOfImage;
    if (dir.VirtualAddress && dir.VirtualAddress < image_size &&
        dir.Size <= image_size - dir.VirtualAddress) {
        IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(image + dir.VirtualAddress);
        size_t max = dir.Size / sizeof *imp;
        for (size_t j = 0; j < max && imp[j].Name && !h.release; j++) {
            if (!imp[j].OriginalFirstThunk || imp[j].OriginalFirstThunk >= image_size ||
                imp[j].FirstThunk >= image_size)
                continue;
            IMAGE_THUNK_DATA64 *names = (IMAGE_THUNK_DATA64 *)(image + imp[j].OriginalFirstThunk),
                               *addresses = (IMAGE_THUNK_DATA64 *)(image + imp[j].FirstThunk);
            size_t count = (image_size - imp[j].OriginalFirstThunk) / sizeof *names,
                   ac = (image_size - imp[j].FirstThunk) / sizeof *addresses;
            if (ac < count)
                count = ac;
            for (size_t i = 0; i < count && names[i].u1.AddressOfData; i++) {
                if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal))
                    continue;
                uint64_t off = names[i].u1.AddressOfData;
                if (off > image_size || image_size - off < 7)
                    continue;
                IMAGE_IMPORT_BY_NAME *bn = (IMAGE_IMPORT_BY_NAME *)(image + off);
                if (!memcmp(bn->Name, "free", 5)) {
                    h.release = (void (*)(void *))addresses[i].u1.Function;
                    break;
                }
            }
        }
    }
    if (!h.release)
        h.release = (void (*)(void *))resolve(c, b, image, "free", 1);
    return h;
}
#elif defined(__APPLE__)
QHost qresolve(QCtx *c) { return qmac_host(c); }
#else
QHost qresolve(QCtx *c) { qfail(c, "This package supports Windows x64 and macOS Intel/Apple Silicon"); }
#endif
static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    return s;
}
int qmemory_text(QCtx *c, QBytes before, QBytes *after) {
    if (before.n > 1024 * 1024 || memchr(before.p, 0, before.n))
        qfail(c, "Unsupported settings format. Set SMS_MEM_MB = 1024 manually.");
    int bom = before.n >= 3 && !memcmp(before.p, "\xef\xbb\xbf", 3);
    char *all = qs(c, before.p, before.n);
    const char *nl = strstr(all, "\r\n") ? "\r\n" : "\n";
    QBuf result = {.c = c};
    if (bom)
        qb_add(&result, before.p, 3);
    size_t pos = bom ? 3 : 0;
    int found = 0;
    while (pos < before.n) {
        const unsigned char *end = memchr(before.p + pos, '\n', before.n - pos);
        size_t next = end ? (size_t)(end - before.p) + 1 : before.n;
        char *line = qs(c, before.p + pos, next - pos), *clean = trim(line),
             *eq = strchr(clean, '=');
        int match = 0;
        if (eq) {
            *eq = 0;
            match = !strcmp(trim(clean), "SMS_MEM_MB");
        }
        if (match) {
            if (!found) {
                char *value = trim(eq + 1), *comment = strstr(value, " #");
                if (comment)
                    *comment = 0;
                char *tail;
                long mb = strtol(value, &tail, 10);
                if (!(bom && pos == 3) && tail != value && !*trim(tail) && mb >= 512 &&
                    mb <= 2048) {
                    *after = before;
                    return 0;
                }
                if (bom && pos == 3)
                    qb_add(&result, nl, strlen(nl));
                qb_add(&result, "SMS_MEM_MB = 1024", 17);
                if (end)
                    qb_add(&result, nl, strlen(nl));
                found = 1;
            }
        } else
            qb_add(&result, before.p + pos, next - pos);
        pos = next;
    }
    if (!found) {
        if (result.n && result.p[result.n - 1] != '\n')
            qb_add(&result, nl, strlen(nl));
        qb_add(&result, "SMS_MEM_MB = 1024", 17);
        qb_add(&result, nl, strlen(nl));
    }
    *after = qb_bytes(&result);
    return 1;
}
#ifndef _WIN32
static int same_settings(const char *path, const struct stat *before, QBytes bytes) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return 0;
    struct stat now;
    int ok = !fstat(fd, &now) && S_ISREG(now.st_mode) && now.st_dev == before->st_dev &&
             now.st_ino == before->st_ino && now.st_mode == before->st_mode &&
             now.st_size >= 0 && (uint64_t)now.st_size == bytes.n;
    size_t at = 0;
    unsigned char buffer[4096];
    while (ok && at < bytes.n) {
        size_t count = bytes.n - at < sizeof buffer ? bytes.n - at : sizeof buffer;
        ssize_t n = read(fd, buffer, count);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || memcmp(buffer, bytes.p + at, (size_t)n))
            ok = 0;
        else
            at += (size_t)n;
    }
    unsigned char extra;
    if (ok && read(fd, &extra, 1) != 0)
        ok = 0;
    close(fd);
    struct stat entry;
    return ok && !lstat(path, &entry) && entry.st_dev == before->st_dev &&
           entry.st_ino == before->st_ino && S_ISREG(entry.st_mode);
}
#endif
int qmemory(QCtx *c, uint32_t current, int in_stage, const char *data) {
    if (current >= 512u * 1024 * 1024)
        return 0;
    if (in_stage)
        qfail(c, "Quahog needs at least 512 MB of game memory. Close the game and launch with "
                 "Quahog enabled.");
#ifdef _WIN32
    wchar_t w[32768];
    DWORD n = GetEnvironmentVariableW(L"SMS_SETTINGS", w, 32768);
    char *settings = NULL;
    if (n >= 32768)
        qfail(c, "SMS_SETTINGS path is too long; settings left unchanged.");
    if (n)
        settings = utf8(c, w);
    else if (qexists("settings.txt"))
        settings = "settings.txt";
    else if (qexists("../../settings.txt"))
        settings = "../../settings.txt";
    if (!settings)
        qfail(c, "No active settings.txt found. Set SMS_MEM_MB = 1024 in the game's settings.txt, "
                 "then restart the game.");
    wchar_t *file = wide(c, settings);
    DWORD attrs = GetFileAttributesW(file);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        qfail(c,
              "Cannot safely update game settings. Set SMS_MEM_MB = 1024 manually, then restart.");
    QBytes before = qread(c, settings, 1024 * 1024, 0), after;
    if (!qmemory_text(c, before, &after))
        qfail(c, "Game memory is below 512 MB although settings already request enough. Close and "
                 "reopen SMS Launcher and check the SMS_MEM_MB environment override. Settings "
                 "unchanged.");
    qmkdir(c, data);
    char *backup = qfmt(c, "%s/settings-before-quahog-%016" PRIx64 ".txt", data, qfnv(before));
    if (qexists(backup)) {
        QBytes old = qread(c, backup, 1024 * 1024, 0);
        if (old.n != before.n || memcmp(old.p, before.p, before.n))
            qfail(c, "Existing settings backup differs. Settings left unchanged.");
    } else
        qatomic(c, backup, before);
    char *temp = qfmt(c, "%s.quahog-%lu.tmp", settings, (unsigned long)GetCurrentProcessId());
    qwrite(c, temp, after);
    QBytes now = qread(c, settings, 1024 * 1024, 0);
    if (now.n != before.n || memcmp(now.p, before.p, before.n)) {
        qremove(temp);
        qfail(c, "Settings changed during preparation. Close the game and retry.");
    }
    if (!ReplaceFileW(file, wide(c, temp), NULL, 0, NULL, NULL)) {
        qremove(temp);
        qfail(c, "Cannot update settings. Set SMS_MEM_MB = 1024 manually.");
    }
    qfail(c,
          "Saved SMS_MEM_MB = 1024. Close the game and press Play once more.\nSettings: "
          "%s\nBackup: %s",
          settings, backup);
#else
    const char *settings = getenv("SMS_SETTINGS");
    if (!settings || !*settings) {
        settings = qexists("settings.txt") ? "settings.txt" :
                   qexists("../../settings.txt") ? "../../settings.txt" : NULL;
    }
    if (!settings)
        qfail(c, "No active settings.txt found. Set SMS_MEM_MB = 1024 in the game's settings.txt "
                 "and restart with Quahog enabled.");
    if (strlen(settings) >= Q_PATH - 32)
        qfail(c, "SMS_SETTINGS path is too long; settings left unchanged");
    struct stat status;
    if (lstat(settings, &status) || !S_ISREG(status.st_mode) || status.st_nlink != 1 ||
        status.st_uid != geteuid())
        qfail(c, "Cannot safely update game settings. Use a regular settings.txt owned by your "
                 "account, set SMS_MEM_MB = 1024, then restart.");
    QBytes before = qread(c, settings, 1024 * 1024, 0), after;
    if (!qmemory_text(c, before, &after))
        qfail(c, "Game memory is below 512 MB although settings already request enough. Close "
                 "and reopen SMS Launcher and check the SMS_MEM_MB environment override. "
                 "Settings unchanged.");
    qmkdir(c, data);
    char *backup = qfmt(c, "%s/settings-before-quahog-%016" PRIx64 ".txt", data, qfnv(before));
    if (qexists(backup)) {
        QBytes old = qread(c, backup, 1024 * 1024, 0);
        if (old.n != before.n || memcmp(old.p, before.p, before.n))
            qfail(c, "Existing settings backup differs. Settings left unchanged.");
    } else
        qatomic(c, backup, before);
    char *temp = qfmt(c, "%s.quahog-XXXXXX", settings);
    int fd = mkstemp(temp);
    if (fd < 0)
        qfail(c, "Cannot create settings update. Set SMS_MEM_MB = 1024 manually.");
    int ok = !fchmod(fd, status.st_mode & 0777);
    size_t written = 0;
    while (ok && written < after.n) {
        ssize_t n = write(fd, after.p + written, after.n - written);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            ok = 0;
        else
            written += (size_t)n;
    }
    if (fsync(fd))
        ok = 0;
    if (close(fd))
        ok = 0;
    if (!ok || !same_settings(settings, &status, before)) {
        qremove(temp);
        qfail(c, "Settings changed or could not be written. Original settings left unchanged.");
    }
    if (rename(temp, settings)) {
        qremove(temp);
        qfail(c, "Cannot commit settings update. Set SMS_MEM_MB = 1024 manually.");
    }
    qfail(c, "Saved SMS_MEM_MB = 1024. Close the game and press Play once more.\nSettings: "
             "%s\nBackup: %s", settings, backup);
#endif
}
/* Process-owned original/prepared byte buffers survive F1 disable/re-enable. */
typedef struct {
    char path[256];
    uint64_t checksum;
    uintptr_t pointer;
    uint32_t size, pad;
} Saved;
typedef struct {
    uint64_t magic;
    uintptr_t image;
    uint32_t pid, count;
    uint64_t bytes;
    Saved records[64];
} SavedCache;
static const void *retain(QCtx *c, const char *path, QBytes b) {
#ifdef _WIN32
    wchar_t name[100];
    swprintf(name, 100, L"Local\\Randyadr.Quahog.C.Originals.v5.%lu",
             (unsigned long)GetCurrentProcessId());
    HANDLE mapping =
        CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SavedCache), name);
    if (!mapping)
        qfail(c, "Cannot retain native archive data");
    int existing = GetLastError() == ERROR_ALREADY_EXISTS;
    SavedCache *cache = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof *cache);
    if (!cache) {
        CloseHandle(mapping);
        qfail(c, "Cannot map native archive cache");
    }
    if (existing)
        CloseHandle(mapping);
    else {
        memset(cache, 0, sizeof *cache);
        cache->magic = UINT64_C(0x514e415449564535);
        cache->image = (uintptr_t)GetModuleHandleW(NULL);
        cache->pid = GetCurrentProcessId();
    }
    if (cache->magic != UINT64_C(0x514e415449564535) || cache->pid != GetCurrentProcessId() ||
        cache->image != (uintptr_t)GetModuleHandleW(NULL) || cache->count > 64) {
        UnmapViewOfFile(cache);
        qfail(c, "Invalid process archive cache");
    }
#else
#if defined(__APPLE__) && !defined(QUAHOG_TEST)
    qmac_pin(c);
#endif
    static SavedCache storage = {0};
    SavedCache *cache = &storage;
#endif
    uint64_t h = qfnv(b);
    const void *out = NULL;
    int conflict = 0;
    for (unsigned i = 0; i < cache->count; i++) {
        Saved *r = &cache->records[i];
        if (!strcmp(path, r->path)) {
            if (r->checksum != h || r->size != b.n)
                conflict = 1;
            else
                out = (const void *)r->pointer;
            break;
        }
    }
    const char *error = NULL;
    if (conflict)
        error = "Another mod or cache update changed the loaded stage. Restart the game.";
    if (!out && !error) {
        if (strlen(path) >= 256 || cache->count >= 64 || b.n > 512u * 1024 * 1024 - cache->bytes)
            error = "Native archive cache capacity exceeded";
        else {
#ifdef _WIN32
            void *copy = HeapAlloc(GetProcessHeap(), 0, b.n ? b.n : 1);
#else
            void *copy = malloc(b.n ? b.n : 1);
#endif
            if (!copy)
                error = "Out of memory retaining native archives";
            else {
                if (b.n)
                    memcpy(copy, b.p, b.n);
                Saved *r = &cache->records[cache->count++];
                strcpy(r->path, path);
                r->checksum = h;
                r->pointer = (uintptr_t)copy;
                r->size = (uint32_t)b.n;
                cache->bytes += b.n;
                out = copy;
            }
        }
    }
#ifdef _WIN32
    UnmapViewOfFile(cache);
#endif
    if (error)
        qfail(c, "%s", error);
    return out;
}
void qrestore(QOverride *o, QHost host) {
    if (o->active) {
        host.replace(o->path, o->original, o->old_size);
        o->active = 0;
    }
}
static void log_line(const char *data, const char *s) {
    char path[Q_PATH];
    if (snprintf(path, sizeof path, "%s/automatic-setup.log", data) >= (int)sizeof path)
        return;
    FILE *f = qopen(path, "ab");
    if (f) {
        fprintf(f, "%s\n", s);
        fclose(f);
    }
}
static QBytes stage_read(QCtx *c, QHost host, const char *stem, char **path) {
    *path = qfmt(c, "/data/scene/%s.szs", stem);
    QBytes b = qhost_read(c, host, *path, 1);
    if (!b.p) {
        *path = qfmt(c, "/data/scene/%s.arc", stem);
        b = qhost_read(c, host, *path, 0);
    }
    return b;
}
static int slotcmp(const void *a, const void *b) {
    return strcmp(((const QSlot *)a)->stem, ((const QSlot *)b)->stem);
}
static int is_plaza(const QSlot *s) {
    if (s->area != 1 || strncmp(s->stem, "dolpic", 6) || !s->stem[6])
        return 0;
    for (const char *p = s->stem + 6; *p; p++)
        if (!isdigit((unsigned char)*p))
            return 0;
    return 1;
}
/* Optional placement work is isolated from map activation and from loop state. */
static QLocation cached_market(QHost host, const char *stem, const char *cache, const char *data) {
    struct MarketJob {
        QCtx ctx;
        QLocation location;
    };
    struct MarketJob *job = calloc(1, sizeof *job);
    if (!job) {
        log_line(data, "Not enough memory for automatic portal placement; F6 remains available.");
        return (QLocation){0};
    }
    QCtx *p = &job->ctx;
    if (!setjmp(p->failure)) {
        char *unused;
        QBytes raw = stage_read(p, host, stem, &unused);
        char ph[65];
        qsha(raw, ph);
        char *lp = qfmt(p, "%s/portal-%s.txt", cache, ph);
        QBytes existing = qread(p, lp, 256, 1);
        int consumed = 0;
        char *text = qs(p, existing.p, existing.n);
        QLocation got = {0};
        if (sscanf(text, "%f %f %f %f %n", &got.x, &got.y, &got.z, &got.yaw, &consumed) == 4 &&
            (size_t)consumed == existing.n && isfinite(got.x) && isfinite(got.y) &&
            isfinite(got.z) && isfinite(got.yaw) && fabsf(got.x) < 100000 &&
            fabsf(got.y) < 100000 && fabsf(got.z) < 100000) {
            got.valid = 1;
            job->location = got;
        } else if (strcmp(text, "unresolved\n")) {
            job->location = qmarket(p, raw);
            char *value = job->location.valid
                              ? qfmt(p, "%.9g %.9g %.9g %.9g\n", job->location.x, job->location.y,
                                     job->location.z, job->location.yaw)
                              : "unresolved\n";
            qatomic(p, lp, (QBytes){(unsigned char *)value, strlen(value)});
        }
    } else
        log_line(data, p->error);

    QLocation result = job->location;
    qclear(p);
    free(job);
    return result;
}

static int saved_placement_valid(const char *path) {
    FILE *f = qopen(path, "rb");
    if (!f)
        return 0;
    float x, y, z, yaw;
    char extra;
    int count = fscanf(f, "%f %f %f %f %c", &x, &y, &z, &yaw, &extra);
    fclose(f);
    return count == 4 && isfinite(x) && isfinite(y) && isfinite(z) && isfinite(yaw) &&
           fabsf(x) < 100000 && fabsf(y) < 100000 && fabsf(z) < 100000;
}

void qprepare(QCtx *c, QHost host, const char *mod, const char *data, QOverride *o, QStages *stages,
              QLocation *location) {
    if (!host.read || !host.replace || !host.release || !host.memory_size)
        qfail(c, "Incomplete game file interface");
    if (*host.memory_size < 512u * 1024 * 1024)
        qfail(c, "Quahog requires at least 512 MB game memory");
    qmkdir(c, data);
    log_line(data, "Quahog " Q_VERSION " native C preparation; no interpreter or child process.");
    QBytes table = qhost_read(c, host, "/data/stagearc.bin", 1);
    if (!table.p)
        table = qhost_read(c, host, "/data/stageArc.bin", 0);
    *stages = qstages(c, table);
    char *path = NULL;
    QBytes original = stage_read(c, host, stages->target, &path),
           common = qhost_read(c, host, "/data/scenecmn.bin", 1);
    char old_hash[65], common_hash[65], table_hash[65], key[65];
    qsha(original, old_hash);
    qsha(common, common_hash);
    qsha(table, table_hash);
    char *id =
        qfmt(c, "quahog-native-c-" Q_VERSION "\n%s\n%s\n%s\n", old_hash, common_hash, table_hash);
    qsha((QBytes){(unsigned char *)id, strlen(id)}, key);
    char *cache = qjoin(c, data, "prepared-c-v5");
    qmkdir(c, cache);
    char *archive = qfmt(c, "%s/%s.arc", cache, key), *record = qfmt(c, "%s/%s.sha256", cache, key);
    QBytes prepared = {0};
    /* Optional cache validation uses its own arena/error boundary. Corruption rebuilds. */
    QCtx *probe = calloc(1, sizeof *probe);
    if (!probe)
        qfail(c, "Out of memory checking cache");
    if (!setjmp(probe->failure)) {
        QBytes mark = qread(probe, record, 128, 1);
        if (mark.n == 65 && mark.p[64] == '\n') {
            QBytes b = qread(probe, archive, Q_LIMIT, 1);
            char hash[65];
            qsha(b, hash);
            if (b.n >= 96 && !memcmp(b.p, "RARC", 4) && qbe32(b.p + 4) == b.n &&
                !memcmp(mark.p, hash, 64)) {
                prepared = b;
            }
        }
    }
    if (prepared.p && !qmerge(c, probe)) {
        qclear(probe);
        free(probe);
        qfail(c, "Preparation memory limit exceeded while adopting cache");
    }
    qclear(probe);
    free(probe);
    if (!prepared.p) {
        log_line(data, "Building native Quahog archive from the active game...");
        prepared = qbuild(c, original, qjoin(c, mod, "assets"), common);
        char hash[65];
        qsha(prepared, hash);
        qatomic(c, archive, prepared);
        char *line = qfmt(c, "%s\n", hash);
        qatomic(c, record, (QBytes){(unsigned char *)line, strlen(line)});
        log_line(data, "Native map built and all archive resources verified.");
    } else
        log_line(data, "Verified prepared archive reused.");
    /* Locate a default only when no user placement exists. Failure never invents a location. */
    *location = (QLocation){0};
    char *placement = qjoin(c, data, "quahog-portal.txt");
    if (!saved_placement_valid(placement)) {
        qsort(stages->slots, stages->count, sizeof(QSlot), slotcmp);
        const char *last = "";
        for (size_t i = 0; i < stages->count; i++) {
            QSlot *s = &stages->slots[i];
            if (!is_plaza(s) || !strcmp(last, s->stem))
                continue;
            last = s->stem;
            *location = cached_market(host, s->stem, cache, data);
            if (location->valid)
                break;
        }
    }
    if (strlen(path) >= sizeof o->path)
        qfail(c, "Stage path is too long");
    strcpy(o->path, path);
    o->original = retain(c, path, original);
    o->old_size = (uint32_t)original.n;
    o->prepared = retain(c, qfmt(c, "@prepared%s", path), prepared);
    o->new_size = (uint32_t)prepared.n; /* Recheck the host before publishing, so another mod cannot
                                           be overwritten unnoticed. */
    QBytes now = qhost_read(c, host, path, 0);
    if (now.n != original.n || qfnv(now) != qfnv(original))
        qfail(c, "Another mod changed the target stage during preparation");
    log_line(
        data,
        location->valid
            ? "Market portal located; custom saved placement takes priority."
            : "Use saved placement, or F1 > Quahog > Place portal here. F6 travel is available.");
}
