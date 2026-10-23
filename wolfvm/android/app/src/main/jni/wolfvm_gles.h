// wolfvm_gles.h — map-session renderer surface (Stage 4 round 2, approx/MODEL).
// EGL + GLES3; draws the 3 verified .mps chip planes from per-sheet textures
// uploaded by the Java side (BitmapFactory), panned by the gesture camera.
#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <vector>

namespace wolf {

// One decoded chip sheet (PNG): GPU texture + MODEL-derived sheet geometry.
struct ChipSheet {
    GLuint tex = 0;
    int w = 0, h = 0;        // pixel dims of the PNG
    int cols = 0, rows = 0;  // chip grid under the detected chip size
    bool ready = false;      // texture uploaded
};

// One draw batch: every tile of one plane referencing one sheet.
struct Batch {
    GLuint vbo = 0;
    int plane = -1;
    int sheet = -1;
    size_t count = 0;        // vertices (6 per tile)
};

struct Renderer {
    bool init(void* window, int32_t w, int32_t h);   // ANativeWindow* + viewport px
    void resize(int32_t w, int32_t h);
    void shutdown();                                  // full teardown + EGL release

    bool upload_sheet(int idx, const void* rgba_bytes, int w, int h);

    // Mesh: per-(plane, sheet) static buffers; planes drawn in z order.
    // sheet_start[s] = first flat chip id of sheet s (MODEL: sheet order).
    void build_plane_meshes(const std::vector<std::vector<uint32_t>>& planes,
                            int w, int h, const std::vector<int>& sheet_start);
    void draw(float cam_x, float cam_y, bool* first_frame_flag);
    size_t vert_total() const;

    std::vector<ChipSheet> sheets;
    int32_t vpw = 0, vph = 0;
    int chip_size = 40;   // MODEL heuristic until the engine value decodes
    size_t missing_cells = 0, oob_cells = 0;

private:
    std::vector<Batch> batches_;
    bool build_meshes_locked = false;
    GLuint prog_ = 0, white_tex_ = 0;
    GLint u_cam_ = -1, u_vp_ = -1, u_tex_ = -1, u_col_ = -1;
    struct { EGLDisplay dpy = EGL_NO_DISPLAY; EGLContext ctx = EGL_NO_CONTEXT; EGLSurface sfc = EGL_NO_SURFACE; } egl_{};
    void* win_ = nullptr;
};

} // namespace wolf
