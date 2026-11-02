/* Native C11 map plugin. Author: Randyadr. No Python or C++ runtime. */
#include "qcore.h"
#include "qportal.h"
#include "sms_mod.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static const SmsApi *api;
static SmsMod *self;
static QHost native_host;
static QOverride world;
static int handles[8], nhandles;
static int target_area = -1, target_episode = -1, current_area = -1, current_episode = -1;
static int return_episode, arrival, key_enter, key_return, key_rescue;
static float cooldown = 2, warning_time;
static int ready, started, placement_notice, return_valid, pending_action;
static float saved_return_yaw = 180;
static SmsVec3 return_position;
static const SmsVec3 spawn = {-157.27223f, 8062.334f, -317.7065f}, zero = {0};
static QPortal entrance, exit_gate;
static char diagnostic_path[Q_PATH], placement_path[Q_PATH];
SMS_MOD_EXPORT uint32_t sms_mod_api_version(void) { return 1u << 16; }
SMS_MOD_EXPORT uint32_t sms_mod_api_prerelease(void) { return 0; }
static int api_header(const SmsApi *h) {
    return h && h->size >= offsetof(SmsApi, minor) + sizeof h->minor;
}
static const char *api_missing(const SmsApi *h) {
    if (!api_header(h))
        return "The game did not supply a complete native API header.";
    if (h->major != 1)
        return "This mod requires native mod API major version 1.";
#define REQUIRE(table, member)                                                                     \
    if (!SMS_API_HAS(h, table) || !SMS_API_HAS(h->table, member))                                  \
        return "Required native API function missing: " #table "." #member;
    REQUIRE(core, log)
    REQUIRE(core, subscribe)
    REQUIRE(core, unsubscribe)
    REQUIRE(core, mod_dir) REQUIRE(core, data_dir) REQUIRE(game, state) REQUIRE(game, find_object)
        REQUIRE(game, warp) REQUIRE(player, get) REQUIRE(player, set_position)
            REQUIRE(player, set_velocity) REQUIRE(player, set_forward_speed)
                REQUIRE(input, bind_key) REQUIRE(config, add_option)
#undef REQUIRE
                    return NULL;
}
static void diagnostic(SmsMod *mod, const SmsApi *host, int level, const char *message) {
    if (!api_header(host) || host->major != 1 || !SMS_API_HAS(host, core))
        return;
    if (SMS_API_HAS(host->core, log))
        host->core->log(mod, level, "%s", message);
    if (!diagnostic_path[0] && SMS_API_HAS(host->core, data_dir)) {
        const char *data = host->core->data_dir(mod);
        if (data && *data) {
            int n = snprintf(diagnostic_path, sizeof diagnostic_path, "%s/startup.log", data);
            if (n < 0 || n >= (int)sizeof diagnostic_path)
                diagnostic_path[0] = 0;
        }
    }
    if (diagnostic_path[0]) {
        FILE *f = qopen(diagnostic_path, "ab");
        if (f) {
            fprintf(f, "%s\n", message);
            fclose(f);
        }
    }
}
static int refuse(SmsMod *mod, const SmsApi *host, int code, const char *reason) {
    char message[8192];
    if (api_header(host))
        snprintf(message, sizeof message,
                 "Quahog " Q_VERSION " - native C - API %u.%u\n%s\n\nDetails: %s", host->major,
                 host->minor, reason, diagnostic_path);
    else
        snprintf(message, sizeof message, "Quahog " Q_VERSION "\n%s", reason);
    diagnostic(mod, host, SMS_LOG_ERROR, message);
#if defined(_WIN32) && !defined(QUAHOG_TEST)
    wchar_t w[8192];
    if (MultiByteToWideChar(CP_UTF8, 0, message, -1, w, 8192))
        MessageBoxW(NULL, w, L"Quahog startup", MB_OK | MB_ICONINFORMATION);
#endif
    return code;
}
static int finite_position(SmsVec3 p) {
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z) && fabsf(p.x) < 100000 &&
           fabsf(p.y) < 100000 && fabsf(p.z) < 100000;
}
static int in_world(void) {
    return current_area == target_area && current_episode == target_episode;
}
static int near_portal(SmsVec3 a, QPortal p) {
    if (!p.valid)
        return 0;
    float yaw = p.yaw * Q_PI / 180, x = a.x - p.base.x, z = a.z - p.base.z;
    float across = cosf(yaw) * x - sinf(yaw) * z, depth = sinf(yaw) * x + cosf(yaw) * z;
    return fabsf(across) < 118 && fabsf(depth) < 110 && a.y > p.base.y - 90 && a.y < p.base.y + 320;
}
static void locate(void) {
    SmsGameState s = {.size = sizeof s};
    api->game->state(&s);
    current_area = s.area;
    current_episode = s.episode;
    ready = s.stage_ready &&
            (current_area == 1 || (in_world() && api->game->find_object("QuahogNativeReady")));
}
static void move_to(SmsVec3 p, float yaw) {
    api->player->set_position(&p);
    api->player->set_velocity(&zero);
    api->player->set_forward_speed(0);
    if (SMS_API_HAS(api->player, set_yaw))
        api->player->set_yaw(yaw);
    cooldown = 3;
}
static SmsVec3 outside(QPortal p) { return portal_point(p, 0, 65, 390); }
static void equip_fludd(void) {
    if (SMS_API_HAS(api->player, set_fludd))
        api->player->set_fludd(1);
    if (SMS_API_HAS(api->player, set_nozzle))
        api->player->set_nozzle(4, 0);
}
static int read_placement(const char *path) {
    FILE *f = qopen(path, "rb");
    if (!f)
        return 0;
    QPortal p = {0};
    char extra;
    int n = fscanf(f, "%f %f %f %f %c", &p.base.x, &p.base.y, &p.base.z, &p.yaw, &extra);
    fclose(f);
    if (n != 4 || !finite_position(p.base) || !isfinite(p.yaw))
        return 0;
    p.valid = 1;
    entrance = p;
    return 1;
}
static void save_placement(void) {
    if (!placement_path[0])
        return;
    QCtx *c = calloc(1, sizeof *c);
    if (!c) {
        warning_time = 8;
        return;
    }
    if (!setjmp(c->failure)) {
        char *text = qfmt(c, "%.9g %.9g %.9g %.9g\n", entrance.base.x, entrance.base.y,
                          entrance.base.z, entrance.yaw);
        qatomic(c, placement_path, (QBytes){(unsigned char *)text, strlen(text)});
    } else {
        diagnostic(self, api, SMS_LOG_WARN, c->error);
        warning_time = 8;
    }
    qclear(c);
    free(c);
}
static void place_here(void) {
    locate();
    if (!ready || current_area != 1) {
        warning_time = 8;
        return;
    }
    SmsPlayer p = {.size = sizeof p};
    if (!api->player->get(&p) || !finite_position(p.position) || !isfinite(p.yaw))
        return;
    entrance = (QPortal){p.position, p.yaw + 180, 1};
    save_placement();
    cooldown = 8;
    placement_notice = 1;
}
static void enter(void) {
    locate();
    SmsGameState s = {.size = sizeof s};
    api->game->state(&s);
    if (!s.stage_ready || s.paused || cooldown > 0)
        return;
    if (!ready) {
        warning_time = 7;
        return;
    }
    if (in_world()) {
        move_to(spawn, 180);
        return;
    }
    if (current_area != 1)
        return;
    SmsPlayer p = {.size = sizeof p};
    if (!api->player->get(&p) || !finite_position(p.position))
        return;
    return_position = entrance.valid ? outside(entrance) : p.position;
    saved_return_yaw = entrance.valid ? entrance.yaw : p.yaw;
    return_valid = 1;
    return_episode = current_episode;
    arrival = 1;
    cooldown = 5;
    started = 0;
    if (!api->game->warp(target_area, target_episode)) {
        arrival = 0;
        cooldown = 1;
        started = 1;
    }
}
static void leave(void) {
    locate();
    SmsGameState s = {.size = sizeof s};
    api->game->state(&s);
    if (!s.stage_ready || s.paused || cooldown > 0 || !ready || !in_world())
        return;
    arrival = 2;
    cooldown = 5;
    started = 0;
    if (!api->game->warp(1, return_episode)) {
        arrival = 0;
        cooldown = 1;
        started = 1;
    }
}
static void action(SmsMod *m, const char *key, void *user) {
    (void)m;
    (void)user;
    if (!strcmp(key, "enter"))
        pending_action = 1;
    else if (!strcmp(key, "return"))
        pending_action = 2;
    else if (!strcmp(key, "place"))
        place_here();
    else if (!strcmp(key, "rescue"))
        pending_action = 3;
}
static void place_arrival(void) {
    if (in_world() && ready) {
        move_to(spawn, 180);
        equip_fludd();
    } else if (ready && return_valid && arrival == 2 && current_area == 1)
        move_to(return_position, saved_return_yaw);
}
static float draw_time(void) {
#ifdef _WIN32
    return (float)((double)(GetTickCount64() % 4096000) / 1000.0);
#else
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (float)fmod((double)t.tv_sec + (double)t.tv_nsec / 1e9, 4096.0);
#endif
}
static void draw_gate(QPortal gate, const char *label, float time) {
    SmsPlayer p = {.size = sizeof p};
    if (!gate.valid || !api->player->get(&p))
        return;
    SmsVec3 d = {p.position.x - gate.base.x, p.position.y - gate.base.y,
                 p.position.z - gate.base.z};
    if (d.x * d.x + d.y * d.y + d.z * d.z < 6000.f * 6000.f)
        portal_draw(api, gate, time, label);
}
static int event(SmsMod *mod, SmsEvent *ev, void *user) {
    (void)mod;
    (void)user;
    if (!api)
        return SMS_CONTINUE;
    if (ev->type == SMS_EV_STAGE_LOADING && ev->data && ev->size >= sizeof(SmsEvStageLoading)) {
        SmsEvStageLoading *e = ev->data;
        current_area = e->area;
        current_episode = e->episode;
        ready = started = 0;
        cooldown = 3;
    } else if (ev->type == SMS_EV_PLAYER_SPAWN && ev->data &&
               ev->size >= sizeof(SmsEvPlayerSpawn)) {
        SmsEvPlayerSpawn *e = ev->data;
        if (in_world() || (return_valid && arrival == 2 && current_area == 1)) {
            e->position = in_world() ? spawn : return_position;
            e->yaw = in_world() ? 180 : saved_return_yaw;
            e->has_position = 1;
            e->kind = SMS_START_WAIT;
        }
    } else if (ev->type == SMS_EV_STAGE_LOADED) {
        locate();
        cooldown = 3;
        place_arrival();
        if (api->minor < 4) {
            started = 1;
            arrival = 0;
        }
    } else if (ev->type == SMS_EV_STAGE_START) {
        locate();
        started = 1;
        cooldown = 3;
        place_arrival();
        arrival = 0;
    } else if (ev->type == SMS_EV_STAGE_LEAVING) {
        ready = started = 0;
    } else if (ev->type == SMS_EV_TICK && ev->data && ev->size >= sizeof(SmsEvTick)) {
        float dt = ((SmsEvTick *)ev->data)->dt;
        if (!isfinite(dt) || dt < 0)
            return SMS_CONTINUE;
        dt = fminf(dt, .25f);
        SmsGameState s = {.size = sizeof s};
        api->game->state(&s);
        if (s.paused || !s.stage_ready)
            return SMS_CONTINUE;
        cooldown = fmaxf(0, cooldown - dt);
        warning_time = fmaxf(0, warning_time - dt);
        if (!ready || !started || cooldown > 0)
            return SMS_CONTINUE;
        if (pending_action) {
            int command = pending_action;
            pending_action = 0;
            if (command == 1)
                enter();
            else if (command == 2)
                leave();
            else if (in_world())
                move_to(spawn, 180);
            return SMS_CONTINUE;
        }
        SmsPlayer p = {.size = sizeof p};
        if (!api->player->get(&p))
            return SMS_CONTINUE;
        if (in_world()) {
            if (p.position.y < 6500) {
                move_to(spawn, 180);
                return SMS_CONTINUE;
            }
            if (near_portal(p.position, exit_gate))
                leave();
        } else if (current_area == 1 && near_portal(p.position, entrance))
            enter();
    } else if (ev->type == SMS_EV_KEY && ev->data && ev->size >= sizeof(SmsEvKey)) {
        SmsEvKey *e = ev->data;
        if (!e->down || e->repeat)
            return SMS_CONTINUE;
        if (e->scancode == key_enter && key_enter >= 0) {
            enter();
            return SMS_CANCEL;
        }
        if (e->scancode == key_return && key_return >= 0) {
            leave();
            return SMS_CANCEL;
        }
        if (e->scancode == key_rescue && key_rescue >= 0) {
            SmsGameState s = {.size = sizeof s};
            api->game->state(&s);
            if (in_world() && ready && !s.paused)
                move_to(spawn, 180);
            return SMS_CANCEL;
        }
    } else if (ev->type == SMS_EV_DRAW) {
        if (!ready && warning_time <= 0)
            return SMS_CONTINUE;
        if (ready) {
            float t = draw_time();
            if (in_world())
                draw_gate(exit_gate, "DELFINO PLAZA", t);
            else if (current_area == 1)
                draw_gate(entrance, "QUAHOG", t);
        }
        if (SMS_API_HAS(api, draw) && SMS_API_HAS(api->draw, text) &&
            SMS_API_HAS(api->draw, rect) && SMS_API_HAS(api->draw, hud_slot)) {
            SmsRect r = {0};
            if (!api->draw->hud_slot(self, SMS_ANCHOR_BOTTOM_LEFT, 410, 27, &r))
                return SMS_CONTINUE;
            api->draw->rect(r.x, r.y, r.w, r.h, 0x142132C0u);
            const char *text =
                pending_action     ? "Travel selected. Close F1 to continue."
                : warning_time > 0 ? "Load Delfino Plaza to use or place this portal."
                : !entrance.valid  ? "Quahog: F6 enters / F1: Place portal here"
                : placement_notice && cooldown > 0 ? "Portal saved. Step away before it activates."
                : in_world()                       ? "Quahog - F8: return / F10: rescue"
                                                   : "Quahog - walk into portal / F6: enter";
            api->draw->text(r.x + 8, r.y + 7, 13, 0xFFFFFFFFu, SMS_TEXT_SHADOW, text);
        }
    }
    return SMS_CONTINUE;
}
static void cleanup(void) {
    if (api && SMS_API_HAS(api, core) && SMS_API_HAS(api->core, unsubscribe))
        while (nhandles)
            api->core->unsubscribe(self, handles[--nhandles]);
    qrestore(&world, native_host);
    ready = started = 0;
}
#ifdef QUAHOG_TEST
extern QHost quahog_test_host(void);
#endif
SMS_MOD_EXPORT int sms_mod_load(SmsMod *mod, const SmsApi *host) {
    diagnostic_path[0] = 0;
    const char *missing = api_missing(host);
    if (missing)
        return refuse(mod, host, 1, missing);
    if (!portal_supported(host))
        return refuse(mod, host, 1,
                      "Portal drawing needs draw.triangle, draw.line and camera.world_to_canvas.");
    api = host;
    self = mod;
    QCtx *c = calloc(1, sizeof *c);
    if (!c)
        return refuse(mod, host, 2, "Out of memory");
    if (setjmp(c->failure)) {
        char reason[2048];
        memcpy(reason, c->error, sizeof reason);
        qclear(c);
        free(c);
        cleanup();
        return refuse(mod, host, 2, reason);
    }
#ifdef QUAHOG_TEST
    native_host = quahog_test_host();
#else
    native_host = qresolve(c);
#endif
    const char *folder = api->core->mod_dir(mod), *data = api->core->data_dir(mod);
    if (!folder || !*folder || !data || !*data)
        qfail(c, "The game returned an empty mod or data folder");
    qmkdir(c, data);
    diagnostic(mod, host, SMS_LOG_INFO,
               "Starting Quahog " Q_VERSION
               " in native C. No Python, installer or background process.");
    if (SMS_API_HAS(api, net) && SMS_API_HAS(api->net, role) && api->net->role() != SMS_NET_OFF)
        qfail(c, "Quahog currently supports single-player. Disable it before hosting or joining "
                 "multiplayer.");
    SmsGameState state = {.size = sizeof state};
    api->game->state(&state);
    qmemory(c, *native_host.memory_size, state.stage_ready != 0, data);
    QStages stages;
    QLocation location;
    qprepare(c, native_host, folder, data, &world, &stages, &location);
    target_area = (int)stages.area;
    target_episode = (int)stages.episode;
    if (state.stage_ready && state.area == target_area && state.episode == target_episode &&
        !api->game->find_object("QuahogNativeReady"))
        qfail(c,
              "The original target stage is already loaded. Restart the game with Quahog enabled.");
    entrance = (QPortal){0};
    exit_gate = (QPortal){{-157.27223f, 7995.334f, -892.7065f}, 0, 1};
    placement_notice = pending_action = 0;
    int n = snprintf(placement_path, sizeof placement_path, "%s/quahog-portal.txt", data);
    if (n < 0 || n >= (int)sizeof placement_path)
        qfail(c, "Portal placement path too long");
    if (!read_placement(placement_path) && location.valid)
        entrance = (QPortal){{location.x, location.y, location.z}, location.yaw, 1};
    return_valid = entrance.valid;
    return_position = entrance.valid ? outside(entrance) : (SmsVec3){0, 300, 0};
    saved_return_yaw = entrance.yaw;
    if (!native_host.replace(world.path, world.prepared, world.new_size))
        qfail(c, "Game refused the prepared Quahog stage");
    world.active = 1;
    qclear(c);
    free(c);
    cooldown = 3;
    warning_time = 0;
    arrival = 0;
    started = 0;
    return_episode = 0;
    locate();
    started = ready;
    static const unsigned types[] = {
        SMS_EV_STAGE_LOADING, SMS_EV_PLAYER_SPAWN, SMS_EV_STAGE_LOADED, SMS_EV_STAGE_START,
        SMS_EV_STAGE_LEAVING, SMS_EV_TICK,         SMS_EV_KEY,          SMS_EV_DRAW};
    nhandles = 0;
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        unsigned type = types[i];
        if ((type == SMS_EV_STAGE_LOADING || type == SMS_EV_PLAYER_SPAWN) && api->minor < 5)
            continue;
        if (type == SMS_EV_STAGE_START && api->minor < 4)
            continue;
        int h = api->core->subscribe(mod, type, event, NULL, 0);
        if (h <= 0) {
            cleanup();
            return refuse(mod, host, 4,
                          "Could not register native event handlers; original stage restored.");
        }
        handles[nhandles++] = h;
    }
    key_enter = api->input->bind_key(mod, "enter", "F6");
    key_return = api->input->bind_key(mod, "return", "F8");
    key_rescue = api->input->bind_key(mod, "rescue", "F10");
    static const char *const keys[] = {"enter", "return", "place", "rescue"},
                             *const labels[] = {"Enter Quahog", "Return to Plaza",
                                                "Place portal here (stand at wall, face wall)",
                                                "Rescue to Quahog entrance"};
    for (size_t i = 0; i < 4; i++) {
        SmsOption option = {.size = sizeof option,
                            .key = keys[i],
                            .label = labels[i],
                            .type = SMS_OPTION_ACTION,
                            .on_change = action};
        api->config->add_option(mod, &option);
    }
    diagnostic(mod, host, SMS_LOG_INFO,
               "Quahog native C loaded. F6 enter / F8 return / F10 rescue.");
    return 0;
}
SMS_MOD_EXPORT void sms_mod_unload(SmsMod *mod) {
    (void)mod;
    cleanup();
    api = NULL;
    self = NULL;
    diagnostic_path[0] = placement_path[0] = 0;
    entrance = (QPortal){0};
    pending_action = 0;
}
