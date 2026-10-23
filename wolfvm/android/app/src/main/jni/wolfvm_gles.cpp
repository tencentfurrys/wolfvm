// wolfvm_gles.cpp — renderer implementation (approx/MODEL, see header).
// Clean-room; no engine code. Facts used: .mps has 3 stacked W*H*u32 planes
// (byte-exact decoder), TileSetData gives per-tileset sheet path strings.
// Everything about chip cells / chip size / draw order that is NOT yet
// engine-verified is tagged MODEL below.
//
// Mesh layout: one batch per (plane z, sheet) — tiles of one plane never
// overlap, so per-sheet batching preserves both z-order and any sheet mix.
#include "wolfvm_gles.h"

#include <android/log.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <string>

#define LOG_TAG "wolfvm"
#define ALOG(c, ...) ((void)__android_log_print(ANDROID_LOG_##c, LOG_TAG, __VA_ARGS__))
#define ALOGI(...) ALOG(INFO, __VA_ARGS__)
#define ALOGW(...) ALOG(WARN, __VA_ARGS__)
#define ALOGE(...) ALOG(ERROR, __VA_ARGS__)

namespace wolf {

static const char* kVs = R"(#version 300 es
uniform vec2 u_cam;   // viewport top-left in map pixel space
uniform vec2 u_vp;    // viewport size in pixels
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    gl_Position = vec4(((a_pos.x - u_cam.x) / u_vp.x) * 2.0 - 1.0,
                       1.0 - ((a_pos.y - u_cam.y) / u_vp.y) * 2.0,
                       0.0, 1.0);
    v_uv = a_uv;
})";

static const char* kFs = R"(#version 300 es
precision mediump float;
uniform sampler2D u_tex;
uniform vec4 u_col;
in vec2 v_uv;
out vec4 o;
void main() { o = texture(u_tex, v_uv) * u_col; })";

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        ALOGE("shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool Renderer::init(void* window, int32_t w, int32_t h) {
    win_ = window;
    egl_.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_.dpy == EGL_NO_DISPLAY) return false;
    EGLint maj = 0, mi = 0;
    eglInitialize(egl_.dpy, &maj, &mi);
    const EGLint cfgA[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg = nullptr; EGLint n = 0;
    if (!eglChooseConfig(egl_.dpy, cfgA, &cfg, 1, &n) || n < 1) return false;
    const EGLint ctxA[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    egl_.ctx = eglCreateContext(egl_.dpy, cfg, EGL_NO_CONTEXT, ctxA);
    egl_.sfc = eglCreateWindowSurface(egl_.dpy, cfg, (EGLNativeWindowType)window, nullptr);
    if (egl_.ctx == EGL_NO_CONTEXT || egl_.sfc == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(egl_.dpy, egl_.sfc, egl_.sfc, egl_.ctx)) return false;
    vpw = w; vph = h;

    GLuint vs = compile(GL_VERTEX_SHADER, kVs);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFs);
    if (!vs || !fs) return false;
    prog_ = glCreateProgram();
    glAttachShader(prog_, vs); glAttachShader(prog_, fs);
    glLinkProgram(prog_);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog_, sizeof log, nullptr, log);
        ALOGE("program link failed: %s", log);
        return false;
    }
    u_cam_ = glGetUniformLocation(prog_, "u_cam");
    u_vp_  = glGetUniformLocation(prog_, "u_vp");
    u_tex_ = glGetUniformLocation(prog_, "u_tex");
    u_col_ = glGetUniformLocation(prog_, "u_col");

    glGenTextures(1, &white_tex_);   // 1x1 white fallback (tint fills)
    glBindTexture(GL_TEXTURE_2D, white_tex_);
    const uint8_t px[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);

    glEnable(GL_BLEND);
    // Java Bitmaps arrive premultiplied -> (one, one-minus-src-alpha).
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    return (prog_ != 0) && (white_tex_ != 0);
}

void Renderer::resize(int32_t w, int32_t h) {
    vpw = w; vph = h;
}

void Renderer::shutdown() {
    if (egl_.dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_.sfc != EGL_NO_SURFACE) eglDestroySurface(egl_.dpy, egl_.sfc);
        if (egl_.ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_.dpy, egl_.ctx);
        eglTerminate(egl_.dpy);
    }
    egl_ = {};
    batches_.clear();
    prog_ = white_tex_ = 0;
    build_meshes_locked = false;
}

bool Renderer::upload_sheet(int idx, const void* rgba, int w, int h) {
    if (idx < 0 || idx >= (int)sheets.size()) return false;
    ChipSheet& s = sheets[(size_t)idx];
    if (s.tex == 0) glGenTextures(1, &s.tex);
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s.w = w; s.h = h; s.ready = true;
    // MODEL chip-grid geometry: largest side (16..64 px) dividing both dims.
    // TODO verify: the engine knows the global chip size; sheet decode pending.
    s.cols = s.rows = 1;
    for (int c = 64; c >= 16; --c)
        if (c <= w && c <= h && w % c == 0 && h % c == 0) { s.cols = w / c; s.rows = h / c; break; }
    return true;
}

void Renderer::build_plane_meshes(const std::vector<std::vector<uint32_t>>& planes,
                                  int w, int h) {
    batches_.clear();
    missing_cells = oob_cells = band_cells = 0;
    for (size_t z = 0; z < planes.size(); ++z) {
        const auto& cells = planes[z];
        if (cells.size() != (size_t)w * (size_t)h) { missing_cells += cells.size(); continue; }
        // per-sheet accumulation for each plane
        std::vector<std::vector<float>> mesh(sheets.size());
        for (int ty = 0; ty < h; ++ty)
            for (int tx = 0; tx < w; ++tx) {
                const uint32_t v = cells[(size_t)ty * (size_t)w + (size_t)tx];
                // Engine-true decode (data census 2026-10-23, all 105 maps):
                // the chip cell value is DECIMAL-composed, not a hex bitfield:
                //   v = page*100000 + chip
                //   page 0 = base sheet (moriV13.png), chip = 1-based grid id
                //            (observed ids 1..426 vs cap 440, dense, oob 0)
                //   pages 1..16 = the AT/autotile sheets that follow moriV13 in
                //            the tileset record; chip = 4-digit edge code
                //            (digits 0..4, fill code 4444; max chip 4444 on every
                //            page, never a raw grid id).
                // code->variant pick inside an AT sheet is the Game.exe map-draw
                // rule — RE-open. Until it decodes, every AT cell draws grid 0
                // (MODEL approximation: sheets/regions/colors show, edge
                // variants don't yet).
                const uint32_t page = v / 100000u;
                const uint32_t chip = v % 100000u;
                // v==0 (page0 chip0) = empty cell; but page>=1 chip0 is the AT
                // sheet's base variant (1-1: 100000 x10339 = the ground fill),
                // NOT empty — census 2026-10-23. Same for any AT chip.
                if (page == 0u && chip == 0u) { ++missing_cells; continue; }
                if (sheets.empty()) { ++missing_cells; continue; }
                int si;
                uint32_t local;
                if (page == 0u) {
                    si = 0;                            // base sheet, 1-based id
                    local = chip - 1u;
                } else {
                    si = page <= 15u ? (int)page : 15; // page->sheet exact table RE-open (page 16 clamps)
                    local = 0u;                        // MODEL: AT variant rule pending
                }
                if (si >= (int)sheets.size()) { ++band_cells; continue; }
                const ChipSheet& sh = sheets[(size_t)si];
                if (!sh.ready || sh.cols <= 0 || sh.rows <= 0) { ++missing_cells; continue; }
                const uint32_t cap = (uint32_t)sh.cols * (uint32_t)sh.rows;
                if (local >= cap) { ++oob_cells; continue; }  // id beyond the sheet grid
                if (si >= (int)mesh.size()) { ++missing_cells; continue; }
                const int col = (int)(local % (uint32_t)sh.cols);
                const int row = (int)(local / (uint32_t)sh.cols);
                const int csx = sh.w / sh.cols, csy = sh.h / sh.rows;
                const float x0 = (float)tx * (float)chip_size;
                const float y0 = (float)ty * (float)chip_size;
                const float x1 = x0 + (float)chip_size, y1 = y0 + (float)chip_size;
                const float u0 = (float)col * (float)csx / (float)sh.w;
                const float u1 = (float)(col + 1) * (float)csx / (float)sh.w;
                const float v0 = (float)row * (float)csy / (float)sh.h;
                const float v1 = (float)(row + 1) * (float)csy / (float)sh.h;
                auto& m = mesh[(size_t)si];
                const float q[] = {   // 2 tris: tl bl tr / tr bl br
                    x0, y0, u0, v0,  x0, y1, u0, v1,  x1, y0, u1, v0,
                    x1, y0, u1, v0,  x0, y1, u0, v1,  x1, y1, u1, v1 };
                m.insert(m.end(), q, q + 24);
            }
        for (size_t si = 0; si < mesh.size(); ++si) {
            if (mesh[si].empty()) continue;
            GLuint b = 0;
            glGenBuffers(1, &b);
            glBindBuffer(GL_ARRAY_BUFFER, b);
            glBufferData(GL_ARRAY_BUFFER, mesh[si].size() * sizeof(float), mesh[si].data(), GL_STATIC_DRAW);
            Batch bt;
            bt.vbo = b; bt.sheet = (int)si; bt.count = mesh[si].size() / 4u;
            bt.plane = (int)z;
            batches_.push_back(bt);
        }
    }
    build_meshes_locked = true;
}

void Renderer::draw(float cam_x, float cam_y, bool* first_frame_flag) {
    if (!prog_ || !build_meshes_locked || vpw <= 0 || vph <= 0) return;
    if (first_frame_flag && *first_frame_flag) {
        *first_frame_flag = false;
        ALOGI("renderer first frame: chip=%d vp=%dx%d verts=%zu", chip_size, vpw, vph, vert_total());
    }
    glViewport(0, 0, vpw, vph);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog_);
    glUniform2f(u_cam_, cam_x, cam_y);
    glUniform2f(u_vp_, (float)vpw, (float)vph);
    glUniform1i(u_tex_, 0);
    glActiveTexture(GL_TEXTURE0);

    const float white[4] = { 1.f, 1.f, 1.f, 1.f };
    int z_prev = -1;
    for (const Batch& bt : batches_) {
        if (bt.plane < z_prev) continue;             // planes drawn in z order
        z_prev = bt.plane;
        const ChipSheet& sh = (bt.sheet >= 0 && (size_t)bt.sheet < sheets.size())
                                  ? sheets[(size_t)bt.sheet] : ChipSheet();
        glBindTexture(GL_TEXTURE_2D, sh.ready && sh.tex ? sh.tex : white_tex_);
        glUniform4fv(u_col_, 1, white);
        glBindBuffer(GL_ARRAY_BUFFER, bt.vbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
        glDrawArrays(GL_TRIANGLES, 0, (GLint)bt.count);
    }
    eglSwapBuffers(egl_.dpy, egl_.sfc);
}

size_t Renderer::vert_total() const {
    size_t n = 0;
    for (const Batch& bt : batches_) n += bt.count;
    return n;
}

} // namespace wolf
