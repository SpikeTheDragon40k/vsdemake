/* Xbox 360 backend on libxenon (Free60). UNTESTED: written without a
 * toolchain or hardware in reach; see notes/toolchain.md for the assumptions
 * and how to verify them.
 *
 * Rendering: one textured-quad batch per frame through the precompiled
 * shaders of libxenon's cube example (vs_xenon_shaders.c). Their pixel
 * shader computes rgb = dot(normal, lightDirection) * tex.rgb, so a sprite's
 * tint is reduced to a brightness (the normal's x component) and per-sprite
 * alpha is not available (sprites with alpha < 64 are skipped). Proper
 * colour tint and alpha need a 2D shader compiled for Xenos. */
#include "core/vs_platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <xenos/xe.h>
#include <xenos/xenos.h>
#include <xenos/edram.h>
#include <console/console.h>
#include <input/input.h>
#include <usb/usbmain.h>
#include <diskio/ata.h>
#include <libfat/fat.h>
#include <ppc/timebase.h>
#include <time/time.h>
#include <xenon_soc/xenon_power.h>

extern unsigned char vs_xenon_shader_vs[];
extern unsigned char vs_xenon_shader_ps[];

struct vs_texture { struct XenosSurface *s; int w, h; };

/* vertex: POSITION float3, NORMAL float3, TEXCOORD float2 = 32 bytes */
typedef struct { float x, y, z, nx, ny, nz, u, v; } vtx;

#define MAX_QUADS 20000
#define VERTS_PER_QUAD 6

static struct XenosDevice g_xe_dev, *g_xe;
static struct XenosShader *g_vs, *g_ps;
static struct XenosVertexBuffer *g_vb;
static vtx *g_verts;               /* locked vertex memory for the frame */
static int g_quad_count;
static int g_lw, g_lh, g_fbw, g_fbh;
static float g_scale, g_off_x, g_off_y;
static struct { struct XenosSurface *tex; int first, count; } g_batches[4096];
static int g_batch_count;

int vs_plat_init(const vs_plat_config *cfg) {
    g_lw = cfg->logical_w;
    g_lh = cfg->logical_h;
    xenos_init(VIDEO_MODE_AUTO);
    console_init();
    xenon_make_it_faster(XENON_SPEED_FULL);
    usb_init();
    usb_do_poll();
    xenon_ata_init();
    fatInitDefault();

    g_xe = &g_xe_dev;
    Xe_Init(g_xe);
    struct XenosSurface *fb = Xe_GetFramebufferSurface(g_xe);
    Xe_SetRenderTarget(g_xe, fb);
    g_fbw = fb->width;
    g_fbh = fb->height;
    /* letterbox the logical size into the framebuffer, integer scale when possible */
    float sx = (float)g_fbw / g_lw, sy = (float)g_fbh / g_lh;
    g_scale = sx < sy ? sx : sy;
    if (g_scale >= 1) g_scale = floorf(g_scale);
    g_off_x = (g_fbw - g_lw * g_scale) * 0.5f;
    g_off_y = (g_fbh - g_lh * g_scale) * 0.5f;

    static const struct XenosVBFFormat vbf = { 3, {
        { XE_USAGE_POSITION, 0, XE_TYPE_FLOAT3 },
        { XE_USAGE_NORMAL, 0, XE_TYPE_FLOAT3 },
        { XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2 },
    } };
    g_ps = Xe_LoadShaderFromMemory(g_xe, vs_xenon_shader_ps);
    Xe_InstantiateShader(g_xe, g_ps, 0);
    g_vs = Xe_LoadShaderFromMemory(g_xe, vs_xenon_shader_vs);
    Xe_InstantiateShader(g_xe, g_vs, 0);
    Xe_ShaderApplyVFetchPatches(g_xe, g_vs, 0, &vbf);

    g_vb = Xe_CreateVertexBuffer(g_xe, MAX_QUADS * VERTS_PER_QUAD * sizeof(vtx));
    edram_init(g_xe);
    console_close();
    return 0;
}

void vs_plat_shutdown(void) {}

vs_texture *vs_plat_texture_create(uint32_t w, uint32_t h, const uint8_t *argb8_be) {
    /* linear (untiled) ARGB8; the pack's big-endian words are the console's
     * native layout, so rows copy straight in */
    struct XenosSurface *s = Xe_CreateTexture(g_xe, w, h, 0, XE_FMT_8888 | XE_FMT_ARGB, 0);
    if (!s) return NULL;
    s->use_filtering = 0;
    s->u_addressing = XE_TEXADDR_CLAMP;
    s->v_addressing = XE_TEXADDR_CLAMP;
    uint8_t *dst = (uint8_t *)Xe_Surface_LockRect(g_xe, s, 0, 0, 0, 0, XE_LOCK_WRITE);
    for (uint32_t y = 0; y < h; y++) memcpy(dst + y * s->wpitch, argb8_be + (size_t)y * w * 4, (size_t)w * 4);
    Xe_Surface_Unlock(g_xe, s);
    vs_texture *t = (vs_texture *)malloc(sizeof *t);
    t->s = s; t->w = (int)w; t->h = (int)h;
    return t;
}

void vs_plat_texture_destroy(vs_texture *t) {
    if (!t) return;
    Xe_DestroyTexture(g_xe, t->s);
    free(t);
}

void vs_plat_begin_frame(uint32_t clear_rgb) {
    Xe_InvalidateState(g_xe);
    Xe_SetClearColor(g_xe, 0xff000000u | clear_rgb);
    Xe_SetShader(g_xe, SHADER_TYPE_PIXEL, g_ps, 0);
    Xe_SetShader(g_xe, SHADER_TYPE_VERTEX, g_vs, 0);
    Xe_SetCullMode(g_xe, XE_CULL_NONE);
    Xe_SetZEnable(g_xe, 0);
    Xe_SetZWrite(g_xe, 0);
    Xe_SetBlendControl(g_xe, XE_BLEND_SRCALPHA, XE_BLENDOP_ADD, XE_BLEND_INVSRCALPHA, XE_BLEND_ONE, XE_BLENDOP_ADD, XE_BLEND_ZERO);
    /* ortho: framebuffer pixel (px, py) → clip (-1..1, 1..-1); the VS does
     * mul(transpose(modelView), pos), i.e. row-major rows are the matrix rows */
    float m[16] = {
        2.0f / g_fbw, 0, 0, -1,
        0, -2.0f / g_fbh, 0, 1,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    Xe_SetVertexShaderConstantF(g_xe, 0, m, 4);
    float light[4] = { 1, 0, 0, 0 }; /* brightness comes from normal.x */
    Xe_SetPixelShaderConstantF(g_xe, 0, light, 1);
    g_verts = (vtx *)Xe_VB_Lock(g_xe, g_vb, 0, MAX_QUADS * VERTS_PER_QUAD * sizeof(vtx), XE_LOCK_WRITE);
    g_quad_count = 0;
    g_batch_count = 0;
}

static void push_quad(struct XenosSurface *tex, const float px[4], const float py[4], float u0, float v0, float u1, float v1, float bright) {
    if (g_quad_count >= MAX_QUADS || !g_verts) return;
    if (g_batch_count == 0 || g_batches[g_batch_count - 1].tex != tex) {
        if (g_batch_count >= (int)(sizeof g_batches / sizeof g_batches[0])) return;
        g_batches[g_batch_count].tex = tex;
        g_batches[g_batch_count].first = g_quad_count * VERTS_PER_QUAD;
        g_batches[g_batch_count].count = 0;
        g_batch_count++;
    }
    vtx *v = g_verts + g_quad_count * VERTS_PER_QUAD;
    const float us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
    const int order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) {
        int k = order[i];
        v[i].x = g_off_x + px[k] * g_scale; v[i].y = g_off_y + py[k] * g_scale; v[i].z = 0;
        v[i].nx = bright; v[i].ny = 0; v[i].nz = 0;
        v[i].u = us[k]; v[i].v = vs[k];
    }
    g_quad_count++;
    g_batches[g_batch_count - 1].count += VERTS_PER_QUAD;
}

void vs_plat_draw(const vs_sprite *s) {
    if (!s->tex || s->alpha < 64) return;
    float w = s->sw * s->scale_x, h = s->sh * s->scale_y;
    float cx = s->dx + w * 0.5f, cy = s->dy + h * 0.5f;
    float px[4] = { s->dx, s->dx + w, s->dx + w, s->dx }, py[4] = { s->dy, s->dy, s->dy + h, s->dy + h };
    if (s->angle_deg != 0) {
        float a = s->angle_deg * 3.14159265f / 180.0f, c = cosf(a), sn = sinf(a);
        for (int i = 0; i < 4; i++) {
            float x = px[i] - cx, y = py[i] - cy;
            px[i] = cx + x * c - y * sn; py[i] = cy + x * sn + y * c;
        }
    }
    float u0 = (float)s->sx / s->tex->w, v0 = (float)s->sy / s->tex->h;
    float u1 = (float)(s->sx + s->sw) / s->tex->w, v1 = (float)(s->sy + s->sh) / s->tex->h;
    if (s->flip & VS_FLIP_X) { float t = u0; u0 = u1; u1 = t; }
    if (s->flip & VS_FLIP_Y) { float t = v0; v0 = v1; v1 = t; }
    /* tint → brightness (luma of the multiply colour) */
    float bright = (0.299f * ((s->tint >> 16) & 255) + 0.587f * ((s->tint >> 8) & 255) + 0.114f * (s->tint & 255)) / 255.0f;
    push_quad(s->tex->s, px, py, u0, v0, u1, v1, bright);
}

void vs_plat_draw_tiled(vs_texture *tex, float ox, float oy, uint32_t tint) {
    if (!tex) return;
    float sx = -fmodf(ox, (float)tex->w), sy = -fmodf(oy, (float)tex->h);
    if (sx > 0) sx -= tex->w;
    if (sy > 0) sy -= tex->h;
    vs_sprite s = {0};
    s.tex = tex; s.sw = (int16_t)tex->w; s.sh = (int16_t)tex->h; s.scale_x = s.scale_y = 1; s.alpha = 255; s.tint = tint;
    for (float y = sy; y < g_lh; y += tex->h)
        for (float x = sx; x < g_lw; x += tex->w) { s.dx = x; s.dy = y; vs_plat_draw(&s); }
}

void vs_plat_end_frame(void) {
    if (g_verts) { Xe_VB_Unlock(g_xe, g_vb); g_verts = NULL; }
    for (int i = 0; i < g_batch_count; i++) {
        Xe_SetTexture(g_xe, 0, g_batches[i].tex);
        Xe_SetStreamSource(g_xe, 0, g_vb, g_batches[i].first * (int)sizeof(vtx), (int)sizeof(vtx));
        Xe_DrawPrimitive(g_xe, XE_PRIMTYPE_TRIANGLELIST, 0, g_batches[i].count / 3);
    }
    Xe_Resolve(g_xe);
    Xe_Sync(g_xe);
}

int vs_plat_poll(vs_pad *pad) {
    struct controller_data_s c;
    usb_do_poll();
    memset(&c, 0, sizeof c);
    get_controller_data(&c, 0);
    uint32_t b = 0;
    if (c.up) b |= VS_BTN_UP;
    if (c.down) b |= VS_BTN_DOWN;
    if (c.left) b |= VS_BTN_LEFT;
    if (c.right) b |= VS_BTN_RIGHT;
    if (c.a) b |= VS_BTN_A;
    if (c.b) b |= VS_BTN_B;
    if (c.x) b |= VS_BTN_X;
    if (c.y) b |= VS_BTN_Y;
    if (c.start) b |= VS_BTN_START;
    if (c.back) b |= VS_BTN_BACK;
    pad->buttons = b;
    pad->lx = c.s1_x / 32767.0f;
    pad->ly = -c.s1_y / 32767.0f;   /* libxenon reports stick up as positive */
    return !c.logo;                  /* guide button quits to the loader */
}

uint64_t vs_plat_time_ms(void) { return mftb() / (PPC_TIMEBASE_FREQ / 1000); }
void vs_plat_sleep_ms(uint32_t ms) { mdelay((int)ms); }

void *vs_plat_read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf && size) *size = (size_t)n;
    return buf;
}
void vs_plat_free(void *p) { free(p); }
int vs_plat_screenshot(const char *path) { (void)path; return -1; }

void vs_plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* Audio is not implemented for the libxenon fallback yet: silent. */
int vs_plat_audio_open(int rate) { (void)rate; return -1; }
void vs_plat_audio_close(void) {}
int vs_plat_audio_queued(void) { return 0; }
void vs_plat_audio_push(const int16_t *frames, int count) { (void)frames; (void)count; }
