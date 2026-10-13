/* Xbox 360 backend on the Microsoft XDK (Direct3D 9-style API, XInput).
 * Produces a dashboard-launchable XEX. UNTESTED: written without the XDK at
 * hand; see docs/XBOX360_GUIDE.md §"First build" for what to check.
 *
 * Rendering: one dynamic vertex batch per frame (position, colour, uv) with
 * tiny HLSL shaders compiled at start-up by D3DX, so sprites keep their real
 * tint and alpha (unlike the libxenon backend). */
#include <xtl.h>
#include <xgraphics.h>
#include <d3dx9.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "core/vs_platform.h"

struct vs_texture { IDirect3DTexture9 *t; int w, h; };

struct vtx { float x, y, z; DWORD color; float u, v; };

#define MAX_QUADS 20000
#define VERTS_PER_QUAD 6

static IDirect3D9 *g_d3d;
static IDirect3DDevice9 *g_dev;
static IDirect3DVertexShader9 *g_vs;
static IDirect3DPixelShader9 *g_ps;
static IDirect3DVertexDeclaration9 *g_decl;
static vtx *g_verts;
static int g_quad_count;
static int g_lw, g_lh, g_fbw, g_fbh;
static float g_scale, g_off_x, g_off_y;
static struct batch { IDirect3DTexture9 *tex; int first, count; } g_batches[4096];
static int g_batch_count;

static const char *VS_SRC =
    "float4 screen : register(c0);\n"            /* 2/W, 2/H, 0, 0 */
    "struct VSIn { float3 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };\n"
    "struct VSOut { float4 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };\n"
    "VSOut main(VSIn i) { VSOut o;\n"
    "  o.pos = float4(i.pos.x * screen.x - 1.0, 1.0 - i.pos.y * screen.y, 0.0, 1.0);\n"
    "  o.col = i.col; o.uv = i.uv; return o; }\n";

static const char *PS_SRC =
    "sampler s : register(s0);\n"
    "struct PSIn { float4 col : COLOR0; float2 uv : TEXCOORD0; };\n"
    "float4 main(PSIn i) : COLOR { return tex2D(s, i.uv) * i.col; }\n";

static int compile_shaders() {
    ID3DXBuffer *code = NULL, *err = NULL;
    if (FAILED(D3DXCompileShader(VS_SRC, (UINT)strlen(VS_SRC), NULL, NULL, "main", "vs_3_0", 0, &code, &err, NULL))) {
        if (err) OutputDebugStringA((const char *)err->GetBufferPointer());
        return -1;
    }
    g_dev->CreateVertexShader((const DWORD *)code->GetBufferPointer(), &g_vs);
    code->Release(); code = NULL;
    if (FAILED(D3DXCompileShader(PS_SRC, (UINT)strlen(PS_SRC), NULL, NULL, "main", "ps_3_0", 0, &code, &err, NULL))) {
        if (err) OutputDebugStringA((const char *)err->GetBufferPointer());
        return -1;
    }
    g_dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &g_ps);
    code->Release();
    D3DVERTEXELEMENT9 elems[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    return FAILED(g_dev->CreateVertexDeclaration(elems, &g_decl)) ? -1 : 0;
}

extern "C" int vs_plat_init(const vs_plat_config *cfg) {
    g_lw = cfg->logical_w;
    g_lh = cfg->logical_h;
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) return -1;
    /* 720p back buffer; the console's video scaler handles the TV mode */
    XVIDEO_MODE vm;
    XGetVideoMode(&vm);
    g_fbw = 1280; g_fbh = 720;
    if (vm.dwDisplayWidth < 1280) { g_fbw = 640; g_fbh = 480; }
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = g_fbw;
    pp.BackBufferHeight = g_fbh;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = FALSE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (FAILED(g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev))) return -1;
    if (compile_shaders() != 0) return -1;

    /* aspect-fit: 810x456 → ~1.58x at 720p. An integer scale would be 1x
     * (a small box in the middle of the TV); point sampling keeps pixels sharp */
    float sx = (float)g_fbw / g_lw, sy = (float)g_fbh / g_lh;
    g_scale = sx < sy ? sx : sy;
    g_off_x = (g_fbw - g_lw * g_scale) * 0.5f;
    g_off_y = (g_fbh - g_lh * g_scale) * 0.5f;
    g_verts = (vtx *)malloc(sizeof(vtx) * MAX_QUADS * VERTS_PER_QUAD);
    return g_verts ? 0 : -1;
}

extern "C" void vs_plat_shutdown(void) {
    if (g_decl) g_decl->Release();
    if (g_ps) g_ps->Release();
    if (g_vs) g_vs->Release();
    if (g_dev) g_dev->Release();
    if (g_d3d) g_d3d->Release();
    free(g_verts);
}

extern "C" vs_texture *vs_plat_texture_create(uint32_t w, uint32_t h, const uint8_t *argb8_be) {
    /* Linear (untiled) A8R8G8B8: the console is big-endian, so the pack's
     * byte order A,R,G,B per pixel is the native DWORD layout. */
    IDirect3DTexture9 *t = NULL;
    if (FAILED(g_dev->CreateTexture(w, h, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT, &t, NULL))) return NULL;
    D3DLOCKED_RECT lr;
    if (FAILED(t->LockRect(0, &lr, NULL, 0))) { t->Release(); return NULL; }
    for (uint32_t y = 0; y < h; y++) memcpy((uint8_t *)lr.pBits + y * lr.Pitch, argb8_be + (size_t)y * w * 4, (size_t)w * 4);
    t->UnlockRect(0);
    vs_texture *out = (vs_texture *)malloc(sizeof *out);
    out->t = t; out->w = (int)w; out->h = (int)h;
    return out;
}

extern "C" void vs_plat_texture_destroy(vs_texture *t) {
    if (!t) return;
    t->t->Release();
    free(t);
}

extern "C" void vs_plat_begin_frame(uint32_t clear_rgb) {
    g_dev->Clear(0, NULL, D3DCLEAR_TARGET, 0xff000000u | clear_rgb, 1.0f, 0);
    g_dev->BeginScene();
    g_dev->SetVertexShader(g_vs);
    g_dev->SetPixelShader(g_ps);
    g_dev->SetVertexDeclaration(g_decl);
    float screen[4] = { 2.0f / g_fbw, 2.0f / g_fbh, 0, 0 };
    g_dev->SetVertexShaderConstantF(0, screen, 1);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    g_dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    g_dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    g_quad_count = 0;
    g_batch_count = 0;
}

static void push_quad(IDirect3DTexture9 *tex, const float px[4], const float py[4], float u0, float v0, float u1, float v1, DWORD color) {
    if (g_quad_count >= MAX_QUADS) return;
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
        v[i].color = color; v[i].u = us[k]; v[i].v = vs[k];
    }
    g_quad_count++;
    g_batches[g_batch_count - 1].count += VERTS_PER_QUAD;
}

extern "C" void vs_plat_draw(const vs_sprite *s) {
    if (!s->tex || s->alpha == 0) return;
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
    DWORD color = ((DWORD)s->alpha << 24) | (s->tint & 0xffffff);
    push_quad(s->tex->t, px, py, u0, v0, u1, v1, color);
}

extern "C" void vs_plat_draw_tiled(vs_texture *tex, float ox, float oy, uint32_t tint) {
    if (!tex) return;
    float sx = -fmodf(ox, (float)tex->w), sy = -fmodf(oy, (float)tex->h);
    if (sx > 0) sx -= tex->w;
    if (sy > 0) sy -= tex->h;
    vs_sprite s;
    memset(&s, 0, sizeof s);
    s.tex = tex; s.sw = (int16_t)tex->w; s.sh = (int16_t)tex->h; s.scale_x = s.scale_y = 1; s.alpha = 255; s.tint = tint;
    for (float y = sy; y < g_lh; y += tex->h)
        for (float x = sx; x < g_lw; x += tex->w) { s.dx = x; s.dy = y; vs_plat_draw(&s); }
}

extern "C" void vs_plat_end_frame(void) {
    for (int i = 0; i < g_batch_count; i++) {
        g_dev->SetTexture(0, g_batches[i].tex);
        g_dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, g_batches[i].count / 3, g_verts + g_batches[i].first, sizeof(vtx));
    }
    g_dev->EndScene();
    g_dev->Present(NULL, NULL, NULL, NULL);
}

extern "C" int vs_plat_poll(vs_pad *pad) {
    XINPUT_STATE st;
    memset(&st, 0, sizeof st);
    uint32_t b = 0;
    float lx = 0, ly = 0;
    if (XInputGetState(0, &st) == ERROR_SUCCESS) {
        WORD w = st.Gamepad.wButtons;
        if (w & XINPUT_GAMEPAD_DPAD_UP) b |= VS_BTN_UP;
        if (w & XINPUT_GAMEPAD_DPAD_DOWN) b |= VS_BTN_DOWN;
        if (w & XINPUT_GAMEPAD_DPAD_LEFT) b |= VS_BTN_LEFT;
        if (w & XINPUT_GAMEPAD_DPAD_RIGHT) b |= VS_BTN_RIGHT;
        if (w & XINPUT_GAMEPAD_A) b |= VS_BTN_A;
        if (w & XINPUT_GAMEPAD_B) b |= VS_BTN_B;
        if (w & XINPUT_GAMEPAD_X) b |= VS_BTN_X;
        if (w & XINPUT_GAMEPAD_Y) b |= VS_BTN_Y;
        if (w & XINPUT_GAMEPAD_START) b |= VS_BTN_START;
        if (w & XINPUT_GAMEPAD_BACK) b |= VS_BTN_BACK;
        lx = st.Gamepad.sThumbLX / 32767.0f;
        ly = -st.Gamepad.sThumbLY / 32767.0f;   /* XInput: up is positive; the game uses y down */
    }
    pad->buttons = b;
    pad->lx = lx;
    pad->ly = ly;
    return 1; /* the guide button is handled by the system */
}

extern "C" uint64_t vs_plat_time_ms(void) { return (uint64_t)GetTickCount(); }
extern "C" void vs_plat_sleep_ms(uint32_t ms) { Sleep(ms); }

extern "C" void *vs_plat_read_file(const char *path, size_t *size) {
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
extern "C" void vs_plat_free(void *p) { free(p); }
extern "C" int vs_plat_screenshot(const char *path) { (void)path; return -1; }

extern "C" void vs_plat_log(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    OutputDebugStringA(buf);
}
