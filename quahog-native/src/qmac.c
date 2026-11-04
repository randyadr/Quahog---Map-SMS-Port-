/* macOS host binding. Only the system C runtime and dyld are used. */
#include "qcore.h"
#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdlib.h>
#include <string.h>

static const struct mach_header_64 *main_header(QCtx *c) {
    const struct mach_header_64 *h = (const void *)_dyld_get_image_header(0);
    if (!h || h->magic != MH_MAGIC_64 || h->filetype != MH_EXECUTE ||
        h->sizeofcmds > 16u * 1024 * 1024 || h->ncmds > 65536)
        qfail(c, "Cannot identify the running 64-bit Mac game");
    return h;
}
static uintptr_t slide_address(QCtx *c, uint64_t value) {
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    uint64_t amount = slide < 0 ? (uint64_t)(-(slide + 1)) + 1 : (uint64_t)slide;
    if ((slide < 0 && value < amount) || (slide >= 0 && value > UINTPTR_MAX - amount))
        qfail(c, "Mac symbol address overflow");
    return (uintptr_t)(slide < 0 ? value - amount : value + amount);
}
static int live_address(QCtx *c, uintptr_t address, int code) {
    const struct mach_header_64 *h = main_header(c);
    const unsigned char *p = (const void *)(h + 1), *end = p + h->sizeofcmds;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        if ((size_t)(end - p) < sizeof(struct load_command))
            qfail(c, "Invalid loaded Mach-O commands");
        const struct load_command *lc = (const void *)p;
        if (lc->cmdsize < 8 || lc->cmdsize % 8 || lc->cmdsize > (size_t)(end - p))
            qfail(c, "Invalid loaded Mach-O command size");
        if (lc->cmd == LC_SEGMENT_64) {
            if (lc->cmdsize < sizeof(struct segment_command_64))
                qfail(c, "Invalid loaded Mach-O segment");
            const struct segment_command_64 *s = (const void *)p;
            if (s->initprot & 1) {
                uintptr_t base = slide_address(c, s->vmaddr);
                uint64_t width = code ? 1 : sizeof(uint32_t);
                if (address >= base && address - base <= s->vmsize &&
                    width <= s->vmsize - (address - base) && (!code || (s->initprot & 4)))
                    return 1;
            }
        }
        p += lc->cmdsize;
    }
    return 0;
}
static void match_uuid(QCtx *c, QBytes slice) {
    const struct mach_header_64 *h = main_header(c);
    const unsigned char *p = (const void *)(h + 1), *end = p + h->sizeofcmds;
    const unsigned char *uuid = NULL;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        if ((size_t)(end - p) < 8)
            qfail(c, "Invalid loaded Mach-O UUID commands");
        const struct load_command *lc = (const void *)p;
        if (lc->cmdsize < 8 || lc->cmdsize > (size_t)(end - p))
            qfail(c, "Invalid loaded Mach-O UUID command");
        if (lc->cmd == LC_UUID && lc->cmdsize >= 24)
            uuid = p + 8;
        p += lc->cmdsize;
    }
    if (!uuid)
        qfail(c, "Mac game has no build UUID; cannot safely use its file symbols");
    /* qmacho_symbol validated all file load commands before this call. */
    struct mach_header_64 file_header;
    memcpy(&file_header, slice.p, sizeof file_header);
    size_t at = sizeof file_header;
    for (uint32_t i = 0; i < file_header.ncmds; i++) {
        struct load_command lc;
        memcpy(&lc, slice.p + at, sizeof lc);
        if (lc.cmd == LC_UUID && lc.cmdsize >= 24) {
            if (memcmp(slice.p + at + 8, uuid, 16))
                qfail(c, "The Mac game changed on disk. Close and restart the game.");
            return;
        }
        at += lc.cmdsize;
    }
    qfail(c, "Mac game file has no matching build UUID");
}
static void *symbol(QCtx *c, QBytes *file, const char *name, int code) {
    void *p = dlsym(RTLD_MAIN_ONLY, name);
    if (p && live_address(c, (uintptr_t)p, code))
        return p;
    const struct mach_header_64 *h = main_header(c);
    if (!file->p) {
        char path[Q_PATH];
        uint32_t size = sizeof path;
        if (_NSGetExecutablePath(path, &size))
            qfail(c, "Mac game executable path is too long");
        *file = qmap_readonly(c, path);
    }
    uint64_t value = qmacho_symbol(c, *file, (uint32_t)h->cputype, name, code);
    if (!value)
        qfail(c, "Game lacks native symbol %s. Use an unstripped SMS Launcher Mac build.", name);
    match_uuid(c, qmacho_slice(c, *file, (uint32_t)h->cputype));
    uintptr_t address = slide_address(c, value);
    if (!live_address(c, address, code))
        qfail(c, "Mac host symbol is not mapped: %s", name);
    return (void *)address;
}
QHost qmac_host(QCtx *c) {
    QBytes file = {0};
    QHost h = {0};
    h.read = (void *(*)(const char *, size_t *))symbol(c, &file, "port_dvd_read_file", 1);
    h.replace = (int (*)(const char *, const void *, uint32_t))
        symbol(c, &file, "port_dvd_override", 1);
    h.memory_size = (uint32_t *)symbol(c, &file, "port_mem1_size", 0);
    /* dvd.cpp allocates its result with the process's system malloc. */
    h.release = free;
    return h;
}
void qmac_pin(QCtx *c) {
    /* Restored DVD overrides reference our malloc'd bytes after unload. Keep
       the registry as well, so F1 disable/re-enable reuses those allocations. */
    static int pinned;
    if (pinned)
        return;
    Dl_info info;
    if (!dladdr((const void *)qmac_pin, &info) || !info.dli_fname)
        qfail(c, "Cannot identify the Mac mod for archive retention");
    void *handle = dlopen(info.dli_fname, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle)
        qfail(c, "Cannot retain Mac archive registry: %s", dlerror());
    pinned = 1;
    /* Keep the reference for process lifetime. No constructor or callback is
       added. Unload still unsubscribes events and restores the original map. */
}
#endif
