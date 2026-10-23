// wolfvm_jni.cpp — Android bridge (Stage 4):
//   (a) console surface: WolfVm.cmd(argv) == run_cli dispatch, pipe-captured;
//   (b) map session surface: GameSession on a dedicated thread fed by the
//       gesture overlay (tap/drag/long) and Java-decoded chip sheets.
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "wolfvm_game.h"
#include "wolfvm_input.h"

int run_cli(int argc, char** argv);

#define LOG_TAG "wolfvm"
#define ALOG(c, ...) ((void)__android_log_print(ANDROID_LOG_##c, LOG_TAG, __VA_ARGS__))
#define ALOGI(...) ALOG(INFO, __VA_ARGS__)
#define ALOGW(...) ALOG(WARN, __VA_ARGS__)
#define ALOGE(...) ALOG(ERROR, __VA_ARGS__)

namespace {

// ------------------------------------------------------------- console bit
void sanitize(std::string& s) {
    for (char& c : s) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (b < 0x20 && c != '\n' && c != '\t') c = ' ';
        else if (b > 0x7E) c = '?';
    }
}

std::string capture_run_cli(const std::vector<std::string>& args, int& exit_code) {
    int out_pipe[2];
    if (pipe(out_pipe) != 0) return "ERROR: pipe() failed\n";
    const int saved_out = dup(STDOUT_FILENO);
    const int saved_err = dup(STDERR_FILENO);
    fflush(nullptr);
    dup2(out_pipe[1], STDOUT_FILENO);
    dup2(out_pipe[1], STDERR_FILENO);

    std::string captured;
    std::thread reader([&captured, &out_pipe] {
        char buf[65536];
        ssize_t n;
        while ((n = read(out_pipe[0], buf, sizeof buf)) > 0)
            captured.append(buf, static_cast<size_t>(n));
    });

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    int rc = 1; bool threw = false;
    try {
        rc = run_cli(static_cast<int>(argv.size() - 1), argv.data());
    } catch (const std::exception& e) {
        threw = true;
        captured += "\nERROR: exception: "; captured += e.what(); captured += "\n";
    } catch (...) {
        threw = true;
        captured += "\nERROR: unknown exception\n";
    }

    fflush(nullptr);
    dup2(saved_out, STDOUT_FILENO);
    dup2(saved_err, STDERR_FILENO);
    close(saved_out);
    close(saved_err);
    close(out_pipe[1]);
    reader.join();
    close(out_pipe[0]);

    exit_code = rc;
    sanitize(captured);
    std::string out = "exit=" + std::to_string(rc) + (threw ? " (exception caught)" : "") + "\n" + captured;
    const size_t cap = 600000;
    if (out.size() > cap) out.erase(0, out.size() - cap);
    return out;
}

// ------------------------------------------------------------- session bit
struct Session {
    wolf::GameSession game;
    ANativeWindow* window = nullptr;
    std::thread thread;
    std::atomic<bool> run{ false };
    std::atomic<bool> paused{ false };

    Session(const wolf::SessionDeps& d) : game(d) {}
    void loop() {
        while (run) {
            if (!paused) game.tick_frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60fps anchor
        }
    }
};

std::mutex g_sessions_mutex;
std::map<jlong, std::unique_ptr<Session>> g_sessions;   // handle -> session
jlong g_next_handle = 1;
std::string g_last_error;

wolf::GameSession* session_of(jlong h) {
    std::lock_guard<std::mutex> g(g_sessions_mutex);
    const auto it = g_sessions.find(h);
    return it == g_sessions.end() ? nullptr : &it->second->game;
}

} // namespace

// ============================================================ console JNI
extern "C" JNIEXPORT jstring JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_cmd(JNIEnv* env, jclass /*clazz*/, jobjectArray jargs) {
    std::vector<std::string> args;
    if (jargs) {
        const jsize n = env->GetArrayLength(jargs);
        args.reserve(static_cast<size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            jstring js = static_cast<jstring>(env->GetObjectArrayElement(jargs, i));
            if (!js) continue;
            const char* raw = env->GetStringUTFChars(js, nullptr);
            if (raw) {
                args.emplace_back(raw);
                env->ReleaseStringUTFChars(js, raw);
            }
            env->DeleteLocalRef(js);
        }
    }
    if (args.empty()) args.push_back("selftest");
    int rc = 0;
    const std::string out = capture_run_cli(args, rc);
    return env->NewStringUTF(out.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_version(JNIEnv* env, jclass /*clazz*/) {
    return env->NewStringUTF(
        "wolfvm shell 0.4-android — dat parsers + stage2 VM + stage3 engine"
        " page-pred + GLES map session + gesture floor (" __DATE__ ")");
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_lastError(JNIEnv* env, jclass /*clazz*/) {
    std::lock_guard<std::mutex> g(g_sessions_mutex);
    return env->NewStringUTF(g_last_error.c_str());
}

// ============================================================ session JNI
extern "C" JNIEXPORT jlong JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_startSession(
        JNIEnv* env, jclass /*clazz*/, jstring jroot, jstring jmap, jint tileset, jobject jsurface) {
    const char* root = env->GetStringUTFChars(jroot, nullptr);
    const char* map = env->GetStringUTFChars(jmap, nullptr);
    wolf::SessionDeps d;
    d.root = root ? root : "";
    d.map_path = map ? map : "";
    d.tileset_index = tileset;
    if (root) env->ReleaseStringUTFChars(jroot, root);
    if (map) env->ReleaseStringUTFChars(jmap, map);

    auto s = std::make_unique<Session>(d);
    const std::string err = s->game.load();
    if (!err.empty()) {
        std::lock_guard<std::mutex> g(g_sessions_mutex);
        g_last_error = "session load: " + err;
        ALOGE("%s", g_last_error.c_str());
        return 0;
    }
    ANativeWindow* win = ANativeWindow_fromSurface(env, jsurface);
    if (!win) {
        std::lock_guard<std::mutex> g(g_sessions_mutex);
        g_last_error = "ANativeWindow unavailable";
        return 0;
    }
    s->window = win;
    if (!s->game.renderer_init(win, (int32_t)ANativeWindow_getWidth(win),
                               (int32_t)ANativeWindow_getHeight(win))) {
        std::lock_guard<std::mutex> g(g_sessions_mutex);
        g_last_error = "renderer init failed";
        ANativeWindow_release(win);
        return 0;
    }
    s->run = true;
    const jlong h = g_next_handle++;
    {
        std::lock_guard<std::mutex> g(g_sessions_mutex);
        g_sessions[h] = std::move(s);
    }
    Session& sess = *g_sessions[h];
    sess.thread = std::thread([&sess] {
        try { sess.loop(); }
        catch (const std::exception& e) { ALOGE("session thread: %s", e.what()); }
    });
    return h;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_stopSession(JNIEnv* /*env*/, jclass /*clazz*/, jlong h) {
    std::unique_ptr<Session> s;
    {
        std::lock_guard<std::mutex> g(g_sessions_mutex);
        const auto it = g_sessions.find(h);
        if (it == g_sessions.end()) return;
        s = std::move(it->second);
        g_sessions.erase(it);
    }
    s->run = false;
    if (s->thread.joinable()) s->thread.join();
    s->game.renderer_shutdown();
    if (s->window) { ANativeWindow_release(s->window); s->window = nullptr; }
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_pauseSession(JNIEnv* /*env*/, jclass /*clazz*/, jlong h, jboolean p) {
    std::lock_guard<std::mutex> g(g_sessions_mutex);
    const auto it = g_sessions.find(h);
    if (it != g_sessions.end()) it->second->paused = (bool)p;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_sessionResize(JNIEnv* env, jclass /*clazz*/, jlong h, jint w, jint hh) {
    std::lock_guard<std::mutex> g(g_sessions_mutex);
    const auto it = g_sessions.find(h);
    if (it != g_sessions.end()) it->second->game.renderer_resize(w, hh);
    (void)env;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_gesture(JNIEnv* /*env*/, jclass /*clazz*/, jlong h,
                                                   jint kind, jfloat x, jfloat y, jfloat dx, jfloat dy) {
    wolf::GameSession* g = session_of(h);
    if (g) g->input().gesture(kind, x, y, dx, dy);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_chipCount(JNIEnv* /*env*/, jclass /*clazz*/, jlong h) {
    wolf::GameSession* g = session_of(h);
    return g ? (jint)g->chip_paths().size() : 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_chipPath(JNIEnv* env, jclass /*clazz*/, jlong h, jint i) {
    wolf::GameSession* g = session_of(h);
    if (!g || i < 0 || (size_t)i >= g->chip_paths().size()) return env->NewStringUTF("");
    return env->NewStringUTF(g->chip_paths()[(size_t)i].c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_chipUpload(
        JNIEnv* env, jclass /*clazz*/, jlong h, jint idx, jobject jbuf, jint w, jint hh) {
    wolf::GameSession* g = session_of(h);
    if (!g) return JNI_FALSE;
    void* addr = env->GetDirectBufferAddress(jbuf);
    if (!addr) return JNI_FALSE;
    return g->renderer_upload_sheet(idx, addr, w, hh) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_chipDone(JNIEnv* /*env*/, jclass /*clazz*/, jlong h) {
    wolf::GameSession* g = session_of(h);
    if (g) g->renderer_upload_done();
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_tencentfurrys_wolfvm_WolfVm_sessionStatus(JNIEnv* env, jclass /*clazz*/, jlong h) {
    wolf::GameSession* g = session_of(h);
    if (!g) return env->NewStringUTF("");
    const auto d = g->diag();
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "map %dx%d chip=%d sheets=%zu/%zu cells(miss/oob)=%zu/%zu "
                  "events=%zu active=%zu cam=(%.0f,%.0f) tileset=%d/%d%s",
                  d.map_w, d.map_h, d.chip, d.sheets_ready, d.sheets,
                  d.missing_cells, d.oob_cells, d.events, d.active_objects,
                  d.cam_x, d.cam_y, d.tileset, d.rows_tileset,
                  d.atlas_ready ? "" : " [atlas not ready]");
    return env->NewStringUTF(buf);
}
