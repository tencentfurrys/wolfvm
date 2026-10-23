// wolfvm_jni.cpp — Android shell bridge (Stage 4).
// Single generic entry: Java_wolfVm_cmd(argv) rebuilds an argv and calls
// run_cli (main.cpp dispatch), capturing stdout/stderr into a Java string.
// Capture design: pipe(+dup2) around the synchronous call, drained by a
// reader thread started before the swap — no forks, no java.io plumbing.
#include <jni.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <thread>

int run_cli(int argc, char** argv);

namespace {

// The game data strings can carry arbitrary bytes; keep them log-safe (ASCII
// view) so NewStringUTF can't abort on invalid Modified UTF-8.
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

    int rc = 1;
    bool threw = false;
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
    std::string out = "exit=" + std::to_string(rc) + (threw ? " (exception caught)" : "") +
                      "\n" + captured;
    const size_t cap = 600000;                 // EditText sanity cap
    if (out.size() > cap) out.erase(0, out.size() - cap);
    return out;
}

} // namespace

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
        "wolfvm shell 0.3-android — stage1 dat parsers + stage2 VM + stage3"
        " engine page-pred (NDK build " __DATE__ ")");
}
