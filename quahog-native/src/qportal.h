#ifndef QPORTAL_H
#define QPORTAL_H
#include "sms_mod.h"
#include <math.h>
#define Q_PI 3.14159265358979323846f
typedef struct {
    SmsVec3 base;
    float yaw;
    int valid;
} QPortal;
static SmsVec3 portal_point(QPortal p, float x, float y, float z) {
    float a = p.yaw * Q_PI / 180, c = cosf(a), s = sinf(a);
    return (SmsVec3){p.base.x + c * x + s * z, p.base.y + y, p.base.z - s * x + c * z};
}
static int portal_supported(const SmsApi *a) {
    return SMS_API_HAS(a, draw) && SMS_API_HAS(a->draw, triangle) && SMS_API_HAS(a->draw, line) &&
           SMS_API_HAS(a, camera) && SMS_API_HAS(a->camera, world_to_canvas);
}
static SmsColor portal_color(unsigned alpha, int bright) {
    return (bright ? 0xADF9FF00u : 0x22BEDB00u) | alpha;
}
static int project(const SmsApi *api, SmsVec3 p, float *x, float *y) {
    float d = 0;
    return api->camera->world_to_canvas(&p, x, y, &d) && isfinite(*x) && isfinite(*y) && d > 8;
}
static void portal_triangle(const SmsApi *api, SmsVec3 a, SmsVec3 b, SmsVec3 c, SmsColor shade) {
    float ax, ay, bx, by, cx, cy;
    if (project(api, a, &ax, &ay) && project(api, b, &bx, &by) && project(api, c, &cx, &cy))
        api->draw->triangle(ax, ay, bx, by, cx, cy, shade);
}
static void portal_line(const SmsApi *api, SmsVec3 a, SmsVec3 b, float width, SmsColor shade) {
    float ax, ay, bx, by;
    if (project(api, a, &ax, &ay) && project(api, b, &bx, &by))
        api->draw->line(ax, ay, bx, by, width, shade);
}
static SmsVec3 oval(QPortal p, float pulse, float a, float r, float z) {
    return portal_point(p, cosf(a) * 138 * r * pulse, 205 + sinf(a) * 198 * r * pulse, z);
}
static void portal_draw(const SmsApi *api, QPortal p, float time, const char *label) {
    if (!p.valid || !portal_supported(api))
        return;
    const int segments = 64;
    float pulse = 1 + .025f * sinf(time * 2.5f);
    SmsVec3 center = portal_point(p, 0, 205, 0);
    for (int i = 0; i < segments; i++) {
        float a = 2 * Q_PI * i / segments, b = 2 * Q_PI * (i + 1) / segments,
              shimmer = .5f + .5f * sinf(a * 5 - time * 2);
        SmsVec3 ai = oval(p, pulse, a, .89f, 0), bi = oval(p, pulse, b, .89f, 0),
                ao = oval(p, pulse, a, 1, 0), bo = oval(p, pulse, b, 1, 0),
                ae = oval(p, pulse, a, 1.14f, 0), be = oval(p, pulse, b, 1.14f, 0);
        portal_triangle(api, center, ai, bi, portal_color(70 + (unsigned)(30 * shimmer), 0));
        portal_triangle(api, ai, ao, bo, portal_color(220, 1));
        portal_triangle(api, ai, bo, bi, portal_color(220, 1));
        portal_triangle(api, ao, ae, be, portal_color(35, 0));
        portal_triangle(api, ao, be, bo, portal_color(35, 0));
        portal_line(api, oval(p, pulse, a, .96f, 3), oval(p, pulse, b, .96f, 3), 2.1f,
                    portal_color(255, 1));
    }
    for (int arm = 0; arm < 3; arm++)
        for (int i = 0; i < 24; i++) {
            float t0 = i / 24.f, t1 = (i + 1) / 24.f,
                  a = time * 1.4f + arm * 2 * Q_PI / 3 + t0 * 4.4f,
                  b = time * 1.4f + arm * 2 * Q_PI / 3 + t1 * 4.4f;
            portal_line(api, oval(p, pulse, a, .12f + .68f * t0, 4),
                        oval(p, pulse, b, .12f + .68f * t1, 4), 1.1f, portal_color(170, 1));
        }
    float x, y;
    if (label && SMS_API_HAS(api->draw, text) && project(api, portal_point(p, 0, 450, 0), &x, &y))
        api->draw->text(x, y, 14, portal_color(255, 1), SMS_TEXT_CENTER | SMS_TEXT_SHADOW, label);
}
#endif
