#ifndef PSX_GPU_GL_UPSCALER_H
#define PSX_GPU_GL_UPSCALER_H

/* Upscalers for the OpenGL presenter (gpu_gl_upscaler.cpp): AMD FSR 1 and
 * NVIDIA Image Scaling, from the internal resolution to the window's size.
 * The presenter draws the game image into the staging target, then asks for
 * the upscaled image and draws that 1:1 with its usual gamma and scanlines. */

#ifdef __cplusplus
extern "C" {
#endif

enum { GL_UPSCALER_OFF = 0, GL_UPSCALER_FSR = 1, GL_UPSCALER_NIS = 2 };

/* Once the context is current: 1 when OpenGL 4.3 is there (programs are built
 * on first use), 0 otherwise (keep the plain scale). */
int gl_upscaler_init(void);

/* The RGBA8 staging texture of w x h and its framebuffer, for the image to
 * upscale; 0 when unavailable. */
unsigned gl_upscaler_staging(int w, int h, unsigned *fbo);

/* Upscales the staging image (in_w x in_h, its full size) to out_w x out_h.
 * Returns the output texture (same row order as the input), or 0 when the
 * mode's program could not be built. Restores the framebuffer and program
 * bindings; leaves texture unit 0 active. sharpness 0..1. */
unsigned gl_upscaler_run(int mode, float sharpness, int in_w, int in_h, int out_w, int out_h);

#ifdef __cplusplus
}
#endif

#endif
