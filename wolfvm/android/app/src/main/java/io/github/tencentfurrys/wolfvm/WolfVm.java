package io.github.tencentfurrys.wolfvm;

/** Thin binding to libwolfvm.so (WolfVm.cmd == the CLI dispatch, run_cli). */
public final class WolfVm {
    static { System.loadLibrary("wolfvm"); }

    /**
     * Run one CLI invocation on-device, stdout+stderr captured.
     * argv mirrors the native harness: e.g.
     *   {"selftest"}
     *   {"gameinfo", "/sdcard/WolfVM/Data/BasicData/Game.dat"}
     *   {"sweep", ce, "--from", "0", "--to", "20", "--db", proj, dat}
     * argv[0] is the command (defaults to selftest when empty).
     * Returned string is "exit=N\n&lt;captured output&gt;"; it never throws —
     * captured C++ exceptions surface as exit=1 text.
     */
    public static native String cmd(String[] argv);

    /** Library/VM version string for the on-device banner. */
    public static native String version();

    private WolfVm() {}
}
