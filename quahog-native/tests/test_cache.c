#include "qcore.h"
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static char game[Q_PATH];
static uint32_t memory_size = 1024u * 1024 * 1024;
static int replacements;
static void *reader(const char *path, size_t *n) {
    char file[Q_PATH];
    assert(snprintf(file, sizeof file, "%s%s", game, path) < (int)sizeof file);
    FILE *f = qopen(file, "rb");
    if (!f)
        return NULL;
    assert(!fseek(f, 0, SEEK_END));
    long size = ftell(f);
    assert(size >= 0);
    rewind(f);
    void *p = malloc(size ? (size_t)size : 1);
    assert(fread(p, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    *n = (size_t)size;
    return p;
}
static int replace(const char *p, const void *b, uint32_t n) {
    (void)p;
    (void)b;
    (void)n;
    replacements++;
    return 1;
}
static int prepare(const char *mod, const char *data, char hash[65], int expect_success) {
    QCtx *c = calloc(1, sizeof *c);
    int error = setjmp(c->failure);
    if (!error) {
        QStages s;
        QLocation p;
        QOverride o = {0};
        qprepare(c, (QHost){reader, replace, free, &memory_size}, mod, data, &o, &s, &p);
        assert(s.area == 23 && s.episode == 1 && p.valid);
        qsha((QBytes){(unsigned char *)o.prepared, o.new_size}, hash);
    } else
        fprintf(stderr, "Expected failure: %s\n", c->error);
    assert((error == 0) == expect_success);
    qclear(c);
    free(c);
    return error == 0;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    assert(strlen(argv[1]) < sizeof game);
    strcpy(game, argv[1]);
    char h1[65], h2[65], folder[Q_PATH], path[Q_PATH] = {0};
    assert(prepare(argv[2], argv[3], h1, 1));
    assert(snprintf(folder, sizeof folder, "%s/prepared-c-v5", argv[3]) < (int)sizeof folder);
    DIR *dir = opendir(folder);
    assert(dir);
    struct dirent *e;
    while ((e = readdir(dir)))
        if (qends(e->d_name, ".arc")) {
            assert(!path[0]);
            assert(snprintf(path, sizeof path, "%s/%s", folder, e->d_name) < (int)sizeof path);
        }
    closedir(dir);
    assert(path[0]);
    struct stat before, after;
    assert(!stat(path, &before)); /* Missing asset directory proves the verified cache path actually
                                     skips assembly. */
    assert(prepare("missing-assets-for-cache-test", argv[3], h2, 1));
    assert(!strcmp(h1, h2));
    assert(!stat(path, &after));
    assert(before.st_mtime == after.st_mtime);
    FILE *f = qopen(path, "r+b");
    assert(f);
    assert(!fseek(f, 128, SEEK_SET));
    int byte = fgetc(f);
    assert(byte != EOF);
    assert(!fseek(f, 128, SEEK_SET));
    fputc(byte ^ 0x80, f);
    fclose(f);
    assert(!prepare("missing-assets-for-cache-test", argv[3], h2, 0));
    assert(prepare(argv[2], argv[3], h2, 1));
    assert(!strcmp(h1, h2));
    /* A broken checksum record also rebuilds, with no host override published. */ char
        manifest[Q_PATH];
    strcpy(manifest, path);
    strcpy(manifest + strlen(manifest) - 4, ".sha256");
    f = qopen(manifest, "wb");
    assert(f);
    fputs("broken\n", f);
    fclose(f);
    assert(prepare(argv[2], argv[3], h2, 1));
    assert(!strcmp(h1, h2) && replacements == 0);
    puts("Cache checks passed: reuse, corrupted archive repair, missing assets refusal, invalid "
         "checksum repair, no early overrides.");
    return 0;
}
