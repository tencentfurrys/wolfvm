package io.github.tencentfurrys.wolfvm;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileFilter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * WolfVM Shell (Stage 4): fullscreen map viewer + gesture floor, with the
 * harness console available in a slide-down overlay (toggle: double-tap).
 *
 * Game data layout (README): <root>/Data/BasicData/* + <root>/Data/MapData/*.mps.
 * Default root /storage/emulated/0/WolfVM — copy the decrypted game data there
 * (restore-data.ps1 flow) or edit the path in the console overlay.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {

    // gesture kinds — must mirror wolfvm_input.h
    private static final int KIND_TAP = 0, KIND_DRAG = 1, KIND_LONG = 2, KIND_DOUBLE = 3;

    private SurfaceView map;
    private volatile long session = 0;
    private volatile int sfcW = 0, sfcH = 0;
    private LinearLayout console;
    private TextView logView;
    private TextView mapLabel;
    private ScrollView logScroll;
    private EditText rootEdit;
    private List<String> maps = Collections.emptyList();
    private volatile int mapIdx = 0;
    private volatile boolean uploaderLive = false;
    private final Handler ui = new Handler(Looper.getMainLooper());

    // gesture bookkeeping
    private float gDownX, gDownY, gLastX, gLastY, gAccX, gAccY;
    private long gDownAt = 0, traveled = 0, lastTapAt = 0;
    private boolean gMoved = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ensureStorageAccess();
        setContentView(buildUi());
        log("== " + WolfVm.version() + "\n");
    }

    // ------------------------------------------------------------------ UI
    private View buildUi() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);

        map = new SurfaceView(this);
        map.getHolder().addCallback(this);
        map.setOnTouchListener(this::onMapTouch);
        root.addView(map, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        mapLabel = new TextView(this);
        mapLabel.setTextColor(Color.argb(200, 124, 201, 124));
        mapLabel.setPadding(dp(8), dp(4), dp(8), dp(4));
        mapLabel.setTypeface(Typeface.MONOSPACE);
        root.addView(mapLabel, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.START));

        Button consoleBtn = new Button(this);
        consoleBtn.setText("≡");
        consoleBtn.setTextSize(18);
        consoleBtn.setOnClickListener(v -> toggleConsole());
        root.addView(consoleBtn, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.END));

        console = new LinearLayout(this);
        console.setOrientation(LinearLayout.VERTICAL);
        console.setBackgroundColor(Color.argb(216, 10, 10, 10));

        rootEdit = new EditText(this);
        rootEdit.setText(defaultRoot());
        rootEdit.setHint("game data root dir");
        rootEdit.setTextSize(11);
        console.addView(rootEdit, row());

        HorizontalRow rows1 = new HorizontalRow();
        rows1.addButton("locate", v -> locate());
        rows1.addButton("selftest", v -> runCmd("selftest", "selftest"));
        rows1.addButton("gameinfo", v -> runCmd("gameinfo", p("Data/BasicData/Game.dat")));
        rows1.addButton("CE info", v -> runCmd("commoninfo", p("Data/BasicData/CommonEvent.dat")));
        console.addView(rows1.view);
        HorizontalRow rows2 = new HorizontalRow();
        rows2.addButton("sweep 0..20", v -> sweepProbe());
        rows2.addButton("◀ map", v -> switchMap(-1));
        rows2.addButton("map ➤", v -> switchMap(1));
        rows2.addButton("status", v -> status());
        console.addView(rows2.view);

        logScroll = new ScrollView(this);
        logView = new TextView(this);
        logView.setTypeface(Typeface.MONOSPACE, 0);
        logView.setTextSize(10f);
        logView.setTextColor(Color.rgb(224, 224, 224));
        logView.setPadding(dp(6), dp(6), dp(6), dp(6));
        logScroll.addView(logView, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        console.addView(logScroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout consoleWrap = new LinearLayout(this);
        consoleWrap.setOrientation(LinearLayout.VERTICAL);
        consoleWrap.addView(console, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        root.addView(consoleWrap, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(300),
                Gravity.BOTTOM | Gravity.START));
        console.setVisibility(View.GONE);
        return root;
    }

    private class HorizontalRow {
        final LinearLayout view = new LinearLayout(MainActivity.this);
        HorizontalRow() {
            view.setOrientation(LinearLayout.HORIZONTAL);
            view.setPadding(dp(4), dp(2), dp(4), 0);
        }
        void addButton(String label, View.OnClickListener onClick) {
            Button b = new Button(MainActivity.this);
            b.setText(label);
            b.setTextSize(11);
            b.setOnClickListener(onClick);
            view.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        }
    }

    private LinearLayout.LayoutParams row() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(4), dp(2), dp(4), dp(2));
        return lp;
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    private void toggleConsole() {
        console.setVisibility(console.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE);
    }

    // ------------------------------------------------------------ lifecycle
    private String defaultRoot() {
        return new File(Environment.getExternalStorageDirectory(), "WolfVM").getAbsolutePath();
    }

    private String p(String rel) {
        return new File(rootEdit.getText().toString().trim(), rel).getAbsolutePath();
    }

    private boolean exists(String path) {
        return path != null && new File(path).isFile();
    }

    private void scanMaps() {
        File dir = new File(p("Data/MapData"));
        List<String> found = new ArrayList<>();
        File[] list = dir.listFiles(new FileFilter() {
            @Override public boolean accept(File f) { return f.isFile() && f.getName().endsWith(".mps"); }
        });
        if (list != null) {
            for (File f : list) found.add(f.getAbsolutePath());
            Collections.sort(found);
        }
        maps = found;
        if (mapIdx >= maps.size()) mapIdx = 0;
    }

    // -------------------------------------------------- surface callbacks
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        new Thread(this::createSession, "wolfvm-start").start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        sfcW = w; sfcH = h;
        if (session != 0) WolfVm.sessionResize(session, w, h);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        stopSession();
        sfcW = 0; sfcH = 0;
    }

    private void stopSession() {
        uploaderLive = false;
        ui.removeCallbacks(statusTick);
        final long h = session;
        session = 0;
        if (h != 0) WolfVm.stopSession(h);
    }

    private void createSession() {
        if (!Environment.isExternalStorageManager()) {
            log("(grant All files access first — session blocked)\n");
            return;
        }
        if (sfcW == 0) {  // surfaceCreated can fire before surfaceChanged
            log("(surface not sized yet; recreate session via surface change)\n");
            return;
        }
        stopSession();
        scanMaps();
        if (maps.isEmpty()) {
            log("[session] no .mps under " + p("Data/MapData") + " — copy game data first\n");
            return;
        }
        final long h = WolfVm.startSession(rootEdit.getText().toString().trim(),
                maps.get(mapIdx), 0, map.getHolder().getSurface());
        if (h <= 0) {
            log("[session] start FAILED: " + WolfVm.lastError() + "\n");
            return;
        }
        session = h;
        WolfVm.sessionResize(h, sfcW, sfcH);
        log("[session] started map " + (mapIdx + 1) + "/" + maps.size() + ": "
                + new File(maps.get(mapIdx)).getName() + "\n");
        uploaderLive = true;
        new Thread(this::uploadSheets, "chip-uploader").start();
        ui.postDelayed(this::statusLine, 1500);
    }

    private void uploadSheets() {
        try {
            final long h = session;
            if (h == 0) return;
            final int n = WolfVm.chipCount(h);
            for (int i = 0; i < n && uploaderLive && session == h; ++i) {
                final String path = WolfVm.chipPath(h, i);
                if (path == null || path.isEmpty()) continue;
                BitmapFactory.Options o = new BitmapFactory.Options();
                o.inJustDecodeBounds = true;
                BitmapFactory.decodeFile(path, o);
                BitmapFactory.Options d = new BitmapFactory.Options();
                d.inScaled = false;
                d.inPreferredConfig = Bitmap.Config.ARGB_8888;
                Bitmap bm = BitmapFactory.decodeFile(path, d);
                if (bm == null) {
                    WolfVm.chipUpload(h, i, ByteBuffer.allocateDirect(4).order(ByteOrder.nativeOrder()), 1, 1);
                    continue;
                }
                final int W = bm.getWidth(), H = bm.getHeight();
                ByteBuffer bb = ByteBuffer.allocateDirect(W * H * 4).order(ByteOrder.nativeOrder());
                bm.copyPixelsToBuffer(bb);
                bb.rewind();
                bm.recycle();
                WolfVm.chipUpload(h, i, bb, W, H);
            }
            if (session == h) WolfVm.chipDone(h);
        } catch (Throwable t) {
            log("[chips] upload error: " + t + "\n");
        }
    }

    private void status() {
        final long h = session;
        log((h == 0 ? "[status] no session" : "[status] " + WolfVm.sessionStatus(h)) + "\n");
    }

    private final Runnable statusTick = new Runnable() {
        @Override public void run() {
            final long h = session;
            if (h != 0) {
                final String st = WolfVm.sessionStatus(h);
                ui.post(() -> mapLabel.setText(st));
            }
            ui.postDelayed(this, 2000);
        }
    };

    private void statusLine() {
        ui.postDelayed(statusTick, 50);
    }

    private void switchMap(int dir) {
        stopSession();
        mapIdx = (mapIdx + dir + maps.size()) % Math.max(1, maps.size());
        log("[maps] switch to " + (mapIdx + 1) + "/" + maps.size() + "\n");
        if (sfcW > 0) createSession();
    }

    // --------------------------------------------------------------- console
    private void runCmd(String title, String... argv) {
        List<String> a = new ArrayList<>(Arrays.asList(argv));
        for (String tok : a)
            if ((tok.startsWith("/storage") || tok.startsWith("/sdcard")) && !exists(tok)) {
                log("ERROR: missing " + tok + "\n");
                return;
            }
        log("\n== " + title + "\n$ " + join(a) + "\n");
        new Thread(() -> {
            final String out = WolfVm.cmd(a.toArray(new String[0]));
            log(out.startsWith("exit=") ? out : "exit=1\n" + out);
        }, "wolfvm-cmd").start();
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
        runCmdList("sweep 0..20", a);
    }

    private void runCmdList(String title, List<String> a) {
        runCmd(title, a.toArray(new String[0]));
    }

    private void locate() {
        String[] files = {
                "Data/BasicData/Game.dat", "Data/BasicData/CommonEvent.dat",
                "Data/BasicData/DataBase.project", "Data/BasicData/DataBase.dat",
                "Data/BasicData/CDataBase.project", "Data/BasicData/CDataBase.dat",
                "Data/BasicData/SysDatabase.project", "Data/BasicData/SysDatabase.dat",
                "Data/BasicData/SysDataBaseBasic.dat", "Data/BasicData/MapTree.dat",
                "Data/BasicData/MapTreeOpenStatus.dat", "Data/BasicData/TileSetData.dat",
                "Data/MapData/1-1.mps",
        };
        StringBuilder sb = new StringBuilder("exit=0\n[locate] ");
        File root = new File(rootEdit.getText().toString().trim());
        sb.append(root.exists() ? "root ok" : "root MISSING").append(": ").append(root).append('\n');
        int ok = 0;
        for (String f : files) {
            boolean e = new File(root, f).isFile();
            if (e) ok++;
            sb.append(e ? "  ok    " : "  miss  ").append(f).append('\n');
        }
        sb.append("  coverage: ").append(ok).append('/').append(files.length).append('\n');
        log(sb.toString());
    }

    private static String join(List<String> a) {
        StringBuilder sb = new StringBuilder();
        for (String s : a) {
            if (sb.length() > 0) sb.append(' ');
            if (s.contains(" ")) sb.append('"').append(s).append('"'); else sb.append(s);
        }
        return sb.toString();
    }

    private void log(final String s) {
        ui.post(() -> {
            logView.append(s);
            logScroll.post(() -> logScroll.smoothScrollTo(0, Integer.MAX_VALUE));
        });
    }

    // -------------------------------------------------------------- gestures
    private boolean onMapTouch(View v, MotionEvent e) {
        final long h = session;
        if (h == 0) return false;
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                gDownX = gLastX = e.getX(); gDownY = gLastY = e.getY();
                gAccX = gAccY = traveled = 0;
                gMoved = false;
                gDownAt = System.currentTimeMillis();
                return true;
            case MotionEvent.ACTION_MOVE: {
                final float dx = e.getX() - gLastX, dy = e.getY() - gLastY;
                gLastX = e.getX(); gLastY = e.getY();
                gAccX += dx; gAccY += dy;
                traveled += Math.sqrt((double) dx * dx + (double) dy * dy);
                if (traveled > 12) {
                    gMoved = true;
                    WolfVm.gesture(h, KIND_DRAG, e.getX(), e.getY(), gAccX, gAccY);
                    gAccX = gAccY = 0;
                }
                return true;
            }
            case MotionEvent.ACTION_UP: {
                final long dur = System.currentTimeMillis() - gDownAt;
                final long now = System.currentTimeMillis();
                if (!gMoved) {
                    if (now - lastTapAt < 300) {
                        lastTapAt = 0;
                        WolfVm.gesture(h, KIND_DOUBLE, e.getX(), e.getY(), 0, 0);
                        toggleConsole();
                    } else if (dur > 400) {
                        lastTapAt = 0;
                        WolfVm.gesture(h, KIND_LONG, e.getX(), e.getY(), 0, 0);
                    } else {
                        lastTapAt = now;
                        WolfVm.gesture(h, KIND_TAP, e.getX(), e.getY(), 0, 0);
                    }
                }
                return true;
            }
            default:
                return false;
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (session != 0) WolfVm.pauseSession(session, true);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (session != 0) WolfVm.pauseSession(session, false);
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
        } catch (SecurityException ex) {
            Toast.makeText(this, "storage grant failed: " + ex, Toast.LENGTH_LONG).show();
        }
    }
}
