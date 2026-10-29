/* Upscalers for the OpenGL presenter ([video] upscaler): AMD FidelityFX Super
 * Resolution 1 (EASU, then RCAS sharpening) and NVIDIA Image Scaling. The game
 * image is drawn at the internal resolution; these rebuild it at the window's
 * size instead of the plain bilinear scale.
 *
 * Both need OpenGL 4.3 (textureGather, bitfield ops, NIS's compute shader).
 * Without it gl_upscaler_init returns 0 and the presenter keeps its plain
 * scale. Programs are built the first time a mode is used.
 *
 * The shaders are AMD's and NVIDIA's own, vendored unmodified in
 * runtime/third_party (fsr1/, nis/, see UPSCALERS.md) and embedded by
 * runtime.cmake. Images keep GL's row order throughout: the filters only see
 * row and column indices, so the input and output being bottom-up changes
 * nothing. */
#include "gpu_gl_upscaler.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "psx_sdl.h"
#if defined(PSX_SDL3)
#include <SDL3/SDL_opengl.h>
#else
#include <SDL_opengl.h>
#endif

/* AMD's CPU helpers for the EASU and RCAS constants. */
#define A_CPU 1
#include "fsr1/ffx_a.h"
#include "fsr1/ffx_fsr1.h"
/* NVIDIA's CPU helpers: NVScalerUpdateConfig and the filter coefficients. */
#include "nis/NIS_Config.h"

#include "psx_upscaler_shaders.inc"  /* kFfxA, kFfxFsr1, kNisScaler (runtime.cmake) */

namespace {

#ifndef APIENTRY
#define APIENTRY
#endif
typedef char GLchar_t;
typedef GLuint (APIENTRY *PFN_CreateShader)(GLenum);
typedef void   (APIENTRY *PFN_ShaderSource)(GLuint, GLsizei, const GLchar_t *const *, const GLint *);
typedef void   (APIENTRY *PFN_CompileShader)(GLuint);
typedef void   (APIENTRY *PFN_GetShaderiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar_t *);
typedef GLuint (APIENTRY *PFN_CreateProgram)(void);
typedef void   (APIENTRY *PFN_AttachShader)(GLuint, GLuint);
typedef void   (APIENTRY *PFN_LinkProgram)(GLuint);
typedef void   (APIENTRY *PFN_GetProgramiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar_t *);
typedef void   (APIENTRY *PFN_DeleteShader)(GLuint);
typedef void   (APIENTRY *PFN_DeleteProgram)(GLuint);
typedef void   (APIENTRY *PFN_UseProgram)(GLuint);
typedef GLint  (APIENTRY *PFN_GetUniformLocation)(GLuint, const GLchar_t *);
typedef GLuint (APIENTRY *PFN_GetUniformBlockIndex)(GLuint, const GLchar_t *);
typedef void   (APIENTRY *PFN_UniformBlockBinding)(GLuint, GLuint, GLuint);
typedef void   (APIENTRY *PFN_Uniform1i)(GLint, GLint);
typedef void   (APIENTRY *PFN_Uniform4uiv)(GLint, GLsizei, const GLuint *);
typedef void   (APIENTRY *PFN_GenFramebuffers)(GLsizei, GLuint *);
typedef void   (APIENTRY *PFN_DeleteFramebuffers)(GLsizei, const GLuint *);
typedef void   (APIENTRY *PFN_BindFramebuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *PFN_CheckFramebufferStatus)(GLenum);
typedef void   (APIENTRY *PFN_ActiveTexture)(GLenum);
typedef void   (APIENTRY *PFN_GenVertexArrays)(GLsizei, GLuint *);
typedef void   (APIENTRY *PFN_BindVertexArray)(GLuint);
typedef void   (APIENTRY *PFN_GenBuffers)(GLsizei, GLuint *);
typedef void   (APIENTRY *PFN_BindBuffer)(GLenum, GLuint);
typedef void   (APIENTRY *PFN_BufferData)(GLenum, ptrdiff_t, const void *, GLenum);
typedef void   (APIENTRY *PFN_BindBufferBase)(GLenum, GLuint, GLuint);
typedef void   (APIENTRY *PFN_DispatchCompute)(GLuint, GLuint, GLuint);
typedef void   (APIENTRY *PFN_BindImageTexture)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
typedef void   (APIENTRY *PFN_MemoryBarrier)(GLbitfield);

PFN_CreateShader p_CreateShader;
PFN_ShaderSource p_ShaderSource;
PFN_CompileShader p_CompileShader;
PFN_GetShaderiv p_GetShaderiv;
PFN_GetShaderInfoLog p_GetShaderInfoLog;
PFN_CreateProgram p_CreateProgram;
PFN_AttachShader p_AttachShader;
PFN_LinkProgram p_LinkProgram;
PFN_GetProgramiv p_GetProgramiv;
PFN_GetProgramInfoLog p_GetProgramInfoLog;
PFN_DeleteShader p_DeleteShader;
PFN_DeleteProgram p_DeleteProgram;
PFN_UseProgram p_UseProgram;
PFN_GetUniformLocation p_GetUniformLocation;
PFN_GetUniformBlockIndex p_GetUniformBlockIndex;
PFN_UniformBlockBinding p_UniformBlockBinding;
PFN_Uniform1i p_Uniform1i;
PFN_Uniform4uiv p_Uniform4uiv;
PFN_GenFramebuffers p_GenFramebuffers;
PFN_DeleteFramebuffers p_DeleteFramebuffers;
PFN_BindFramebuffer p_BindFramebuffer;
PFN_FramebufferTexture2D p_FramebufferTexture2D;
PFN_CheckFramebufferStatus p_CheckFramebufferStatus;
PFN_ActiveTexture p_ActiveTexture;
PFN_GenVertexArrays p_GenVertexArrays;
PFN_BindVertexArray p_BindVertexArray;
PFN_GenBuffers p_GenBuffers;
PFN_BindBuffer p_BindBuffer;
PFN_BufferData p_BufferData;
PFN_BindBufferBase p_BindBufferBase;
PFN_DispatchCompute p_DispatchCompute;
PFN_BindImageTexture p_BindImageTexture;
PFN_MemoryBarrier p_MemoryBarrier;

constexpr GLenum kFragmentShader = 0x8B30, kVertexShader = 0x8B31, kComputeShader = 0x91B9;
constexpr GLenum kCompileStatus = 0x8B81, kLinkStatus = 0x8B82;
constexpr GLenum kFramebuffer = 0x8D40, kColorAttachment0 = 0x8CE0, kFramebufferComplete = 0x8CD5;
constexpr GLenum kTexture0 = 0x84C0, kRgba8 = 0x8058, kRgba32f = 0x8814, kClampToEdge = 0x812F;
constexpr GLenum kUniformBuffer = 0x8A11, kDynamicDraw = 0x88E8, kWriteOnly = 0x88B9;
constexpr GLbitfield kImageBarrier = 0x20, kTextureFetchBarrier = 0x8;

int s_state = 0;  /* 0 not tried, 1 available, -1 unavailable */

/* An RGBA8 render target, resized on demand. */
struct Target {
    GLuint tex = 0, fbo = 0;
    int w = 0, h = 0;
    bool ensure(int nw, int nh) {
        if (tex && w == nw && h == nh) return true;
        if (!tex) {
            glGenTextures(1, &tex);
            p_GenFramebuffers(1, &fbo);
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, kClampToEdge);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, kRgba8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        p_BindFramebuffer(kFramebuffer, fbo);
        p_FramebufferTexture2D(kFramebuffer, kColorAttachment0, GL_TEXTURE_2D, tex, 0);
        const bool ok = p_CheckFramebufferStatus(kFramebuffer) == kFramebufferComplete;
        p_BindFramebuffer(kFramebuffer, 0);
        w = nw;
        h = nh;
        return ok;
    }
};

Target s_staging, s_easu, s_out;
GLuint s_vao = 0;
GLuint s_easu_prog = 0, s_rcas_prog = 0, s_nis_prog = 0;
int s_easu_tried = 0, s_rcas_tried = 0, s_nis_tried = 0;
GLint s_easu_uIn = -1, s_easu_uCon = -1, s_rcas_uIn = -1, s_rcas_uCon = -1;
GLuint s_nis_ubo = 0, s_nis_coef_scale = 0, s_nis_coef_usm = 0;
GLint s_nis_uIn = -1, s_nis_uScale = -1, s_nis_uUsm = -1;

const char *kFullscreenVs =
    "#version 430 core\n"
    "void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2);\n"
    "  gl_Position = vec4(p*2.0-1.0,0.0,1.0); }\n";

GLuint compile(GLenum kind, const char *const *parts, int n, const char *what) {
    GLuint s = p_CreateShader(kind);
    p_ShaderSource(s, n, parts, nullptr);
    p_CompileShader(s);
    GLint ok = 0;
    p_GetShaderiv(s, kCompileStatus, &ok);
    if (!ok) {
        char log[2048] = {0};
        p_GetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "psxrecomp: %s shader failed to compile:\n%s\n", what, log);
        p_DeleteShader(s);
        return 0;
    }
    return s;
}

/* Links the compiled shaders (0 for one that failed: nothing is linked). */
GLuint link(std::initializer_list<GLuint> shaders, const char *what) {
    bool all = true;
    for (GLuint s : shaders) all = all && s != 0;
    GLuint p = 0;
    if (all) {
        p = p_CreateProgram();
        for (GLuint s : shaders) p_AttachShader(p, s);
        p_LinkProgram(p);
    }
    for (GLuint s : shaders)
        if (s) p_DeleteShader(s);
    if (!all) return 0;
    GLint ok = 0;
    p_GetProgramiv(p, kLinkStatus, &ok);
    if (!ok) {
        char log[2048] = {0};
        p_GetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "psxrecomp: %s program failed to link:\n%s\n", what, log);
        p_DeleteProgram(p);
        return 0;
    }
    return p;
}

const char *text(const unsigned char *bytes) { return reinterpret_cast<const char *>(bytes); }

/* EASU: the edge-adaptive upscale, one fragment per output pixel. */
bool build_easu() {
    if (s_easu_tried) return s_easu_prog != 0;
    s_easu_tried = 1;
    const char *head = "#version 430 core\n#define A_GPU 1\n#define A_GLSL 1\n";
    const char *mid =
        "\n#define FSR_EASU_F 1\n"
        "uniform sampler2D u_in;\n"
        "uniform uvec4 u_con[4];\n"
        "AF4 FsrEasuRF(AF2 p){ return textureGather(u_in, p, 0); }\n"
        "AF4 FsrEasuGF(AF2 p){ return textureGather(u_in, p, 1); }\n"
        "AF4 FsrEasuBF(AF2 p){ return textureGather(u_in, p, 2); }\n";
    const char *tail =
        "\nout vec4 frag;\n"
        "void main(){ AF3 c; FsrEasuF(c, AU2(gl_FragCoord.xy), u_con[0], u_con[1], u_con[2], u_con[3]);\n"
        "  frag = vec4(c, 1.0); }\n";
    const char *fs[] = {head, text(kFfxA), mid, text(kFfxFsr1), tail};
    const char *vs[] = {kFullscreenVs};
    s_easu_prog = link({compile(kVertexShader, vs, 1, "FSR"), compile(kFragmentShader, fs, 5, "FSR EASU")}, "FSR EASU");
    if (s_easu_prog) {
        s_easu_uIn = p_GetUniformLocation(s_easu_prog, "u_in");
        s_easu_uCon = p_GetUniformLocation(s_easu_prog, "u_con");
    }
    return s_easu_prog != 0;
}

/* RCAS: FSR's sharpening pass at the output size. */
bool build_rcas() {
    if (s_rcas_tried) return s_rcas_prog != 0;
    s_rcas_tried = 1;
    const char *head = "#version 430 core\n#define A_GPU 1\n#define A_GLSL 1\n";
    const char *mid =
        "\n#define FSR_RCAS_F 1\n"
        "uniform sampler2D u_in;\n"
        "uniform uvec4 u_con;\n"
        "AF4 FsrRcasLoadF(ASU2 p){ return texelFetch(u_in, p, 0); }\n"
        "void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b){}\n";
    const char *tail =
        "\nout vec4 frag;\n"
        "void main(){ AF1 r, g, b; FsrRcasF(r, g, b, AU2(gl_FragCoord.xy), u_con);\n"
        "  frag = vec4(r, g, b, 1.0); }\n";
    const char *fs[] = {head, text(kFfxA), mid, text(kFfxFsr1), tail};
    const char *vs[] = {kFullscreenVs};
    s_rcas_prog = link({compile(kVertexShader, vs, 1, "FSR"), compile(kFragmentShader, fs, 5, "FSR RCAS")}, "FSR RCAS");
    if (s_rcas_prog) {
        s_rcas_uIn = p_GetUniformLocation(s_rcas_prog, "u_in");
        s_rcas_uCon = p_GetUniformLocation(s_rcas_prog, "u_con");
    }
    return s_rcas_prog != 0;
}

/* NIS: NVIDIA's scaler as a compute shader. NIS_Scaler.h samples through
 * Vulkan-style sampler2D(texture, sampler) constructors; desktop OpenGL has
 * combined samplers, so those constructors are taken out of the macros before
 * compiling (the header itself stays as NVIDIA ships it). */
bool build_nis() {
    if (s_nis_tried) return s_nis_prog != 0;
    s_nis_tried = 1;
    std::string scaler = text(kNisScaler);
    for (const char *from : {"sampler2D(x, samplerLinearClamp)", "sampler2D(x, sampler)"}) {
        for (size_t at = scaler.find(from); at != std::string::npos; at = scaler.find(from, at))
            scaler.replace(at, std::strlen(from), "x");
    }
    const char *head =
        "#version 430 core\n"
        "#define NIS_GLSL 1\n#define NIS_HLSL 0\n#define NIS_HLSL_6_2 0\n"
        "#define NIS_SCALER 1\n#define NIS_USE_HALF_PRECISION 0\n#define NIS_NV12_SUPPORT 0\n"
        "#define NIS_CLAMP_OUTPUT 1\n#define NIS_VIEWPORT_SUPPORT 0\n"
        "#define NIS_BLOCK_WIDTH 32\n#define NIS_BLOCK_HEIGHT 24\n#define NIS_THREAD_GROUP_SIZE 256\n"
        "layout(std140, binding = 0) uniform const_buffer {\n"
        "  float kDetectRatio; float kDetectThres; float kMinContrastRatio; float kRatioNorm;\n"
        "  float kContrastBoost; float kEps; float kSharpStartY; float kSharpScaleY;\n"
        "  float kSharpStrengthMin; float kSharpStrengthScale; float kSharpLimitMin; float kSharpLimitScale;\n"
        "  float kScaleX; float kScaleY; float kDstNormX; float kDstNormY; float kSrcNormX; float kSrcNormY;\n"
        "  uint kInputViewportOriginX; uint kInputViewportOriginY; uint kInputViewportWidth; uint kInputViewportHeight;\n"
        "  uint kOutputViewportOriginX; uint kOutputViewportOriginY; uint kOutputViewportWidth; uint kOutputViewportHeight;\n"
        "  float reserved0; float reserved1;\n"
        "};\n"
        "uniform sampler2D in_texture;\n"
        "uniform sampler2D coef_scaler;\n"
        "uniform sampler2D coef_usm;\n"
        "layout(rgba8, binding = 0) uniform writeonly image2D out_texture;\n";
    const char *tail =
        "\nlayout(local_size_x = NIS_THREAD_GROUP_SIZE) in;\n"
        "void main(){ NVScaler(gl_WorkGroupID.xy, gl_LocalInvocationID.x); }\n";
    const char *cs[] = {head, scaler.c_str(), tail};
    s_nis_prog = link({compile(kComputeShader, cs, 3, "NIS")}, "NIS");
    if (!s_nis_prog) return false;
    s_nis_uIn = p_GetUniformLocation(s_nis_prog, "in_texture");
    s_nis_uScale = p_GetUniformLocation(s_nis_prog, "coef_scaler");
    s_nis_uUsm = p_GetUniformLocation(s_nis_prog, "coef_usm");
    p_GenBuffers(1, &s_nis_ubo);
    /* The filter coefficients: kPhaseCount rows of kFilterSize taps, 4 per texel. */
    auto coef = [](GLuint &tex, const float *data) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, kRgba32f, int(kFilterSize / 4), int(kPhaseCount), 0, GL_RGBA, GL_FLOAT, data);
    };
    coef(s_nis_coef_scale, &coef_scale[0][0]);
    coef(s_nis_coef_usm, &coef_usm[0][0]);
    return true;
}

void draw_fullscreen(GLuint fbo, int w, int h) {
    p_BindFramebuffer(kFramebuffer, fbo);
    glViewport(0, 0, w, h);
    p_BindVertexArray(s_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    p_BindVertexArray(0);
}

unsigned run_fsr(float sharpness, int in_w, int in_h, int out_w, int out_h) {
    if (!build_easu() || !build_rcas() || !s_easu.ensure(out_w, out_h) || !s_out.ensure(out_w, out_h)) return 0;
    AU1 con[16];
    FsrEasuCon(con, con + 4, con + 8, con + 12, AF1(in_w), AF1(in_h), AF1(s_staging.w), AF1(s_staging.h),
               AF1(out_w), AF1(out_h));
    p_UseProgram(s_easu_prog);
    p_ActiveTexture(kTexture0);
    glBindTexture(GL_TEXTURE_2D, s_staging.tex);
    p_Uniform1i(s_easu_uIn, 0);
    p_Uniform4uiv(s_easu_uCon, 4, con);
    draw_fullscreen(s_easu.fbo, out_w, out_h);
    /* RCAS: sharpness 1 is its maximum (0 stops), 0 two stops softer. */
    AU1 rcon[4];
    FsrRcasCon(rcon, AF1(2.0f * (1.0f - sharpness)));
    p_UseProgram(s_rcas_prog);
    glBindTexture(GL_TEXTURE_2D, s_easu.tex);
    p_Uniform1i(s_rcas_uIn, 0);
    p_Uniform4uiv(s_rcas_uCon, 1, rcon);
    draw_fullscreen(s_out.fbo, out_w, out_h);
    return s_out.tex;
}

unsigned run_nis(float sharpness, int in_w, int in_h, int out_w, int out_h) {
    if (!build_nis() || !s_out.ensure(out_w, out_h)) return 0;
    NISConfig cfg;
    if (!NVScalerUpdateConfig(cfg, sharpness, 0, 0, uint32_t(in_w), uint32_t(in_h), uint32_t(s_staging.w),
                              uint32_t(s_staging.h), 0, 0, uint32_t(out_w), uint32_t(out_h), uint32_t(out_w),
                              uint32_t(out_h), NISHDRMode::None))
        return 0;
    p_BindBuffer(kUniformBuffer, s_nis_ubo);
    p_BufferData(kUniformBuffer, sizeof cfg, &cfg, kDynamicDraw);
    p_BindBufferBase(kUniformBuffer, 0, s_nis_ubo);
    p_UseProgram(s_nis_prog);
    p_ActiveTexture(kTexture0);
    glBindTexture(GL_TEXTURE_2D, s_staging.tex);
    p_Uniform1i(s_nis_uIn, 0);
    p_ActiveTexture(kTexture0 + 1);
    glBindTexture(GL_TEXTURE_2D, s_nis_coef_scale);
    p_Uniform1i(s_nis_uScale, 1);
    p_ActiveTexture(kTexture0 + 2);
    glBindTexture(GL_TEXTURE_2D, s_nis_coef_usm);
    p_Uniform1i(s_nis_uUsm, 2);
    p_BindImageTexture(0, s_out.tex, 0, GL_FALSE, 0, kWriteOnly, kRgba8);
    p_DispatchCompute(GLuint((out_w + 31) / 32), GLuint((out_h + 23) / 24), 1);
    p_MemoryBarrier(kImageBarrier | kTextureFetchBarrier);
    p_ActiveTexture(kTexture0 + 2);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_ActiveTexture(kTexture0 + 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    p_ActiveTexture(kTexture0);
    p_BindBuffer(kUniformBuffer, 0);
    return s_out.tex;
}

}  // namespace

extern "C" int gl_upscaler_init(void) {
    if (s_state) return s_state > 0;
    s_state = -1;
    GLint major = 0, minor = 0;
    glGetIntegerv(0x821B /* GL_MAJOR_VERSION */, &major);
    glGetIntegerv(0x821C /* GL_MINOR_VERSION */, &minor);
    if (major * 10 + minor < 43) {
        std::fprintf(stdout, "psxrecomp: upscalers need OpenGL 4.3 (have %d.%d); the plain scale is used\n", major,
                     minor);
        return 0;
    }
    bool ok = true;
#define UPS_LOAD(p, n) do { p = reinterpret_cast<decltype(p)>(SDL_GL_GetProcAddress(n)); if (!p) ok = false; } while (0)
    UPS_LOAD(p_CreateShader, "glCreateShader");
    UPS_LOAD(p_ShaderSource, "glShaderSource");
    UPS_LOAD(p_CompileShader, "glCompileShader");
    UPS_LOAD(p_GetShaderiv, "glGetShaderiv");
    UPS_LOAD(p_GetShaderInfoLog, "glGetShaderInfoLog");
    UPS_LOAD(p_CreateProgram, "glCreateProgram");
    UPS_LOAD(p_AttachShader, "glAttachShader");
    UPS_LOAD(p_LinkProgram, "glLinkProgram");
    UPS_LOAD(p_GetProgramiv, "glGetProgramiv");
    UPS_LOAD(p_GetProgramInfoLog, "glGetProgramInfoLog");
    UPS_LOAD(p_DeleteShader, "glDeleteShader");
    UPS_LOAD(p_DeleteProgram, "glDeleteProgram");
    UPS_LOAD(p_UseProgram, "glUseProgram");
    UPS_LOAD(p_GetUniformLocation, "glGetUniformLocation");
    UPS_LOAD(p_GetUniformBlockIndex, "glGetUniformBlockIndex");
    UPS_LOAD(p_UniformBlockBinding, "glUniformBlockBinding");
    UPS_LOAD(p_Uniform1i, "glUniform1i");
    UPS_LOAD(p_Uniform4uiv, "glUniform4uiv");
    UPS_LOAD(p_GenFramebuffers, "glGenFramebuffers");
    UPS_LOAD(p_DeleteFramebuffers, "glDeleteFramebuffers");
    UPS_LOAD(p_BindFramebuffer, "glBindFramebuffer");
    UPS_LOAD(p_FramebufferTexture2D, "glFramebufferTexture2D");
    UPS_LOAD(p_CheckFramebufferStatus, "glCheckFramebufferStatus");
    UPS_LOAD(p_ActiveTexture, "glActiveTexture");
    UPS_LOAD(p_GenVertexArrays, "glGenVertexArrays");
    UPS_LOAD(p_BindVertexArray, "glBindVertexArray");
    UPS_LOAD(p_GenBuffers, "glGenBuffers");
    UPS_LOAD(p_BindBuffer, "glBindBuffer");
    UPS_LOAD(p_BufferData, "glBufferData");
    UPS_LOAD(p_BindBufferBase, "glBindBufferBase");
    UPS_LOAD(p_DispatchCompute, "glDispatchCompute");
    UPS_LOAD(p_BindImageTexture, "glBindImageTexture");
    UPS_LOAD(p_MemoryBarrier, "glMemoryBarrier");
#undef UPS_LOAD
    if (!ok) {
        std::fprintf(stdout, "psxrecomp: upscalers unavailable (missing OpenGL 4.3 entry points)\n");
        return 0;
    }
    p_GenVertexArrays(1, &s_vao);
    s_state = 1;
    std::fprintf(stdout, "psxrecomp: upscalers available (OpenGL %d.%d)\n", major, minor);
    return 1;
}

extern "C" unsigned gl_upscaler_staging(int w, int h, unsigned *fbo) {
    if (s_state <= 0 || w <= 0 || h <= 0 || !s_staging.ensure(w, h)) return 0;
    *fbo = s_staging.fbo;
    return s_staging.tex;
}

extern "C" unsigned gl_upscaler_run(int mode, float sharpness, int in_w, int in_h, int out_w, int out_h) {
    if (s_state <= 0 || in_w <= 0 || in_h <= 0 || out_w <= 0 || out_h <= 0 || !s_staging.tex) return 0;
    if (sharpness < 0.0f) sharpness = 0.0f;
    if (sharpness > 1.0f) sharpness = 1.0f;
    GLint prev_fbo = 0, prev_prog = 0;
    glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &prev_fbo);
    glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &prev_prog);
    unsigned out = 0;
    if (mode == GL_UPSCALER_FSR) out = run_fsr(sharpness, in_w, in_h, out_w, out_h);
    else if (mode == GL_UPSCALER_NIS) out = run_nis(sharpness, in_w, in_h, out_w, out_h);
    p_BindFramebuffer(kFramebuffer, GLuint(prev_fbo));
    p_UseProgram(GLuint(prev_prog));
    return out;
}
