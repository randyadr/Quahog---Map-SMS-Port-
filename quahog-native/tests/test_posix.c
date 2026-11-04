#define _POSIX_C_SOURCE 200809L
#include "qcore.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static QBytes bytes(const char *s) { return (QBytes){(unsigned char *)s, strlen(s)}; }
static void expect_memory(uint32_t size, int in_stage, const char *data, const char *message) {
    QCtx *c = calloc(1, sizeof *c); assert(c);
    int failed = setjmp(c->failure);
    if (!failed) assert(!qmemory(c, size, in_stage, data));
    if (message) { assert(failed); assert(strstr(c->error, message)); }
    else assert(!failed);
    qclear(c); free(c);
}
int main(void) {
    char root[] = "/tmp/quahog-posix-XXXXXX";
    assert(mkdtemp(root));
    QCtx *c = calloc(1, sizeof *c); assert(c);
    if (setjmp(c->failure)) { fprintf(stderr, "%s\n", c->error); abort(); }
    char *dir = qjoin(c, root, "Mac space \\ Ã©"), *settings = qjoin(c, dir, "settings.txt");
    char *data = qjoin(c, root, "mod data"), *alias = qjoin(c, root, "linked.txt");
    qmkdir(c, dir); assert(qexists(dir));
    const char *original = "# preserve my options\nFPS = 120\nSMS_MEM_MB = 64\nOther = yes\n";
    qwrite(c, settings, bytes(original)); assert(!chmod(settings, 0640));
    assert(!setenv("SMS_SETTINGS", settings, 1));
    expect_memory(1024u << 20, 0, data, NULL);
    expect_memory(64u << 20, 1, data, "launch with Quahog enabled");
    expect_memory(64u << 20, 0, data, "Saved SMS_MEM_MB = 1024");
    QBytes content = qread(c, settings, 4096, 0);
    char *text = qs(c, content.p, content.n);
    assert(strstr(text, "SMS_MEM_MB = 1024\n"));
    assert(strstr(text, "FPS = 120\n") && strstr(text, "Other = yes\n"));
    struct stat st; assert(!stat(settings, &st) && (st.st_mode & 0777) == 0640);
    char *backup = qfmt(c, "%s/settings-before-quahog-%016llx.txt", data,
                        (unsigned long long)qfnv(bytes(original)));
    QBytes saved = qread(c, backup, 4096, 0);
    assert(saved.n == strlen(original) && !memcmp(saved.p, original, saved.n));
    expect_memory(64u << 20, 0, data, "settings already request enough");
    assert(!symlink(settings, alias)); assert(!setenv("SMS_SETTINGS", alias, 1));
    expect_memory(64u << 20, 0, data, "Cannot safely update"); assert(!unlink(alias));
    assert(!link(settings, alias));
    expect_memory(64u << 20, 0, data, "Cannot safely update"); assert(!unlink(alias));
    expect_memory(64u << 20, 0, data, "Cannot safely update");
    assert(!setenv("SMS_SETTINGS", settings, 1));
    qwrite(c, settings, bytes(original)); qwrite(c, backup, bytes("conflicting backup"));
    expect_memory(64u << 20, 0, data, "Existing settings backup differs");
    content = qread(c, settings, 4096, 0);
    assert(content.n == strlen(original) && !memcmp(content.p, original, content.n));
    assert(!unlink(settings)); assert(!unlink(backup)); assert(!rmdir(dir));
    assert(!rmdir(data)); assert(!rmdir(root)); assert(!unsetenv("SMS_SETTINGS"));
    qclear(c); free(c);
    puts("POSIX: UTF-8/spaces/backslash paths, memory update, preserved options/mode, backup, "
         "sufficient memory, in-stage refusal, symlinks/hardlinks and conflict checks passed.");
    return 0;
}
