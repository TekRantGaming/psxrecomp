#ifndef PSXRECOMP_PSX_GUNCON_H
#define PSXRECOMP_PSX_GUNCON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Namco GunCon (NPC-103) host mapping. The gun reports where it saw the CRT
 * beam: X in 8 MHz clocks since HSYNC, Y in scanlines. The presenter shows
 * exactly the GP1(06h) X1..X2 / GP1(07h) Y1..Y2 window, so a normalized
 * position inside the picture maps linearly onto those CRTC ranges (GPU video
 * clock ticks for X, lines for Y) and X is then rescaled from the GPU clock to
 * the gun's 8 MHz counter — the same model DuckStation uses. */

/* GunCon button bits in the active-low pad word. */
#define PSX_GUNCON_TRIGGER 0x2000u
#define PSX_GUNCON_A       0x0008u
#define PSX_GUNCON_B       0x4000u

/* Beam reading when the gun sees no light (aimed off-screen). */
#define PSX_GUNCON_OFFSCREEN_X 0x0001u
#define PSX_GUNCON_OFFSCREEN_Y 0x000Au

/* Convert a normalized display position (0..1 across the presented picture)
 * into the beam X/Y the gun reports, from the current CRTC programming. */
void psx_guncon_beam_from_uv(float u, float v, uint16_t *x, uint16_t *y);

/* Drive a GunCon slot for one frame. on_screen = 0 reports "no light".
 * pressed is a mask of PSX_GUNCON_* bits currently held. */
void psx_guncon_apply(int slot, int on_screen, float u, float v, uint16_t pressed);

/* On-screen sight for a gun aimed without a pointer (gamepad stick). A real
 * GunCon game draws no crosshair, so the presenter overlays one at (u, v)
 * while visible. */
void psx_guncon_set_reticle(int slot, int visible, float u, float v);
int  psx_guncon_get_reticle(int slot, float *u, float *v);

/* Sight look: style 0 cross, 1 dot, 2 ring; size 0 small, 1 medium, 2 large. */
void psx_guncon_set_reticle_style(int style, int size);
void psx_guncon_get_reticle_style(int *style, int *size);

#ifdef __cplusplus
}
#endif

#endif /* PSXRECOMP_PSX_GUNCON_H */
