#include "psx_guncon.h"

#include "gpu.h"
#include "sio.h"

/* GPU video clock (psx-spx "GPU Timings"). */
#define GUNCON_GPU_HZ_NTSC 53693175.0
#define GUNCON_GPU_HZ_PAL  53203425.0
#define GUNCON_COUNTER_HZ   8000000.0

static int   s_reticle_on[PSX_MAX_PLAYERS];
static float s_reticle_u[PSX_MAX_PLAYERS];
static float s_reticle_v[PSX_MAX_PLAYERS];

void psx_guncon_set_reticle(int slot, int visible, float u, float v) {
    if (slot < 0 || slot >= PSX_MAX_PLAYERS) return;
    s_reticle_on[slot] = visible ? 1 : 0;
    s_reticle_u[slot] = u;
    s_reticle_v[slot] = v;
}

int psx_guncon_get_reticle(int slot, float *u, float *v) {
    if (slot < 0 || slot >= PSX_MAX_PLAYERS || !s_reticle_on[slot]) return 0;
    *u = s_reticle_u[slot];
    *v = s_reticle_v[slot];
    return 1;
}

static float clamp01(float f) {
    if (f < 0.f) return 0.f;
    if (f > 1.f) return 1.f;
    return f;
}

void psx_guncon_beam_from_uv(float u, float v, uint16_t *x, uint16_t *y) {
    uint32_t x1, x2, y1, y2, hres1, hres2;
    gpu_get_crtc_debug(&x1, &x2, &y1, &y2, &hres1, &hres2);
    const int pal = gpu_get_video_pal();

    /* Unprogrammed/degenerate ranges fall back to the GP1(00h) reset values. */
    if (x2 <= x1) { x1 = 0x200; x2 = 0xC00; }
    if (y2 <= y1) { y1 = 0x010; y2 = 0x100; }

    const double tick = (double)x1 + (double)clamp01(u) * (double)(x2 - x1);
    const double gpu_hz = pal ? GUNCON_GPU_HZ_PAL : GUNCON_GPU_HZ_NTSC;
    const double bx = tick * GUNCON_COUNTER_HZ / gpu_hz;
    const double by = (double)y1 + (double)clamp01(v) * (double)(y2 - y1);

    *x = (uint16_t)(bx + 0.5);
    *y = (uint16_t)(by + 0.5);
}

void psx_guncon_apply(int slot, int on_screen, float u, float v, uint16_t pressed) {
    uint16_t bx = PSX_GUNCON_OFFSCREEN_X, by = PSX_GUNCON_OFFSCREEN_Y;
    if (on_screen)
        psx_guncon_beam_from_uv(u, v, &bx, &by);
    sio_set_guncon_position(slot, bx, by);
    sio_set_pad_state_slot(slot, (uint16_t)~(pressed & (PSX_GUNCON_TRIGGER |
                                                        PSX_GUNCON_A |
                                                        PSX_GUNCON_B)));
}
