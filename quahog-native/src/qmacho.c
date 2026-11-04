/* Bounded 64-bit Mach-O lookup, including universal Intel/Apple Silicon files.
   No Apple headers are needed by the parser, so its fixtures run on any host. */
#include "qcore.h"
#include <string.h>

static void need(QCtx *c, QBytes b, uint64_t at, uint64_t n) {
    if (at > b.n || n > b.n - at)
        qfail(c, "Truncated Mach-O metadata");
}
static uint32_t u32(QCtx *c, QBytes b, size_t at, int be) {
    need(c, b, at, 4);
    if (be)
        return qbe32(b.p + at);
    return (uint32_t)b.p[at] | (uint32_t)b.p[at + 1] << 8 |
           (uint32_t)b.p[at + 2] << 16 | (uint32_t)b.p[at + 3] << 24;
}
static uint64_t u64(QCtx *c, QBytes b, size_t at, int be) {
    uint64_t a = u32(c, b, at, be), d = u32(c, b, at + 4, be);
    return be ? (a << 32) | d : a | (d << 32);
}
QBytes qmacho_slice(QCtx *c, QBytes b, uint32_t cpu) {
    if (cpu != 0x01000007 && cpu != 0x0100000c)
        qfail(c, "Unsupported Mac architecture");
    uint32_t magic = u32(c, b, 0, 1);
    int fat64 = magic == 0xcafebabf || magic == 0xbfbafeca;
    if (fat64 || magic == 0xcafebabe || magic == 0xbebafeca) {
        int be = magic == 0xcafebabe || magic == 0xcafebabf;
        uint32_t count = u32(c, b, 4, be);
        size_t stride = fat64 ? 32 : 20;
        if (!count || count > 64)
            qfail(c, "Invalid universal Mach-O architecture table");
        need(c, b, 8, (uint64_t)count * stride);
        int found = 0;
        QBytes chosen = {0};
        for (uint32_t i = 0; i < count; i++) {
            size_t at = 8 + i * stride;
            uint64_t off = fat64 ? u64(c, b, at + 8, be) : u32(c, b, at + 8, be);
            uint64_t size = fat64 ? u64(c, b, at + 16, be) : u32(c, b, at + 12, be);
            if (off < 8 + (uint64_t)count * stride || !size)
                qfail(c, "Invalid universal Mach-O slice");
            need(c, b, off, size);
            if (u32(c, b, at, be) == cpu) {
                /* Two subtypes of one CPU would need subtype selection. Refuse
                   instead of accidentally resolving offsets in the wrong one. */
                if (found++)
                    qfail(c, "Ambiguous Mac executable architecture");
                chosen = (QBytes){b.p + (size_t)off, (size_t)size};
            }
        }
        if (!found)
            qfail(c, "Game executable has no matching Mac architecture");
        b = chosen;
    }
    need(c, b, 0, 32);
    if (u32(c, b, 0, 0) != 0xfeedfacf || u32(c, b, 4, 0) != cpu ||
        u32(c, b, 12, 0) != 2)
        qfail(c, "Expected the running 64-bit Mac game executable");
    return b;
}
uint64_t qmacho_symbol(QCtx *c, QBytes file, uint32_t cpu, const char *wanted, int code) {
    QBytes b = qmacho_slice(c, file, cpu);
    uint32_t commands = u32(c, b, 16, 0), bytes = u32(c, b, 20, 0);
    if (commands > 65536 || bytes > 16u * 1024 * 1024)
        qfail(c, "Invalid Mach-O load-command count");
    need(c, b, 32, bytes);
    size_t end = 32 + (size_t)bytes, at = 32;
    uint32_t symbols = 0, count = 0, strings = 0, string_size = 0;
    struct { uint64_t address, size; uint32_t protection; } sections[255];
    unsigned section_count = 0;
    int symtab = 0;
    for (uint32_t i = 0; i < commands; i++) {
        if (at > end || end - at < 8)
            qfail(c, "Truncated Mach-O load command");
        uint32_t cmd = u32(c, b, at, 0), size = u32(c, b, at + 4, 0);
        if (size < 8 || size % 8 || size > end - at)
            qfail(c, "Invalid Mach-O load command size");
        if (cmd == 2) { /* LC_SYMTAB */
            if (size < 24 || symtab++)
                qfail(c, "Invalid Mach-O symbol table");
            symbols = u32(c, b, at + 8, 0);
            count = u32(c, b, at + 12, 0);
            strings = u32(c, b, at + 16, 0);
            string_size = u32(c, b, at + 20, 0);
        } else if (cmd == 0x19) { /* LC_SEGMENT_64 */
            if (size < 72)
                qfail(c, "Truncated Mach-O segment");
            uint64_t va = u64(c, b, at + 24, 0), len = u64(c, b, at + 32, 0);
            uint32_t n = u32(c, b, at + 64, 0), prot = u32(c, b, at + 60, 0);
            if (len > UINT64_MAX - va || n > (size - 72) / 80 || n > 255 - section_count)
                qfail(c, "Invalid Mach-O sections");
            for (uint32_t j = 0; j < n; j++) {
                size_t s = at + 72 + (size_t)j * 80;
                uint64_t address = u64(c, b, s + 32, 0), span = u64(c, b, s + 40, 0);
                if (address < va || address - va > len || span > len - (address - va))
                    qfail(c, "Mach-O section is outside its segment");
                sections[section_count].address = address;
                sections[section_count].size = span;
                sections[section_count++].protection = prot;
            }
        }
        at += size;
    }
    if (at != end)
        qfail(c, "Inconsistent Mach-O load commands");
    if (!symtab || !count)
        return 0;
    if (count > 10000000)
        qfail(c, "Mach-O symbol count exceeds limit");
    need(c, b, symbols, (uint64_t)count * 16);
    need(c, b, strings, string_size);
    size_t name_size = strlen(wanted);
    for (uint32_t i = 0; i < count; i++) {
        size_t s = symbols + (size_t)i * 16;
        unsigned type = b.p[s + 4], section = b.p[s + 5];
        if ((type & 0xe0) || (type & 0x0e) != 0x0e)
            continue; /* Skip debug, undefined, absolute and indirect symbols. */
        uint32_t offset = u32(c, b, s, 0);
        if (offset >= string_size)
            qfail(c, "Invalid Mach-O string offset");
        const char *name = (const char *)b.p + strings + offset;
        const char *nul = memchr(name, 0, string_size - offset);
        if (!nul)
            qfail(c, "Unterminated Mach-O symbol name");
        if ((size_t)(nul - name) != name_size + 1 || name[0] != '_' ||
            memcmp(name + 1, wanted, name_size))
            continue;
        if (!section || section > section_count)
            qfail(c, "Invalid Mach-O symbol section");
        unsigned k = section - 1;
        uint64_t value = u64(c, b, s + 8, 0), base = sections[k].address, len = sections[k].size;
        uint32_t required = code ? 5 : 1; /* VM_PROT_READ | VM_PROT_EXECUTE */
        size_t width = code ? 1 : sizeof(uint32_t);
        if ((sections[k].protection & required) != required || value < base ||
            value - base > len || width > len - (value - base))
            qfail(c, "Mac symbol is outside a compatible mapped section: %s", wanted);
        return value;
    }
    return 0;
}
