package io.github.tencentfurrys.wolfvm;

import android.view.Surface;

import java.nio.ByteBuffer;

/** Binding to libwolfvm.so:
 *  - cmd() == the CLI dispatch (run_cli), stdout+stderr captured;
 *  - session surface: GameSession on a native thread fed by the gesture
 *    overlay and Java-decoded chip sheets (Stage 4 map viewer). */
public final class WolfVm {
    static { System.loadLibrary("wolfvm"); }

    // ---------------------------------------------------------- console
    /**
     * Run one CLI invocation on-device (argv mirrors the native harness,
     * e.g. {"selftest"} / {"gameinfo", "/sdcard/WolfVM/Data/BasicData/Game.dat"}).
     * Returns "exit=N\n&lt;captured output&gt;"; never throws.
     */
    public static native String cmd(String[] argv);

    /** Library/VM version string for the on-device banner. */
    public static native String version();

    /** Last session-load error ("session load: …") — read when start fails. */
    public static native String lastError();

    // ---------------------------------------------------------- session
    /** Load the dat layer + open the surface context; handle&gly;0 on success. */
    public static native long startSession(String root, String mapPath, int tileset, Surface sfc);

    /** Stop the game thread and release the EGL surface. */
    public static native void stopSession(long h);

    public static native void pauseSession(long h, boolean pause);
    public static native void sessionResize(long h, int w, int h2);

    /** Gesture floor (approx — gesture kinds mirror wolfvm_input.h). */
    public static native void gesture(long h, int kind, float x, float y, float dx, float dy);

    /** Chip sheets of the selected tileset (paths resolvable under root). */
    public static native int chipCount(long h);
    public static native String chipPath(long h, int i);

    /** Upload one decoded sheet: ARGB_8888 bytes (premultiplied), direct buf. */
    public static native boolean chipUpload(long h, int idx, ByteBuffer rgba, int w, int h2);

    /** Sheets uploaded — native builds the tile meshes; draw loop starts. */
    public static native void chipDone(long h);

    /** One-line session diagnostics for the console overlay. */
    public static native String sessionStatus(long h);

    private WolfVm() {}
}
