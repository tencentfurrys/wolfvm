package io.github.tencentfurrys.wolfvm;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * WolfVM Shell (Stage 4, headless): a console over the native VM core.
 * Game data layout unchanged from the README:
 *   &lt;root&gt;/Data/BasicData/*  +  &lt;root&gt;/Data/MapData/*.mps
 * Default root /storage/emulated/0/WolfVM — copy the decrypted game data
 * there (UberWolfCli restore, see restore-data.ps1) or edit the path.
 */
public class MainActivity extends Activity {

    private EditText rootEdit;
    private TextView logView;
    private ScrollView logScroll;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ensureStorageAccess();
        setContentView(buildUi());
        banner(WolfVm.version());
        runCmd("selftest", "selftest");
    }

    // ------------------------------------------------------------------ UI
    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        rootEdit = new EditText(this);
        rootEdit.setText(defaultRoot());
        rootEdit.setHint("game data root dir");
        rootEdit.setBackgroundColor(Color.argb(30, 124, 201, 124));
        root.addView(rootEdit, row(0, dp(40)));

        HorizontalRow rows1 = new HorizontalRow();
        rows1.addButton("selftest", v -> runCmd("selftest", "selftest"));
        rows1.addButton("locate data", v -> locate());
        rows1.addButton("game info", v -> runCmd("gameinfo",
                p("Data/BasicData/Game.dat")));
        rows1.addButton("CE info", v -> runCmd("commoninfo",
                p("Data/BasicData/CommonEvent.dat")));
        root.addView(rows1.view);
        HorizontalRow rows2 = new HorizontalRow();
        rows2.addButton("sweep 0..20", v -> sweepProbe());
        rows2.addButton("map info", v -> runCmd("mpsinfo", firstMap(), "-c"));
        rows2.addButton("map obj", v -> runCmd("mapobj", firstMpsOr("1-1.mps"), "--ticks", "1"));
        rows2.addButton("clear", v -> logView.setText(""));
        root.addView(rows2.view);

        logView = new TextView(this);
        logView.setTypeface(Typeface.MONOSPACE, 0);
        logView.setTextSize(11f);
        logView.setTextColor(Color.rgb(224, 224, 224));
        logView.setPadding(dp(6), dp(6), dp(6), dp(6));
        logScroll = new ScrollView(this);
        logScroll.addView(logView, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        logScroll.setBackgroundColor(Color.rgb(16, 16, 16));
        root.addView(logScroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        return root;
    }

    /** Helper row of buttons that scrolls horizontally. */
    private class HorizontalRow {
        final LinearLayout view = new LinearLayout(MainActivity.this);
        HorizontalRow() {
            view.setOrientation(LinearLayout.HORIZONTAL);
            view.setPadding(dp(4), dp(4), dp(4), 0);
        }
        void addButton(String label, View.OnClickListener onClick) {
            Button b = new Button(MainActivity.this);
            b.setText(label);
            b.setOnClickListener(onClick);
            view.addView(b, row(dp(4), ViewGroup.LayoutParams.WRAP_CONTENT));
        }
    }

    private LinearLayout.LayoutParams row(int w, int h) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(w, h);
        lp.setMargins(dp(4), dp(4), dp(4), dp(4));
        return lp;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    // ------------------------------------------------------------ commands
    private void banner(String s) {
        log("== " + s + "\n");
    }

    private String defaultRoot() {
        return new File(Environment.getExternalStorageDirectory(), "WolfVM").getAbsolutePath();
    }

    private String p(String rel) {
        return new File(rootEdit.getText().toString().trim(), rel).getAbsolutePath();
    }

    private boolean exists(String path) {
        return path != null && new File(path).isFile();
    }

    private void sweepProbe() {
        List<String> a = new ArrayList<>();
        a.add("sweep");
        a.add(p("Data/BasicData/CommonEvent.dat"));
        a.add("--from"); a.add("0");
        a.add("--to");   a.add("20");
        String dbp = p("Data/BasicData/DataBase.project"), dbd = p("Data/BasicData/DataBase.dat");
        String cbp = p("Data/BasicData/CDataBase.project"), cbd = p("Data/BasicData/CDataBase.dat");
        if (exists(dbp) && exists(dbd)) { a.add("--db"); a.add(dbp); a.add(dbd); }
        if (exists(cbp) && exists(cbd)) { a.add("--cdb"); a.add(cbp); a.add(cbd); }
        runList("sweep 0..20", a);
    }

    private void locate() {
        String[] files = {
                "Data/BasicData/Game.dat",
                "Data/BasicData/CommonEvent.dat",
                "Data/BasicData/DataBase.project", "Data/BasicData/DataBase.dat",
                "Data/BasicData/CDataBase.project", "Data/BasicData/CDataBase.dat",
                "Data/BasicData/SysDatabase.project", "Data/BasicData/SysDatabase.dat",
                "Data/BasicData/SysDataBaseBasic.dat",
                "Data/BasicData/MapTree.dat",
                "Data/BasicData/MapTreeOpenStatus.dat",
                "Data/BasicData/TileSetData.dat",
                "Data/MapData/1-1.mps",
        };
        StringBuilder sb = new StringBuilder("exit=0\n[locate] ");
        File root = new File(rootEdit.getText().toString().trim());
        sb.append(root.exists() ? "root ok" : "root MISSING");
        sb.append(": ").append(root).append('\n');
        int ok = 0;
        for (String f : files) {
            boolean e = new File(root, f).isFile();
            if (e) ok++;
            sb.append(e ? "  ok    " : "  miss  ").append(f).append('\n');
        }
        File maps = new File(root, "Data/MapData");
        int n = maps.isDirectory() && maps.listFiles() != null ? maps.listFiles().length : 0;
        sb.append("  MapData entries: ").append(n)
          .append(" | required-file coverage: ").append(ok).append('/').append(files.length).append('\n');
        log(sb.toString());
    }

    /** First .mps in Data/MapData (lexicographic); falls back to 1-1.mps text. */
    private String firstMap() {
        File dir = new File(p("Data/MapData"));
        File[] list = dir.listFiles((d, name) -> name.endsWith(".mps"));
        if (list != null && list.length > 0) {
            java.util.Arrays.sort(list);
            return list[0].getAbsolutePath();
        }
        return p("Data/MapData/1-1.mps");
    }

    private String firstMpsOr(String fallback) {
        return firstMap();
    }

    private void runCmd(String title, String... argv) {
        List<String> a = new ArrayList<>(argv.length);
        for (String s : argv) a.add(s);
        runList(title, a);
    }

    private void runList(final String title, final List<String> argv) {
        log("\n== " + title + "\n");
        final String joined = join(argv);
        log("$ " + joined + "\n");
        // Missing files fail hard in the native reader (read_file exit(2)):
        // pre-validate so the shell reports instead of killing the process.
        if (!validatePaths(joined)) return;
        new Thread(() -> {
            final String out = WolfVm.cmd(argv.toArray(new String[0]));
            log(out.startsWith("exit=") ? out : "exit=1\n" + out);
        }, "wolfvm-cmd").start();
    }

    private boolean validatePaths(String joined) {
        boolean ok = true;
        for (String tok : joined.split(" ")) {
            if (tok.startsWith("/sdcard") || tok.startsWith("/storage")) {
                if (!exists(tok)) {
                    log("ERROR: file missing: " + tok + "\n");
                    ok = false;
                }
            }
        }
        if (!ok) log("(copy the game data to the root path above, then retry)\n");
        return ok;
    }

    private static String join(List<String> a) {
        StringBuilder sb = new StringBuilder();
        for (String s : a) {
            if (sb.length() > 0) sb.append(' ');
            if (s.contains(" ")) sb.append('\"').append(s).append('\"'); else sb.append(s);
        }
        return sb.toString();
    }

    private void log(final String s) {
        ui.post(() -> {
            logView.append(s);
            logScroll.post(() -> logScroll.smoothScrollTo(0, Integer.MAX_VALUE));
        });
    }

    // --------------------------------------------------------- permissions
    private void ensureStorageAccess() {
        if (Environment.isExternalStorageManager()) return;
        try {
            Intent i = new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
            Toast.makeText(this, "grant All files access, then return", Toast.LENGTH_LONG).show();
        } catch (SecurityException e) {
            Toast.makeText(this, "storage grant failed: " + e, Toast.LENGTH_LONG).show();
        }
    }
}
