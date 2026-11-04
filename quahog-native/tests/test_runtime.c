/* Actual plugin with a fake host and authored game files. */
#if defined(LEGACY_13)
#include "compat_sdk/sms_mod_1_3.h"
#elif defined(LEGACY_14)
#include "compat_sdk/sms_mod_1_4.h"
#else
#include "sms_mod.h"
#endif
#include "qcore.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
struct SmsMod {
    int unused;
};
#ifdef QUAHOG_DYNAMIC_TEST
#include <dlfcn.h>
static int (*sms_mod_load)(SmsMod *, const SmsApi *);
static void (*sms_mod_unload)(SmsMod *);
static void *plugin_handle;
static const char *plugin_path;
static void open_plugin(void) {
    plugin_handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
    if (!plugin_handle) { fprintf(stderr, "%s\n", dlerror()); abort(); }
    sms_mod_load = (int (*)(SmsMod *, const SmsApi *))dlsym(plugin_handle, "sms_mod_load");
    sms_mod_unload = (void (*)(SmsMod *))dlsym(plugin_handle, "sms_mod_unload");
    assert(sms_mod_load && sms_mod_unload);
    uint32_t (*version)(void) = (uint32_t (*)(void))dlsym(plugin_handle, "sms_mod_api_version");
    assert(version && version() == (1u << 16));
}
#else
extern int sms_mod_load(SmsMod *, const SmsApi *);
extern void sms_mod_unload(SmsMod *);
#endif
static char game_folder[Q_PATH], mod_folder[Q_PATH], data_folder[Q_PATH];
static SmsMod mod;
static SmsEventFn callbacks[256];
static SmsOption options[4];
static int noptions;
static SmsGameState state;
static SmsPlayer player;
static int warps, requested_area, requested_episode, markers = 1, fail_event, triangles, lines,
                                                     reads, overrides;
#ifdef QUAHOG_DYNAMIC_TEST
uint32_t port_mem1_size = 64u * 1024 * 1024;
#define memory_size port_mem1_size
#else
static uint32_t memory_size = 64u * 1024 * 1024;
#endif
static const void *mapped;
static uint32_t mapped_size;
static const char *target = "/data/scene/dolpic_ex0.szs";
static void *read_game(const char *path, size_t *n) {
    reads++;
    if (mapped && !strcmp(path, target)) {
        void *p = malloc(mapped_size);
        memcpy(p, mapped, mapped_size);
        *n = mapped_size;
        return p;
    }
    char file[Q_PATH];
    assert(snprintf(file, sizeof file, "%s%s", game_folder, path) < (int)sizeof file);
    FILE *f = qopen(file, "rb");
    if (!f)
        return NULL;
    assert(!fseek(f, 0, SEEK_END));
    long len = ftell(f);
    rewind(f);
    assert(len >= 0);
    void *p = malloc(len ? ((size_t)len) : 1);
    assert(fread(p, 1, (size_t)len, f) == (size_t)len);
    fclose(f);
    *n = (size_t)len;
    return p;
}
static int replace_game(const char *path, const void *p, uint32_t n) {
    assert(!strcmp(path, target));
    mapped = p;
    mapped_size = n;
    overrides++;
    return 1;
}
QHost quahog_test_host(void) { return (QHost){read_game, replace_game, free, &memory_size}; }
#ifdef QUAHOG_DYNAMIC_TEST
void *port_dvd_read_file(const char *path, size_t *size) { return read_game(path, size); }
int port_dvd_override(const char *path, const void *p, uint32_t n) { return replace_game(path, p, n); }
#endif
static int subscribe(SmsMod *m, unsigned t, SmsEventFn fn, void *u, int priority) {
    (void)m;
    (void)u;
    (void)priority;
    if ((int)t == fail_event)
        return 0;
    assert(t < 256);
    callbacks[t] = fn;
    return (int)t;
}
static void unsubscribe(SmsMod *m, int h) {
    (void)m;
    assert(h > 0 && h < 256);
    callbacks[h] = NULL;
}
static void logger(SmsMod *m, int level, const char *format, ...) {
    (void)m;
    va_list a;
    va_start(a, format);
    if (level == SMS_LOG_ERROR) {
        vfprintf(stderr, format, a);
        fputc('\n', stderr);
    }
    va_end(a);
}
static const char *mod_dir(SmsMod *m) {
    (void)m;
    return mod_folder;
}
static const char *data_dir(SmsMod *m) {
    (void)m;
    return data_folder;
}
static void get_state(SmsGameState *out) { *out = state; }
static SmsObject find_object(const char *name) {
    return markers && !strcmp(name, "QuahogNativeReady") ? &mod : NULL;
}
static int warp(int a, int e) {
    warps++;
    requested_area = a;
    requested_episode = e;
    return 1;
}
static int get_player(SmsPlayer *out) {
    *out = player;
    return player.valid;
}
static int position(const SmsVec3 *p) {
    player.position = *p;
    return 1;
}
static int velocity(const SmsVec3 *p) {
    player.velocity = *p;
    return 1;
}
static int speed(float f) {
    player.forward_speed = f;
    return 1;
}
#if SMS_MOD_API_MINOR >= 5
static int yaw(float f) {
    player.yaw = f;
    return 1;
}
static int fludd(int f) {
    player.has_fludd = f;
    return 1;
}
static int nozzle(int f, int unused) {
    (void)unused;
    player.nozzle = f;
    return 1;
}
#endif
static int bind_key(SmsMod *m, const char *name, const char *key) {
    (void)m;
    (void)name;
    return atoi(key + 1);
}
static int add_option(SmsMod *m, const SmsOption *o) {
    (void)m;
    assert(noptions < 4);
    options[noptions++] = *o;
    return 1;
}
static void triangle(float a, float b, float c, float d, float e, float f, SmsColor color) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)color;
    triangles++;
}
static void line(float a, float b, float c, float d, float width, SmsColor color) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)width;
    (void)color;
    lines++;
}
static int project(const SmsVec3 *p, float *x, float *y, float *depth) {
    *x = p->x * .1f;
    *y = p->y * .1f;
    *depth = 1000;
    return 1;
}
static void emit(unsigned t, void *data, unsigned size) {
    SmsEvent e = {t, size, data, 0, NULL};
    if (callbacks[t])
        callbacks[t](&mod, &e, NULL);
}
static void tick(float seconds) {
    for (int i = 0; i < (int)(seconds * 60 + 1); i++) {
        SmsEvTick t = {0};
        t.dt = 1.f / 60;
        emit(SMS_EV_TICK, &t, sizeof t);
    }
}
static void key(int code) {
    SmsEvKey k = {0};
    k.down = 1;
    k.scancode = code;
    emit(SMS_EV_KEY, &k, sizeof k);
}
static void action(const char *name) {
    for (int i = 0; i < noptions; i++)
        if (!strcmp(options[i].key, name)) {
            options[i].on_change(&mod, name, NULL);
            return;
        }
    assert(0);
}
static void load_stage(int area, int episode) {
    state.area = area;
    state.episode = episode;
    state.stage_ready = 1;
#if SMS_MOD_API_MINOR >= 5
    SmsEvStageLoading loading = {0};
    loading.area = area;
    loading.episode = episode;
    emit(SMS_EV_STAGE_LOADING, &loading, sizeof loading);
    SmsEvPlayerSpawn p = {0};
    emit(SMS_EV_PLAYER_SPAWN, &p, sizeof p);
    if (p.has_position)
        player.position = p.position;
#endif
    emit(SMS_EV_STAGE_LOADED, NULL, 0);
#if SMS_MOD_API_MINOR >= 4
    emit(SMS_EV_STAGE_START, NULL, 0);
#endif
}
static void unload(void) {
    sms_mod_unload(&mod);
    noptions = 0;
    for (int i = 0; i < 256; i++)
        assert(!callbacks[i]);
#ifdef QUAHOG_DYNAMIC_TEST
    assert(!dlclose(plugin_handle));
    open_plugin();
#endif
}
int main(int argc, char **argv) {
#ifdef QUAHOG_DYNAMIC_TEST
    assert(argc == 5);
    plugin_path = argv[4];
    open_plugin();
#ifdef QUAHOG_HIDDEN_HOST
    assert(!dlsym(RTLD_MAIN_ONLY, "port_dvd_read_file"));
#endif
#else
    assert(argc == 4);
#endif
    assert(strlen(argv[1]) < sizeof game_folder && strlen(argv[2]) < sizeof mod_folder &&
           strlen(argv[3]) < sizeof data_folder);
    strcpy(game_folder, argv[1]);
    strcpy(mod_folder, argv[2]);
    strcpy(data_folder, argv[3]);
    SmsCoreApi core = {0};
    core.size = sizeof core;
    core.log = logger;
    core.subscribe = subscribe;
    core.unsubscribe = unsubscribe;
    core.mod_dir = mod_dir;
    core.data_dir = data_dir;
    SmsGameApi game = {0};
    game.size = sizeof game;
    game.state = get_state;
    game.find_object = find_object;
    game.warp = warp;
    SmsPlayerApi p = {0};
    p.size = sizeof p;
    p.get = get_player;
    p.set_position = position;
    p.set_velocity = velocity;
    p.set_forward_speed = speed;
#if SMS_MOD_API_MINOR >= 5
    p.set_yaw = yaw;
    p.set_fludd = fludd;
    p.set_nozzle = nozzle;
#endif
    SmsInputApi input = {0};
    input.size = sizeof input;
    input.bind_key = bind_key;
    SmsConfigApi config = {0};
    config.size = sizeof config;
    config.add_option = add_option;
    SmsDrawApi draw = {0};
    draw.size = sizeof draw;
    draw.triangle = triangle;
    draw.line = line;
    SmsCameraApi camera = {0};
    camera.size = sizeof camera;
    camera.world_to_canvas = project;
    SmsApi api = {0};
    api.size = sizeof api;
    api.major = 1;
    api.minor = SMS_MOD_API_MINOR;
    api.core = &core;
    api.game = &game;
    api.player = &p;
    api.input = &input;
    api.config = &config;
    api.draw = &draw;
    api.camera = &camera;
    state.size = sizeof state;
    state.area = 1;
    state.episode = 3;
    player.valid = 1;
    assert(sms_mod_load(NULL, NULL) == 1);
    uint32_t tiny = 4;
    assert(sms_mod_load(&mod, (const SmsApi *)&tiny) == 1);
    api.major = 2;
    assert(sms_mod_load(&mod, &api) == 1);
    api.major = 1;
    game.find_object = NULL;
    assert(sms_mod_load(&mod, &api) == 1);
    game.find_object = find_object;
    camera.world_to_canvas = NULL;
    assert(sms_mod_load(&mod, &api) == 1);
    camera.world_to_canvas = project;
    assert(sms_mod_load(&mod, &api) == 2);
    memory_size = 1024u * 1024 * 1024;
    assert(sms_mod_load(&mod, &api) == 0);
    assert(mapped_size > 59000000 && !memcmp(mapped, "RARC", 4));
    int count = reads;
    load_stage(1, 3);
    tick(4);
    state.paused = 1;
    player.position = (SmsVec3){1100, 20, 2100};
    player.yaw = 90;
    action("place");
    tick(10);
    assert(warps == 0);
    state.paused = 0;
    tick(7);
    assert(warps == 0);
    player.position = (SmsVec3){1400, 20, 2100};
    tick(2);
    emit(SMS_EV_DRAW, NULL, 0);
    assert(triangles > 0 && lines > 0);
    state.paused = 1;
    action("enter");
    tick(1);
    assert(warps == 0);
    state.paused = 0;
    tick(.1f);
    assert(warps == 1 && requested_area == 23 && requested_episode == 1);
    load_stage(23, 1);
    assert(player.position.y > 8000);
    tick(4);
    player.position.y = 6000;
    tick(.1f);
    assert(player.position.y > 8000);
    tick(4);
    key(8);
    assert(warps == 2 && requested_area == 1 && requested_episode == 3);
    load_stage(1, 3);
    assert(player.position.x < 1100 && fabsf(player.position.z - 2100) < .01f);
    tick(4);
    player.position = (SmsVec3){1100, 20, 2100};
    tick(.1f);
    assert(warps == 3);
    load_stage(23, 1);
    tick(4);
    player.position = (SmsVec3){-157.27223f, 7995.334f, -892.7065f};
    tick(.1f);
    assert(warps == 4);
    assert(reads == count); /* No file reads during gameplay. */
    const void *first_prepared = mapped;
    unload();
    assert(!memcmp(mapped, "Yaz0", 4));
    load_stage(1, 3);
    assert(sms_mod_load(&mod, &api) == 0);
    assert(mapped == first_prepared); /* Registry survives actual dlclose/reopen on Mac. */
    tick(4);
    player.position = (SmsVec3){1100, 20, 2100};
    int old = warps;
    tick(.1f);
    assert(warps == old + 1);
    unload();
    fail_event = SMS_EV_TICK;
    assert(sms_mod_load(&mod, &api) == 4);
    assert(!memcmp(mapped, "Yaz0", 4));
    for (int i = 0; i < 256; i++)
        assert(!callbacks[i]);
    unload();
    printf("API 1.%u: native prepare/cache, render, travel, return episode, pause, placement "
           "reload, fall rescue, no background reads and unload rollback passed (%d overrides).\n",
           api.minor, overrides);
    return 0;
}
