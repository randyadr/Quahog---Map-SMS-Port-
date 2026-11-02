/* Quahog native C11 implementation. Author: Randyadr. */
#ifndef QCORE_H
#define QCORE_H
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define Q_VERSION "0.5.1"
#define Q_LIMIT (256u * 1024u * 1024u)
#define Q_PATH 4096
#define Q_MAP_GROUP "\x83\x7d\x83\x62\x83\x76\x83\x4f\x83\x8b\x81\x5b\x83\x76"
#define Q_CUBE "\x83\x4c\x83\x85\x81\x5b\x83\x75"
#define Q_STAGE_TABLE                                                                              \
    "\x83\x58\x83\x65\x81\x5b\x83\x57\x96\x88\x83\x56\x83\x69\x83\x8a\x83\x49\x83\x41\x81\x5b\x83" \
    "\x4a\x83\x43\x83\x75\x96\xbc\x8c\x51"
typedef struct QAlloc QAlloc;
typedef struct QMapped QMapped;
typedef struct {
    QAlloc *alloc;
    QMapped *maps;
    size_t allocated;
    jmp_buf failure;
    char error[2048];
} QCtx;
typedef struct {
    unsigned char *p;
    size_t n;
} QBytes;
typedef struct {
    unsigned char *p;
    size_t n, cap;
    QCtx *c;
} QBuf;
typedef struct QNode {
    char *type, *name;
    size_t name_len;
    QBytes payload, prefix;
    struct QNode **children;
    size_t count;
    int container;
} QNode;
typedef struct {
    char *path;
    QBytes data;
    unsigned id, flags;
    int removed;
} QFile;
typedef struct {
    QFile *files;
    size_t count;
    unsigned next_id;
} QArchive;
typedef struct {
    unsigned area, episode;
    char *stem;
} QSlot;
typedef struct {
    QSlot *slots;
    size_t count;
    unsigned area, episode;
    char *target;
} QStages;
typedef struct {
    float x, y, z, yaw;
    int valid;
} QLocation;
typedef struct {
    void *(*read)(const char *, size_t *);
    int (*replace)(const char *, const void *, uint32_t);
    void (*release)(void *);
    uint32_t *memory_size;
} QHost;
_Noreturn void qfail(QCtx *, const char *, ...);
void *qa(QCtx *, size_t);
void qclear(QCtx *);
int qmerge(QCtx *, QCtx *);
char *qs(QCtx *, const void *, size_t);
char *qfmt(QCtx *, const char *, ...);
int qends(const char *, const char *);
int qsafe(const char *);
uint16_t qbe16(const unsigned char *);
uint32_t qbe32(const unsigned char *);
void qput16(unsigned char *, uint16_t);
void qput32(unsigned char *, uint32_t);
uint64_t qfnv(QBytes);
void qsha(QBytes, char out[65]);
void qb_add(QBuf *, const void *, size_t);
void qb_u32(QBuf *, uint32_t);
void qb_str(QBuf *, const void *, size_t);
QBytes qb_bytes(QBuf *);
QBytes qread(QCtx *, const char *, size_t, int);
/* Read-only virtual mapping, with 64-bit file sizes, released by qclear. */
QBytes qmap_readonly(QCtx *, const char *);
FILE *qopen(const char *, const char *);
int qexists(const char *);
void qmkdir(QCtx *, const char *);
void qwrite(QCtx *, const char *, QBytes);
void qatomic(QCtx *, const char *, QBytes);
void qremove(const char *);
char *qjoin(QCtx *, const char *, const char *);
QBytes qhost_read(QCtx *, QHost, const char *, int);
QBytes qyaz(QCtx *, QBytes);
QNode *qparse(QCtx *, QBytes);
QBytes qencode(QCtx *, QNode *);
QArchive qunpack(QCtx *, QBytes);
QBytes qpack(QCtx *, QArchive *);
QFile *qunique(QCtx *, QArchive *, const char *);
void qsetfile(QCtx *, QArchive *, const char *, QBytes);
QStages qstages(QCtx *, QBytes);
QBytes qbuild(QCtx *, QBytes, const char *, QBytes);
QLocation qmarket(QCtx *, QBytes);
uint32_t qpe_symbol(QCtx *, QBytes, const char *, int);
QHost qresolve(QCtx *);
int qmemory(QCtx *, uint32_t, int, const char *);
/* Returns 1 if changed, 0 if existing setting is already sufficient. */
int qmemory_text(QCtx *, QBytes, QBytes *);
typedef struct {
    char path[256];
    const void *original, *prepared;
    uint32_t old_size, new_size;
    int active;
} QOverride;
void qrestore(QOverride *, QHost);
void qprepare(QCtx *, QHost, const char *, const char *, QOverride *, QStages *, QLocation *);
#endif
