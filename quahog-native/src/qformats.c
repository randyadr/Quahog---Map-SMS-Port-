#include "qcore.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint16_t key(const unsigned char *s, size_t n) {
    unsigned h = 0;
    for (size_t i = 0; i < n; i++)
        h = (h * 3 + s[i]) & 65535;
    return (uint16_t)h;
}
static int span(QBytes b, size_t p, size_t n) { return p <= b.n && n <= b.n - p; }
static void need(QCtx *c, QBytes b, size_t p, size_t n) {
    if (!span(b, p, n))
        qfail(c, "Truncated binary structure at %zu", p);
}
static int str_at(QBytes b, size_t *pos, QBytes *s) {
    if (!span(b, *pos, 2))
        return 0;
    size_t n = qbe16(b.p + *pos);
    *pos += 2;
    if (!span(b, *pos, n))
        return 0;
    *s = (QBytes){b.p + *pos, n};
    *pos += n;
    return 1;
}
static int in_list(const char *s, const char *const *a, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!strcmp(s, a[i]))
            return 1;
    return 0;
}
static int known_container(const char *s) {
    static const char *const names[] = {"GroupObj",
                                        "NameRefGrp",
                                        "IdxGroup",
                                        "Strategy",
                                        "LightAry",
                                        "AmbAry",
                                        "ScenarioArchiveNameTable",
                                        "ScenarioArchiveNamesInStage",
                                        "ScenarioArchiveNames",
                                        "StageArcNameTable",
                                        "StageArcNames",
                                        "MarioPositionObj",
                                        "MarScene",
                                        "SmJ3DScn"};
    return in_list(s, names, sizeof names / sizeof *names) || qends(s, "Table") || qends(s, "Ary");
}
static QNode *parse_node(QCtx *c, QBytes b, unsigned depth, int *strict_error) {
    if (depth > 128 || b.n < 12 || qbe32(b.p) != b.n)
        return NULL;
    size_t p = 6;
    QBytes type, name;
    if (!str_at(b, &p, &type) || !type.n || qbe16(b.p + 4) != key(type.p, type.n))
        return NULL;
    for (size_t i = 0; i < type.n; i++)
        if (!((type.p[i] >= 'A' && type.p[i] <= 'Z') || (type.p[i] >= 'a' && type.p[i] <= 'z') ||
              type.p[i] == '_' || (i && type.p[i] >= '0' && type.p[i] <= '9')))
            return NULL;
    if (!span(b, p, 2))
        return NULL;
    uint16_t hash = qbe16(b.p + p);
    p += 2;
    if (!str_at(b, &p, &name) || hash != key(name.p, name.n))
        return NULL;
    QNode *n = qa(c, sizeof *n);
    n->type = qs(c, type.p, type.n);
    n->name = qs(c, name.p, name.n);
    n->name_len = name.n;
    n->payload = (QBytes){b.p + p, b.n - p};
    int strict = !strcmp(n->type, "MarScene") || !strcmp(n->type, "SmJ3DScn");
    size_t off = !strcmp(n->type, "IdxGroup") ? 4 : 0;
    if (strict) {
        if (n->payload.n < 4)
            goto bad_strict;
        uint32_t count = qbe32(n->payload.p);
        off = 4;
        if (count > 10000 || count > (n->payload.n - 4) / 6)
            goto bad_strict;
        for (uint32_t i = 0; i < count; i++) {
            if (!span(n->payload, off, 4))
                goto bad_strict;
            off += 4;
            QBytes unused;
            if (!str_at(n->payload, &off, &unused))
                goto bad_strict;
        }
    }
    if (!span(n->payload, off, 4)) {
        if (strict)
            goto bad_strict;
        return n;
    }
    uint32_t count = qbe32(n->payload.p + off);
    if (count > 10000) {
        if (strict)
            goto bad_strict;
        return n;
    }
    if (!count && !known_container(n->type))
        return n;
    size_t pos = off + 4;
    QNode **children = qa(c, (size_t)count * sizeof *children);
    for (uint32_t i = 0; i < count; i++) {
        if (!span(n->payload, pos, 4))
            goto bad_children;
        uint32_t size = qbe32(n->payload.p + pos);
        if (size < 12 || !span(n->payload, pos, size))
            goto bad_children;
        children[i] = parse_node(c, (QBytes){n->payload.p + pos, size}, depth + 1, strict_error);
        if (*strict_error)
            return NULL;
        if (!children[i])
            goto bad_children;
        pos += size;
    }
    if (pos != n->payload.n)
        goto bad_children;
    n->container = 1;
    n->children = children;
    n->count = count;
    n->prefix = (QBytes){n->payload.p, off};
    return n;
bad_children:
    if (!strict)
        return n;
bad_strict:
    *strict_error = 1;
    return NULL;
}
QNode *qparse(QCtx *c, QBytes b) {
    int strict = 0;
    QNode *n = parse_node(c, b, 0, &strict);
    if (!n || strict)
        qfail(c, "Invalid scene framing, lighting map or child records");
    return n;
}
static void encode_node(QBuf *b, QNode *n, unsigned depth) {
    if (depth > 128)
        qfail(b->c, "Scene depth exceeded");
    size_t begin = b->n;
    qb_u32(b, 0);
    unsigned char h[2];
    size_t t = strlen(n->type);
    qput16(h, key((unsigned char *)n->type, t));
    qb_add(b, h, 2);
    qb_str(b, n->type, t);
    qput16(h, key((unsigned char *)n->name, n->name_len));
    qb_add(b, h, 2);
    qb_str(b, n->name, n->name_len);
    if (n->container) {
        qb_add(b, n->prefix.p, n->prefix.n);
        qb_u32(b, (uint32_t)n->count);
        for (size_t i = 0; i < n->count; i++)
            encode_node(b, n->children[i], depth + 1);
    } else
        qb_add(b, n->payload.p, n->payload.n);
    qput32(b->p + begin, (uint32_t)(b->n - begin));
}
QBytes qencode(QCtx *c, QNode *n) {
    QBuf b = {.c = c};
    encode_node(&b, n, 0);
    return qb_bytes(&b);
}
QBytes qyaz(QCtx *c, QBytes b) {
    if (b.n < 4 || memcmp(b.p, "Yaz0", 4))
        return b;
    need(c, b, 0, 16);
    uint32_t size = qbe32(b.p + 4);
    if (size > Q_LIMIT)
        qfail(c, "Yaz0 archive exceeds memory limit");
    QBytes out = {qa(c, size), size};
    size_t p = 16, q = 0;
    while (q < size) {
        need(c, b, p, 1);
        unsigned code = b.p[p++];
        for (int bit = 0; bit < 8 && q < size; bit++) {
            if (code & (128u >> bit)) {
                need(c, b, p, 1);
                out.p[q++] = b.p[p++];
            } else {
                need(c, b, p, 2);
                unsigned a = b.p[p++], z = b.p[p++], distance = ((a & 15) << 8 | z) + 1,
                         length = (a >> 4) + 2;
                if (!(a >> 4)) {
                    need(c, b, p, 1);
                    length = b.p[p++] + 18;
                }
                if (distance > q)
                    qfail(c, "Invalid Yaz0 backreference");
                while (length-- && q < size) {
                    out.p[q] = out.p[q - distance];
                    q++;
                }
            }
        }
    }
    return out;
}

typedef struct {
    QCtx *c;
    QBytes b;
    QArchive a;
    size_t ns, es, ss, ds, dl;
    unsigned nodes, entries;
    unsigned char *seen;
} Unpack;
/* Shift-JIS trail bytes can equal ASCII letters or a backslash. */
static int sjis_lead(unsigned char ch) {
    return (ch >= 0x81 && ch <= 0x9f) || (ch >= 0xe0 && ch <= 0xfc);
}
static int arc_safe(const char *s) {
    if (!s || !*s || *s == '/')
        return 0;
    const char *part = s;
    for (const unsigned char *p = (const unsigned char *)s;; p++) {
        if (sjis_lead(*p)) {
            if (!p[1])
                return 0;
            p++;
            continue;
        }
        if (*p == '\\' || *p == ':')
            return 0;
        if (*p == '/' || !*p) {
            size_t n = (const char *)p - part;
            if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.'))
                return 0;
            if (!*p)
                return 1;
            part = (const char *)p + 1;
        }
    }
}
static char *arc_string(Unpack *u, unsigned rel) {
    size_t p = u->ss + rel;
    if (p < u->ss || p >= u->ds)
        qfail(u->c, "Invalid RARC string offset");
    const unsigned char *end = memchr(u->b.p + p, 0, u->ds - p);
    if (!end)
        qfail(u->c, "Unterminated RARC name");
    char *s = qs(u->c, u->b.p + p,
                 (size_t)(end - u->b.p - p)); /* Resource names in the game's VFS are
                                                 case-insensitive ASCII. Preserve SJIS bytes. */
    for (char *t = s; *t; t++) {
        if (sjis_lead((unsigned char)*t)) {
            if (!t[1])
                qfail(u->c, "Truncated Shift-JIS name");
            t++;
        } else if ((unsigned char)*t < 128)
            *t = (char)tolower((unsigned char)*t);
    }
    return s;
}
static void arc_walk(Unpack *u, unsigned index, const char *prefix, unsigned depth) {
    if (depth > 128 || index >= u->nodes)
        qfail(u->c, "Invalid RARC directory index/depth");
    if (u->seen[index])
        qfail(u->c, "Cyclic or shared RARC directory");
    u->seen[index] = 1;
    size_t p = u->ns + index * 16;
    char *leaf = arc_string(u, qbe32(u->b.p + p + 4));
    if (!arc_safe(leaf) || strchr(leaf, '/'))
        qfail(u->c, "Unsafe RARC directory");
    char *path = *prefix ? qjoin(u->c, prefix, leaf) : leaf;
    unsigned count = qbe16(u->b.p + p + 10), first = qbe32(u->b.p + p + 12);
    if (first > u->entries || count > u->entries - first)
        qfail(u->c, "Invalid RARC file table");
    for (unsigned i = 0; i < count; i++) {
        size_t e = u->es + (first + i) * 20;
        unsigned flags = u->b.p[e + 4];
        char *name = arc_string(u, qbe32(u->b.p + e + 4) & 0xffffff);
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        if (!arc_safe(name) || strchr(name, '/'))
            qfail(u->c, "Unsafe RARC file name");
        if (flags & 2) {
            arc_walk(u, qbe32(u->b.p + e + 8), path, depth + 1);
            continue;
        }
        unsigned off = qbe32(u->b.p + e + 8), size = qbe32(u->b.p + e + 12);
        if (off > u->dl || size > u->dl - off)
            qfail(u->c, "RARC file exceeds data area");
        if (u->a.count >= 30000)
            qfail(u->c, "Too many archive files");
        QFile *f = &u->a.files[u->a.count++];
        f->path = qjoin(u->c, path, name);
        f->data = (QBytes){u->b.p + u->ds + off, size};
        f->id = qbe16(u->b.p + e);
        f->flags = flags;
        if (f->id != 65535 && u->a.next_id <= f->id)
            u->a.next_id = f->id + 1;
    }
}
static int filecmp(const void *a, const void *b) {
    return strcmp(((const QFile *)a)->path, ((const QFile *)b)->path);
}
QArchive qunpack(QCtx *c, QBytes raw) {
    QBytes b = qyaz(c, raw);
    need(c, b, 0, 96);
    if (memcmp(b.p, "RARC", 4))
        qfail(c, "Invalid RARC header/length");
    Unpack u = {.c = c, .b = b};
    u.ds = 32ull + qbe32(b.p + 12);
    u.dl = qbe32(b.p + 16);
    u.nodes = qbe32(b.p + 32);
    u.ns = 32ull + qbe32(b.p + 36);
    u.entries = qbe32(b.p + 40);
    u.es = 32ull + qbe32(b.p + 44);
    u.ss = 32ull + qbe32(b.p + 52);
    if (!u.nodes || u.nodes > 10000 || u.entries > 30000 || u.ss >= u.ds || !span(b, u.ds, u.dl) ||
        !span(b, u.ns, u.nodes * 16) || !span(b, u.es, u.entries * 20))
        qfail(c, "Invalid RARC offsets");
    u.seen = qa(c, u.nodes);
    u.a.files = qa(c, 30032 * sizeof(QFile));
    arc_walk(&u, 0, "", 0);
    qsort(u.a.files, u.a.count, sizeof(QFile), filecmp);
    for (size_t i = 1; i < u.a.count; i++)
        if (!strcmp(u.a.files[i - 1].path, u.a.files[i].path))
            qfail(c, "Duplicate RARC resource name");
    return u.a;
}
QFile *qunique(QCtx *c, QArchive *a, const char *suffix) {
    QFile *f = NULL;
    for (size_t i = 0; i < a->count; i++)
        if (!a->files[i].removed && qends(a->files[i].path, suffix)) {
            if (f)
                qfail(c, "Multiple archive resources matching %s", suffix);
            f = &a->files[i];
        }
    if (!f)
        qfail(c, "Missing archive resource %s", suffix);
    return f;
}
void qsetfile(QCtx *c, QArchive *a, const char *path, QBytes b) {
    for (size_t i = 0; i < a->count; i++)
        if (!strcmp(a->files[i].path, path)) {
            a->files[i].data = b;
            a->files[i].removed = 0;
            return;
        }
    if (a->count >= 30032 || a->next_id >= 65535)
        qfail(c, "Archive file count/ID overflow");
    a->files[a->count++] = (QFile){qs(c, path, strlen(path)), b, UINT32_MAX, 0x11, 0};
}
typedef struct {
    char *path;
    unsigned depth, first, count;
} Dir;
static int dircmp(const void *a, const void *b) {
    const Dir *x = a, *y = b;
    return x->depth == y->depth ? strcmp(x->path, y->path) : (x->depth < y->depth ? -1 : 1);
}
static int parent_is(const char *p, const char *parent) {
    const char *slash = strrchr(p, '/');
    return slash && (size_t)(slash - p) == strlen(parent) &&
           !memcmp(p, parent, (size_t)(slash - p));
}
static unsigned st(QBuf *s, const char *text) {
    size_t p = 0;
    while (p < s->n) {
        if (!strcmp((char *)s->p + p, text))
            return (unsigned)p;
        p += strlen((char *)s->p + p) + 1;
    }
    if (s->n + strlen(text) + 1 > 0x1000000)
        qfail(s->c, "RARC name table overflow");
    unsigned off = (unsigned)s->n;
    qb_add(s, text, strlen(text) + 1);
    return off;
}
static size_t align32(size_t x) { return (x + 31) & ~(size_t)31; }
static void add_entry(QBuf *e, QBuf *strings, unsigned id, unsigned flags, const char *name,
                      unsigned off, unsigned size) {
    unsigned char p[20] = {0};
    qput16(p, (uint16_t)id);
    qput16(p + 2, key((const unsigned char *)name, strlen(name)));
    qput32(p + 4, flags << 24 | st(strings, name));
    qput32(p + 8, off);
    qput32(p + 12, size);
    qb_add(e, p, 20);
}
QBytes qpack(QCtx *c, QArchive *a) {
    qsort(a->files, a->count, sizeof(QFile), filecmp);
    Dir *dirs = qa(c, 10000 * sizeof *dirs);
    size_t nd = 0;
    char *root = NULL;
    for (size_t i = 0; i < a->count; i++) {
        QFile *f = &a->files[i];
        if (f->removed)
            continue;
        if (!arc_safe(f->path))
            qfail(c, "Unsafe archive path");
        const char *slash = strchr(f->path, '/');
        if (!slash)
            qfail(c, "Archive file lacks root");
        if (!root)
            root = qs(c, f->path, (size_t)(slash - f->path));
        if (strlen(root) != (size_t)(slash - f->path) || memcmp(root, f->path, strlen(root)))
            qfail(c, "Archive has multiple roots");
        unsigned depth = 0;
        while (slash) {
            size_t len = (size_t)(slash - f->path), j;
            for (j = 0; j < nd; j++)
                if (strlen(dirs[j].path) == len && !memcmp(dirs[j].path, f->path, len))
                    break;
            if (j == nd) {
                if (nd >= 10000)
                    qfail(c, "Too many RARC directories");
                dirs[nd++] = (Dir){qs(c, f->path, len), depth, 0, 0};
            }
            slash = strchr(slash + 1, '/');
            depth++;
        }
    }
    if (!root)
        qfail(c, "Empty archive");
    qsort(dirs, nd, sizeof *dirs, dircmp);
    QBuf nodes = {.c = c}, entries = {.c = c}, strings = {.c = c};
    size_t data_size = 0; /* Layout first, copy data once. */
    size_t *offsets = qa(c, a->count * sizeof(size_t));
    for (size_t d = 0; d < nd; d++) {
        unsigned first = (unsigned)(entries.n / 20);
        const char *leaf = strrchr(dirs[d].path, '/');
        leaf = leaf ? leaf + 1 : dirs[d].path;
        for (size_t j = 0; j < nd; j++)
            if (parent_is(dirs[j].path, dirs[d].path))
                add_entry(&entries, &strings, 65535, 2, strrchr(dirs[j].path, '/') + 1, (unsigned)j,
                          16);
        for (size_t i = 0; i < a->count; i++) {
            QFile *f = &a->files[i];
            if (f->removed || !parent_is(f->path, dirs[d].path))
                continue;
            if (f->id == UINT32_MAX)
                f->id = a->next_id++;
            if (f->id >= 65535)
                qfail(c, "Invalid RARC file ID");
            data_size = align32(data_size);
            offsets[i] = data_size;
            if (f->data.n > Q_LIMIT - data_size)
                qfail(c, "Archive exceeds size limit");
            data_size += f->data.n;
            add_entry(&entries, &strings, f->id, (f->flags & ~0x60u) | 0x11,
                      strrchr(f->path, '/') + 1, (unsigned)offsets[i], (unsigned)f->data.n);
        }
        add_entry(&entries, &strings, 65535, 2, ".", (unsigned)d, 16);
        unsigned parent = UINT32_MAX;
        for (size_t j = 0; j < nd; j++)
            if (parent_is(dirs[d].path, dirs[j].path)) {
                parent = (unsigned)j;
                break;
            }
        add_entry(&entries, &strings, 65535, 2, "..", parent, 16);
        unsigned char n[16] = {0};
        memset(n, ' ', 4);
        if (!d)
            memcpy(n, "ROOT", 4);
        else
            for (size_t j = 0; j < 4 && leaf[j]; j++)
                n[j] = (unsigned char)toupper((unsigned char)leaf[j]);
        qput32(n + 4, st(&strings, leaf));
        qput16(n + 8, key((unsigned char *)leaf, strlen(leaf)));
        size_t count = entries.n / 20 - first;
        if (count > 65535)
            qfail(c, "Too many directory entries");
        qput16(n + 10, (uint16_t)count);
        qput32(n + 12, first);
        qb_add(&nodes, n, 16);
    }
    size_t no = 64, eo = align32(no + nodes.n), so = align32(eo + entries.n),
           da = align32(so + strings.n);
    data_size = align32(data_size);
    if (da > Q_LIMIT - data_size)
        qfail(c, "Archive exceeds size limit");
    QBytes out = {qa(c, da + data_size), da + data_size};
    memcpy(out.p, "RARC", 4);
    qput32(out.p + 4, (uint32_t)out.n);
    qput32(out.p + 8, 32);
    qput32(out.p + 12, (uint32_t)(da - 32));
    qput32(out.p + 16, (uint32_t)data_size);
    qput32(out.p + 20, (uint32_t)data_size);
    qput32(out.p + 32, (uint32_t)nd);
    qput32(out.p + 36, (uint32_t)(no - 32));
    qput32(out.p + 40, (uint32_t)(entries.n / 20));
    qput32(out.p + 44, (uint32_t)(eo - 32));
    qput32(out.p + 48, (uint32_t)strings.n);
    qput32(out.p + 52, (uint32_t)(so - 32));
    qput16(out.p + 56, (uint16_t)a->next_id);
    memcpy(out.p + no, nodes.p, nodes.n);
    memcpy(out.p + eo, entries.p, entries.n);
    memcpy(out.p + so, strings.p, strings.n);
    for (size_t i = 0; i < a->count; i++)
        if (!a->files[i].removed && a->files[i].data.n)
            memcpy(out.p + da + offsets[i], a->files[i].data.p, a->files[i].data.n);
    return out;
}
static void find_nodes(QNode *n, const char *type, const char *name, QNode **found,
                       unsigned *count) {
    if ((!type || !strcmp(type, n->type)) &&
        (!name || (n->name_len == strlen(name) && !memcmp(n->name, name, n->name_len)))) {
        *found = n;
        (*count)++;
    }
    for (size_t i = 0; i < n->count; i++)
        find_nodes(n->children[i], type, name, found, count);
}
static int has_type(QNode *n, const char *type) {
    QNode *f = NULL;
    unsigned count = 0;
    find_nodes(n, type, NULL, &f, &count);
    return count > 0;
}
QStages qstages(QCtx *c, QBytes b) {
    QNode *doc = qparse(c, b), *table = NULL;
    unsigned count = 0;
    find_nodes(doc, NULL, Q_STAGE_TABLE, &table, &count);
    if (!count)
        find_nodes(doc, "ScenarioArchiveNamesInStage", NULL, &table, &count);
    if (count > 1)
        qfail(c, "Ambiguous stage archive table");
    if (!table)
        table = doc;
    if (!table->container || table->count > 256)
        qfail(c, "Invalid stage area array");
    QStages out = {.slots = qa(c, 65536 * sizeof(QSlot))};
    int target = 0;
    for (size_t area = 0; area < table->count; area++) {
        QNode *group = table->children[area];
        if (!group->container || group->count > 256 ||
            (!strcmp(table->type, "ScenarioArchiveNamesInStage") &&
             strcmp(group->type, "ScenarioArchiveNameTable")))
            qfail(c, "Invalid stage episode array at area %zu", area);
        for (size_t ep = 0; ep < group->count; ep++) {
            QNode *n = group->children[ep];
            QBytes name;
            size_t p = 0;
            if (n->container || !str_at(n->payload, &p, &name) || p != n->payload.n || !name.n ||
                memchr(name.p, 0, name.n))
                qfail(c, "Invalid stage name at %zu/%zu", area, ep);
            char *s = qs(c, name.p, name.n);
            for (size_t k = 0; k < name.n; k++) {
                if ((unsigned char)s[k] > 127)
                    qfail(c, "Non-ASCII stage archive name");
                s[k] = (char)tolower((unsigned char)s[k]);
            }
            if (!qsafe(s))
                qfail(c, "Unsafe stage name");
            char *stem = strrchr(s, '/');
            stem = stem ? stem + 1 : s;
            char *dot = strrchr(stem, '.');
            if (dot)
                *dot = 0;
            out.slots[out.count++] = (QSlot){(unsigned)area, (unsigned)ep, stem};
            if (!strcmp(stem, "dolpic_ex0") && !target) {
                out.area = (unsigned)area;
                out.episode = (unsigned)ep;
                out.target = stem;
                target = 1;
            }
        }
    }
    if (!target || out.area < 16 || out.area > 254 || out.episode > 254)
        qfail(c, "Active game has no supported dolpic_ex0 stage slot");
    return out;
}
static int keep_node(QNode *n) {
    static const char *const core[] = {
        "Map",         "MapObjManager", "MapObjBaseManager", "ItemManager", "Sky",
        "SunMgr",      "SunModel",      "SunsetModel",       "Shimmer",     "Pollution",
        "StickyStain", "Mario",         "MarioPositionObj",  "Light",       "IdxLight",
        "AmbColor",    "normalLight",   "shadowLight",       "MLight",      "Camera",
        "PolarCamera", "MapDrawWall"};
    if (n->container) {
        size_t j = 0;
        for (size_t i = 0; i < n->count; i++)
            if (keep_node(n->children[i]))
                n->children[j++] = n->children[i];
        n->count = j;
        return 1;
    }
    if (in_list(n->type, core, sizeof core / sizeof *core))
        return 1;
    static const char *const parts[] = {"Camera", "Cube",  "Position", "Light",
                                        "Amb",    "Table", "Manager",  "Conductor"};
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++)
        if (strstr(n->type, parts[i]))
            return 1;
    return 0;
}
static void empty_cubes(QNode *n) {
    for (size_t i = 0; i < n->count; i++)
        empty_cubes(n->children[i]);
    if (n->container && (strstr(n->type, "Cube") || strstr(n->name, Q_CUBE)))
        n->count = 0;
}
static QNode *static_obj(QCtx *c, const char *name, const char *alias) {
    QNode *n = qa(c, sizeof *n);
    n->type = "MapStaticObj";
    n->name = qs(c, name, strlen(name));
    n->name_len = strlen(name);
    QBuf b = {.c = c};
    for (int i = 0; i < 9; i++)
        qb_u32(&b, i >= 6 ? 0x3f800000 : 0);
    qb_str(&b, "", 0);
    qb_u32(&b, 0);
    qb_str(&b, alias, strlen(alias));
    n->payload = qb_bytes(&b);
    return n;
}
#include "asset_hashes.h"
static QBytes asset(QCtx *c, const char *folder, const char *name) {
    size_t i;
    for (i = 0; i < sizeof q_assets / sizeof *q_assets; i++)
        if (!strcmp(q_assets[i].name, name))
            break;
    if (i == sizeof q_assets / sizeof *q_assets)
        qfail(c, "Unknown mod asset: %s", name);
    QBytes b = qread(c, qjoin(c, folder, name), Q_LIMIT, 0);
    char hash[65];
    qsha(b, hash);
    if (b.n != q_assets[i].size || strcmp(hash, q_assets[i].sha256))
        qfail(c, "Damaged mod asset: %s. Re-extract the full ZIP.", name);
    return b;
}
QBytes qbuild(QCtx *c, QBytes raw, const char *assets, QBytes common) {
    QArchive a = qunpack(c, raw);
    QFile *scene_file = qunique(c, &a, "/map/scene.bin");
    char *scene_path = scene_file->path;
    QNode *scene = qparse(c, scene_file->data);
    QBytes round = qencode(c, scene);
    if (round.n != scene_file->data.n || memcmp(round.p, scene_file->data.p, round.n))
        qfail(c, "Scaffold scene did not round-trip");
    QNode *map = NULL, *group = NULL;
    unsigned maps = 0, groups = 0;
    find_nodes(scene, "Map", NULL, &map, &maps);
    if (maps != 1)
        qfail(c, "Expected one Map record; found %u", maps);
    /* Validate variable translucent groups before changing collision capacities. */
    need(c, map->payload, 0, 4);
    unsigned ng = qbe32(map->payload.p);
    if (ng > 64)
        qfail(c, "Unsupported translucent groups");
    size_t off = 4;
    for (unsigned i = 0; i < ng; i++) {
        need(c, map->payload, off, 4);
        unsigned n = qbe32(map->payload.p + off);
        if (n > 10000)
            qfail(c, "Invalid translucent group");
        off += 4;
        need(c, map->payload, off, (size_t)n * 8);
        off += (size_t)n * 8;
    }
    need(c, map->payload, off, 24);
    if (!keep_node(scene))
        qfail(c, "Unsupported scaffold scene root");
    int manager = has_type(scene, "MapObjManager");
    if (common.n)
        manager |= has_type(qparse(c, common), "MapObjManager");
    for (size_t i = 0; i < a.count; i++)
        if (qends(a.files[i].path, "/map/tables.bin"))
            manager |= has_type(qparse(c, a.files[i].data), "MapObjManager");
    if (!manager)
        qfail(c, "Scaffold lacks MapObjManager");
    find_nodes(scene, NULL, Q_MAP_GROUP, &group, &groups);
    if (groups != 1 || !group->container)
        qfail(c, "Scaffold lacks a unique native map group");
    map->container = 0;
    map->count = 0;
    QBuf mp = {.c = c};
    const uint32_t caps[] = {0, 128, 128, 1000000, 5000000, 20000, 0};
    for (size_t i = 0; i < 7; i++)
        qb_u32(&mp, caps[i]);
    map->payload = qb_bytes(&mp);
    char *root = qs(c, scene_path, (size_t)(strchr(scene_path, '/') - scene_path));
    static const char *const aliases[] = {"riccoSea",
                                          "riccoSeaPollutionS0",
                                          "riccoSeaPollutionS1",
                                          "riccoSeaPollutionS2",
                                          "riccoSeaPollutionS3",
                                          "riccoSeaPollutionS4",
                                          "mareSeaPollutionS0",
                                          "mareSeaPollutionS12",
                                          "mareSeaPollutionS34567"};
    QNode **kids = qa(c, (group->count + 9) * sizeof *kids);
    if (group->count)
        memcpy(kids, group->children, group->count * sizeof *kids);
    group->children = kids;
    QBytes stub = asset(c, assets, "collision_stub.bmd");
    for (size_t i = 0; i < 9; i++) {
        group->children[group->count++] = static_obj(
            c, i ? qfmt(c, "QuahogCollision%zu", i + 1) : "QuahogNativeReady", aliases[i]);
        char *alias = qs(c, aliases[i], strlen(aliases[i]));
        for (char *p = alias; *p; p++)
            *p = (char)tolower((unsigned char)*p);
        qsetfile(c, &a, qfmt(c, "%s/map/map/%s.col", root, alias),
                 asset(c, assets, qfmt(c, "quahog_%02zu.col", i + 1)));
        if (i != 0 && i != 8)
            qsetfile(c, &a, qfmt(c, "%s/map/map/%s.bmd", root, alias), stub);
    }
    qsetfile(c, &a, qfmt(c, "%s/map/map.col", root), asset(c, assets, "quahog_00.col"));
    qsetfile(c, &a, qfmt(c, "%s/map/map/map.bmd", root), asset(c, assets, "quahog.bmd"));
    for (size_t i = 0; i < a.count; i++) {
        QFile *f = &a.files[i];
        if (strstr(f->path, "/map/sp/") ||
            (strstr(f->path, "/map/map/map.") && !qends(f->path, ".bmd")))
            f->removed = 1;
        else if (qends(f->path, "/map/tables.bin")) {
            QNode *n = qparse(c, f->data);
            empty_cubes(n);
            f->data = qencode(c, n);
        }
    }
    empty_cubes(scene);
    qsetfile(c, &a, scene_path, qencode(c, scene));
    QBytes result =
        qpack(c, &a); /* Check each output resource against its source bytes, IDs and flags. */
    QArchive verify = qunpack(c, result);
    size_t count = 0;
    for (size_t i = 0; i < a.count; i++)
        if (!a.files[i].removed) {
            if (count >= verify.count || strcmp(a.files[i].path, verify.files[count].path) ||
                a.files[i].data.n != verify.files[count].data.n ||
                memcmp(a.files[i].data.p, verify.files[count].data.p, a.files[i].data.n) ||
                a.files[i].id != verify.files[count].id)
                qfail(c, "Prepared archive verification failed");
            count++;
        }
    if (count != verify.count)
        qfail(c, "Prepared archive resource count mismatch");
    return result;
}
/* Collision-derived market portal, preserving the previous placement algorithm. */
typedef struct {
    double x, y, z;
} V;
static V sub(V a, V b) { return (V){a.x - b.x, a.y - b.y, a.z - b.z}; }
static double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V cross(V a, V b) {
    return (V){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static float bf(const unsigned char *p) {
    uint32_t n = qbe32(p);
    float f;
    memcpy(&f, &n, 4);
    return f;
}
static V vertex(const unsigned char *p) { return (V){bf(p), bf(p + 4), bf(p + 8)}; }
static double ray(V o, V d, V a, V b, V c) {
    V e1 = sub(b, a), e2 = sub(c, a), h = cross(d, e2);
    double det = dot(e1, h);
    if (fabs(det) < 1e-6)
        return -1;
    V s = sub(o, a);
    double u = dot(s, h) / det;
    if (u < 0 || u > 1)
        return -1;
    V q = cross(s, e1);
    double v = dot(d, q) / det;
    if (v < 0 || u + v > 1)
        return -1;
    double t = dot(e2, q) / det;
    return t > 0 ? t : -1;
}
static int contains_ci(QBytes b, const char *s) {
    size_t n = strlen(s);
    for (size_t i = 0; i + n <= b.n; i++) {
        size_t j = 0;
        while (j < n && tolower(b.p[i + j]) == tolower((unsigned char)s[j]))
            j++;
        if (j == n)
            return 1;
    }
    return 0;
}
static void fruits(QCtx *c, QNode *n, V *a, size_t *na, V *b, size_t *nb) {
    if (n->payload.n >= 36 &&
        (!strcmp(n->type, "Fruit") || !strcmp(n->type, "ResetFruit") ||
         !strcmp(n->type, "MapObjBase") || !strcmp(n->type, "MapObjGeneral"))) {
        V v = vertex(n->payload.p);
        if (isfinite(v.x) && isfinite(v.y) && isfinite(v.z) && fabs(v.x) < 50000 &&
            fabs(v.y) < 50000 && fabs(v.z) < 50000) {
            if (contains_ci(n->payload, "Fruitcoconut")) {
                if (*na >= 1024)
                    qfail(c, "Too many market anchors");
                a[(*na)++] = v;
            }
            if (contains_ci(n->payload, "Fruitpapaya")) {
                if (*nb >= 1024)
                    qfail(c, "Too many market anchors");
                b[(*nb)++] = v;
            }
        }
    }
    for (size_t i = 0; i < n->count; i++)
        fruits(c, n->children[i], a, na, b, nb);
}
static double cast_col(QCtx *c, QBytes col, V origin, V direction, int wall) {
    need(c, col, 0, 16);
    unsigned nv = qbe32(col.p), vo = qbe32(col.p + 4), ng = qbe32(col.p + 8),
             go = qbe32(col.p + 12);
    if (nv > 1000000 || ng > 100000)
        qfail(c, "Invalid collision header");
    need(c, col, vo, (size_t)nv * 12);
    need(c, col, go, (size_t)ng * 24);
    double nearest = 1e100;
    for (unsigned i = 0; i < ng; i++) {
        const unsigned char *g = col.p + go + i * 24;
        int count = (int16_t)qbe16(g + 2);
        unsigned off = qbe32(g + 8);
        if (count < 0)
            qfail(c, "Invalid collision face count");
        need(c, col, off, (size_t)count * 6);
        for (int j = 0; j < count; j++) {
            V t[3];
            for (int k = 0; k < 3; k++) {
                int ix = (int16_t)qbe16(col.p + off + j * 6 + k * 2);
                if (ix < 0 || (unsigned)ix >= nv)
                    qfail(c, "Invalid collision vertex index");
                t[k] = vertex(col.p + vo + ix * 12);
                if (!isfinite(t[k].x) || !isfinite(t[k].y) || !isfinite(t[k].z))
                    qfail(c, "Non-finite collision vertex");
            }
            if (wall) {
                V n = cross(sub(t[1], t[0]), sub(t[2], t[0]));
                double norm = sqrt(dot(n, n));
                if (norm < 60000 || fabs(n.y) > norm * .3)
                    continue;
            }
            double h = ray(origin, direction, t[0], t[1], t[2]);
            if (h > (wall ? 80 : 0) && h < (wall ? 1100 : 350) && h < nearest)
                nearest = h;
        }
    }
    return nearest;
}
QLocation qmarket(QCtx *c, QBytes raw) {
    QArchive a = qunpack(c, raw);
    QNode *scene = qparse(c, qunique(c, &a, "/map/scene.bin")->data);
    V *co = qa(c, 1024 * sizeof(V)), *pa = qa(c, 1024 * sizeof(V));
    size_t nc = 0, np = 0;
    fruits(c, scene, co, &nc, pa, &np);
    double best = 1e100;
    V x = {0}, y = {0};
    for (size_t i = 0; i < nc; i++)
        for (size_t j = 0; j < np; j++) {
            V d = sub(co[i], pa[j]);
            double dist = sqrt(dot(d, d));
            if (fabs(d.y) < 100 && dist < best) {
                best = dist;
                x = co[i];
                y = pa[j];
            }
        }
    if (best <= 150 || best >= 2200)
        return (QLocation){0};
    V mid = {(x.x + y.x) * .5, (x.y + y.y) * .5, (x.z + y.z) * .5}, along = sub(y, x);
    double len = hypot(along.x, along.z);
    if (len < 1)
        return (QLocation){0};
    V d = {-along.z / len, 0, along.x / len}, o = {mid.x, mid.y + 190, mid.z};
    QBytes col = qunique(c, &a, "/map/map.col")->data;
    double h = cast_col(c, col, o, d, 1);
    V opposite = {-d.x, 0, -d.z};
    double other = cast_col(c, col, o, opposite, 1);
    if (other < h) {
        d = opposite;
        h = other;
    }
    if (h > 1100)
        return (QLocation){0};
    V p = {o.x + d.x * (h - 75), mid.y + 120, o.z + d.z * (h - 75)};
    double floor = cast_col(c, col, p, (V){0, -1, 0}, 0);
    if (floor > 350)
        return (QLocation){0};
    return (QLocation){(float)p.x, (float)(p.y - floor), (float)p.z,
                       (float)(atan2(-d.x, -d.z) * 180 / 3.14159265358979323846), 1};
}
