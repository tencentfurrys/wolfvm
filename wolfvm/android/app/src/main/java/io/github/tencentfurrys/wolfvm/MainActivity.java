package io.github.tencentfurrys.wolfvm;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.util.Log;
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
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * WolfVM Shell (Stage 4): fullscreen map viewer + gesture floor, with the
 * harness console available in a slide-down overlay (toggle: double-tap).
 *
 * Game data: bundled data is the player's own copy, imported in-app
 * (console ▸ "import zip" / "import dir" — zip the decrypted Data tree first;
 * .wolf archives stay PC-side via UberWolf). Legacy path: /sdcard/WolfVM with
 * All-files access. Root auto-detects a Data/BasicData/Game.dat subtree.
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
    private TextView bootView;
    private volatile boolean bootedShow = false;
    private final StringBuilder bootRing = new StringBuilder();
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
        setContentView(buildUi());
        loadRoot();
        log("== " + WolfVm.version() + "\n");
        autoImportAssetPack();
    }

    // ------------------------------------------------- bundled asset pack
    // PERSONAL TEST builds only (tools/mkdatapack.ps1): the pack is the
    // player's own game data, gitignored, never committed or uploaded.
    // Reuses the SAF "import zip" pipeline incl. root auto-detect.
    private void autoImportAssetPack() {
        if (new File(gameDir(), "Data/BasicData/Game.dat").isFile()) return;
        new Thread(() -> {
            final File tmp = new File(getCacheDir(), "bundled_wolfvm_data.zip");
            boolean have;
            try (InputStream in = getAssets().open("wolfvm_data.zip");
                 OutputStream os = new FileOutputStream(tmp)) {
                byte[] buf = new byte[1 << 16];
                int rd, staged = 0;
                while ((rd = in.read(buf)) > 0) { os.write(buf, 0, rd); staged += rd; }
                have = true;
                log("[pack] staged " + (staged >> 20) + " MB from the APK asset\n");
            } catch (IOException noPack) {
                have = false;
            }
            if (have) {
                log("[import] bundled data-pack found — auto-importing\n");
                importZip(Uri.fromFile(tmp));
            } else {
                log("[data] no bundled pack — import via zip/dir or set a root\n");
            }
        }, "import-asset-pack").start();
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
        mapLabel.setText("no session — double-tap the map to toggle the console");
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
        rootEdit.setSingleLine(true);
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
        HorizontalRow rows3 = new HorizontalRow();
        rows3.addButton("import zip", v -> pickZip());
        rows3.addButton("import dir", v -> pickDir());
        rows3.addButton("save root", v -> saveRoot());
        rows3.addButton("clear", v -> logView.setText(""));
        console.addView(rows3.view);

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
        // boot canvas — opaque until the map session starts, mirrors the last
        // log lines; a double-tap still opens the console above it on failure.
        bootView = new TextView(this);
        bootView.setBackgroundColor(Color.BLACK);
        bootView.setTextColor(Color.rgb(124, 201, 124));
        bootView.setTypeface(Typeface.MONOSPACE);
        bootView.setTextSize(12);
        bootView.setPadding(dp(14), dp(14), dp(14), dp(14));
        bootView.setText("WOLFVM\nbooting…\n");
        bootView.setOnClickListener(v -> toggleConsole());   // boot/failed canvas: tap = console
        root.addView(bootView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        root.addView(consoleWrap, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(340),
                Gravity.BOTTOM | Gravity.START));
        console.setVisibility(View.GONE);   // hidden at game boot; double-tap toggles
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

    // ------------------------------------------------------------ root/prefs
    private File gameDir() {   // app-owned import target (no permissions needed)
        return new File(getExternalFilesDir(null), "game");
    }

    private String defaultRoot() {
        return new File(Environment.getExternalStorageDirectory(), "WolfVM").getAbsolutePath();
    }

    private void loadRoot() {
        String saved = getSharedPreferences("wolfvm", MODE_PRIVATE).getString("root", null);
        mapIdx = getSharedPreferences("wolfvm", MODE_PRIVATE).getInt("mapIdx", 0);
        File g = gameDir();
        if (new File(g, "Data/BasicData/Game.dat").isFile()) rootEdit.setText(g.getAbsolutePath());
        else rootEdit.setText(saved != null ? saved : defaultRoot());
    }

    private void saveRoot() {
        getSharedPreferences("wolfvm", MODE_PRIVATE).edit()
                .putString("root", rootEdit.getText().toString().trim()).apply();
        Toast.makeText(this, "root saved", Toast.LENGTH_SHORT).show();
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

    // -------------------------------------------------- data import (SAF)
    /** Zip the DECRYPTED data tree (UberWolf output; .wolf stays PC-side). */
    private void pickZip() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, REQ_ZIP);
    }

    private void pickDir() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        startActivityForResult(i, REQ_DIR);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        if (requestCode == REQ_ZIP) new Thread(() -> importZip(data.getData()), "import-zip").start();
        if (requestCode == REQ_DIR) new Thread(() -> importDir(data.getData()), "import-dir").start();
    }

    private void importZip(Uri uri) {
        File dest = gameDir();
        dest.mkdirs();
        int files = 0, skip = 0;
        try (ZipInputStream zis = new ZipInputStream(getContentResolver().openInputStream(uri))) {
            byte[] buf = new byte[1 << 16];
            ZipEntry e;
            while ((e = zis.getNextEntry()) != null) {
                String name = e.getName().replace('\\', '/');
                if (name.contains("..")) { ++skip; continue; }
                File out = new File(dest, name);
                if (e.isDirectory()) { out.mkdirs(); continue; }
                File parent = out.getParentFile();
                if (parent != null) parent.mkdirs();
                try (OutputStream os = new FileOutputStream(out)) {
                    int n;
                    while ((n = zis.read(buf)) > 0) os.write(buf, 0, n);
                }
                ++files;
                if ((files % 250) == 0) log("[import] ... " + files + " files\n");
            }
        } catch (Exception ex) {
            log("[import] zip FAILED: " + ex + " (extracted " + files + " files before)\n");
            return;
        }
        log("[import] zip ok: " + files + " files" + (skip > 0 ? " (" + skip + " skipped)" : "")
                + " → " + dest + "\n");
        adoptImport(dest);
    }

    /** Recursively copy a SAF-picked tree (Data/ or the whole game root). */
    private void importDir(Uri tree) {
        try {
            getContentResolver().takePersistableUriPermission(tree,
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException ignored) { }
        File dest = gameDir();
        dest.mkdirs();
        final int[] n = { 0 };
        boolean ok;
        try {
            copyTree(tree, tree, dest, n);
            ok = true;
        } catch (Exception ex) {
            log("[import] dir FAILED: " + ex + "\n");
            ok = false;
        }
        if (ok) {
            log("[import] dir ok: " + n[0] + " files → " + dest + "\n");
            adoptImport(dest);
        }
    }

    private void copyTree(Uri tree, Uri dir, File outDir, int[] n) throws Exception {
        outDir.mkdirs();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                tree, DocumentsContract.getDocumentId(dir));
        String[] cols = { DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                          DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                          DocumentsContract.Document.COLUMN_MIME_TYPE };
        try (Cursor c = getContentResolver().query(children, cols, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                final String name = c.getString(0);
                final String docId = c.getString(1);
                final String mime = c.getString(2);
                if (name.contains("/")) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    copyTree(tree, DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId),
                            new File(outDir, name), n);
                } else {
                    final File of = new File(outDir, name);
                    try (InputStream in = getContentResolver().openInputStream(
                            DocumentsContract.buildDocumentUriUsingTree(tree, docId));
                         OutputStream os = new FileOutputStream(of)) {
                        byte[] buf = new byte[1 << 16];
                        int rd;
                        while ((rd = in.read(buf)) > 0) os.write(buf, 0, rd);
                    }
                    ++n[0];
                }
            }
        }
        // auto-detect the root: walk up by rewriting text only — caller handles
    }

    /** Find the extracted subtree owning Data/BasicData/Game.dat, adopt it. */
    private void adoptImport(File dest) {
        String root = null;
        File game = findGameRoot(dest, 0);
        if (game != null) root = game.getAbsolutePath();
        if (root != null) {
            rootEdit.setText(root);
            log("[import] root detected: " + root + "\n");
        } else {
            log("[import] no Data/BasicData/Game.dat under the import — check the zip\n");
        }
        if (root != null) ui.post(() -> {
            scanMaps();
            if (!maps.isEmpty() && sfcW > 0) createSession();
            else log("[import] no .mps found — map data missing from the zip\n");
        });
    }

    private File findGameRoot(File dir, int depth) {
        if (depth > 4) return null;
        if (new File(dir, "Data/BasicData/Game.dat").isFile()) return dir;
        File[] subs = dir.listFiles();
        if (subs != null)
            for (File f : subs)
                if (f.isDirectory()) {
                    File hit = findGameRoot(f, depth + 1);
                    if (hit != null) return hit;
                }
        return null;
    }

    // -------------------------------------------------- surface callbacks
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        log("[surface] created\n");
        new Thread(this::createSession, "wolfvm-start").start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        sfcW = w; sfcH = h;
        log("[surface] changed " + w + "x" + h
                + (session != 0 ? " (session resize)\n" : " (no session yet)\n"));
        if (session != 0) WolfVm.sessionResize(session, w, h);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        log("[surface] destroyed\n");
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
        if (sfcW == 0) {  // surfaceCreated can fire before surfaceChanged
            log("[session] surface not sized yet — waiting for surfaceChanged\n");
            return;
        }
        stopSession();
        scanMaps();
        if (maps.isEmpty()) {
            log("[session] no .mps under " + p("Data/MapData") + "\n");
            log("  → import your game data: console ▸ import zip / import dir\n"
                + "    (zip the DECRYPTED Data tree — UberWolf decrypt, then zip)\n"
                + "  → or type a root and use locate\n");
            if (Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
                ui.post(this::ensureStorageAccess);
            }
            return;
        }
        final String mps = maps.get(mapIdx);
        log("[session] startSession(" + new File(mps).getParentFile().getName()
                + "/" + new File(mps).getName() + ") …\n");
        final long h = WolfVm.startSession(rootEdit.getText().toString().trim(),
                maps.get(mapIdx), 0, map.getHolder().getSurface());
        if (h <= 0) {
            log("[session] start FAILED: " + WolfVm.lastError() + "\n");
            return;
        }
        session = h;
        getSharedPreferences("wolfvm", MODE_PRIVATE).edit().putInt("mapIdx", mapIdx).apply();
        booted(true);
        WolfVm.sessionResize(h, sfcW, sfcH);
        log("[session] started map " + (mapIdx + 1) + "/" + maps.size() + ": "
                + new File(maps.get(mapIdx)).getName() + "\n");
        uploaderLive = true;
        new Thread(this::uploadSheets, "chip-uploader").start();
        statusLine();
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
        mapIdx = (mapIdx + dir + Math.max(1, maps.size())) % Math.max(1, maps.size());
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
        String ce = p("Data/BasicData/CommonEvent.dat");
        if (!exists(ce)) { log("ERROR: missing " + ce + " (import the data first)\n"); return; }
        List<String> a = new ArrayList<>();
        a.add("sweep");
        a.add(ce);
        a.add("--from"); a.add("0");
        a.add("--to");   a.add("20");
        String dbp = p("Data/BasicData/DataBase.project"), dbd = p("Data/BasicData/DataBase.dat");
        String cbp = p("Data/BasicData/CDataBase.project"), cbd = p("Data/BasicData/CDataBase.dat");
        if (exists(dbp) && exists(dbd)) { a.add("--db"); a.add(dbp); a.add(dbd); }
        if (exists(cbp) && exists(cbd)) { a.add("--cdb"); a.add(cbp); a.add(cbd); }
        runCmd("sweep 0..20", a.toArray(new String[0]));
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
        // every console line also lands in logcat ("adb logcat -s wolfvm")
        Log.i(LOGTAG, s.replace('\n', ' ').trim());
        ui.post(() -> {
            logView.append(s);
            logScroll.post(() -> logScroll.smoothScrollTo(0, Integer.MAX_VALUE));
            bootFeed(s);
        });
    }

    /** hide the boot canvas once a session runs; keep it for failure diagnosis. */
    private void booted(final boolean up) {
        bootedShow = up;
        ui.post(() -> {
            if (bootView != null) bootView.setVisibility(up ? View.GONE : View.VISIBLE);
        });
    }

    /** boot-canvas feed: keep roughly the last 15 lines while it covers the map. */
    private void bootFeed(final String s) {
        if (bootView == null || bootView.getVisibility() != View.VISIBLE) return;
        if (bootRing.length() > 8192) {
            final String keep = bootRing.substring(bootRing.length() - 2048);
            bootRing.setLength(0);
            bootRing.append(keep);
        }
        bootRing.append(s);
        final String txt = bootRing.toString();
        final String[] lines = txt.split("\n", -1);
        final StringBuilder sb = new StringBuilder();
        for (int i = Math.max(0, lines.length - 15); i < lines.length; ++i)
            sb.append(lines[i]).append('\n');
        bootView.setText(sb.toString());
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
    private void ensureStorageAccess() {   // only for the legacy /sdcard path
        if (Build.VERSION.SDK_INT < 30) return;   // legacy devices: use import
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

    private static final int REQ_ZIP = 41, REQ_DIR = 42;
    private static final String LOGTAG = "wolfvm";
}
