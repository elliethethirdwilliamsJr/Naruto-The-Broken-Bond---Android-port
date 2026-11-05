package org.brokenbond.nativecheck;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONObject;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public final class MainActivity extends Activity {
    private static final String XEX_SHA = "8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576";
    private static final long XEX_BYTES = 15351808;
    private static final int PICK_XEX = 10;
    private static final int BG = 0xff10171f, CARD = 0xff1b2632, TEXT = 0xffeef4fa;
    private static final int MUTED = 0xffaebccd, ACCENT = 0xff69dbc8, AMBER = 0xfff0c77d, RED = 0xffff9b9b;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final StringBuilder log = new StringBuilder();
    private final TextView[] badges = new TextView[5], details = new TextView[5];
    private Button pick, run, stop, showLog, presentation, gameBoot;
    private TextView overall, fileLabel, logView;
    private File root;
    private volatile Process active;
    private volatile boolean cancelled, destroyed;
    private boolean busy;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        root = new File(getFilesDir(), "native_checks");
        new File(root, "game").mkdirs();
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        if (Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        LinearLayout outer = new LinearLayout(this);
        outer.setOrientation(LinearLayout.VERTICAL); outer.setBackgroundColor(BG);
        outer.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            }
            return insets;
        });
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true);
        LinearLayout page = new LinearLayout(this); page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(dp(22), dp(24), dp(22), dp(24));
        scroll.addView(page); outer.addView(scroll); setContentView(outer);
        page.addView(text("THE BROKEN BOND", 12, ACCENT, true));
        TextView title = text("Native Android checks", 28, TEXT, true); margin(title, 8, 16); page.addView(title);
        TextView notice = text("Development build — original-game boot is experimental.", 15, AMBER, false);
        notice.setPadding(dp(16), dp(14), dp(16), dp(14)); notice.setBackground(panel(0xff302b22));
        page.addView(notice); margin(notice, 0, 22);

        addCard(page, 0, "Original code", "Original routines and guest threads");
        addCard(page, 1, "Texture upload", "BC decoding and Vulkan sampling");
        addCard(page, 2, "Clipping", "Geometry clipping, depth and MSAA");
        addCard(page, 3, "Descriptor bindings", "Bounded arrays and immutable draw snapshots");
        addCard(page, 4, "Production renderer", "Engine draws, uploads, lifetime and readback");

        fileLabel = text("", 13, MUTED, false); margin(fileLabel, 10, 8); page.addView(fileLabel); refreshFile();
        pick = button("Select original default.xex", false); page.addView(pick);
        pick.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
            try { startActivityForResult(intent, PICK_XEX); }
            catch (Exception error) { overall.setText("No file picker is available: " + error.getMessage()); }
        });
        run = button("Run native checks", true); margin(run, 12, 0); page.addView(run);
        run.setOnClickListener(v -> startChecks());
        stop = button("Stop checks", false); stop.setVisibility(View.GONE); page.addView(stop);
        stop.setOnClickListener(v -> { cancelled = true; Process process = active; if (process != null) process.destroyForcibly(); });
        overall = text("Ready. Graphics checks can run without the original file.", 14, MUTED, false);
        margin(overall, 16, 16); page.addView(overall);
        gameBoot = button("Launch original game", true); page.addView(gameBoot);
        gameBoot.setOnClickListener(v -> {
            File external = getExternalFilesDir(null);
            File game = external == null ? null : new File(external, "game");
            if (game == null || !new File(game, "default.xex").isFile() || !new File(game, "naruto.bf").isFile()) {
                overall.setText("The original game files have not been staged on this phone."); return;
            }
            Intent gameIntent = new Intent(this, GameActivity.class);
            gameIntent.putExtra("boot_seconds", getIntent().getIntExtra("game_boot_seconds", 0));
            gameIntent.putExtra("capture_period", getIntent().getIntExtra("game_capture_period", 0));
            gameIntent.putExtra("fast_compile", getIntent().getBooleanExtra("game_fast_compile", false));
            getIntent().removeExtra("game_boot_seconds");
            getIntent().removeExtra("game_capture_period");
            getIntent().removeExtra("game_fast_compile");
            startActivity(gameIntent);
        });
        presentation = button("Test Android presentation", false); page.addView(presentation);
        presentation.setOnClickListener(v -> startActivity(new Intent(this, RendererActivity.class)));
        showLog = button("Show run log", false); page.addView(showLog);
        logView = text("No checks have run in this session.", 11, MUTED, false);
        logView.setTypeface(Typeface.MONOSPACE); logView.setTextIsSelectable(true); logView.setVisibility(View.GONE);
        margin(logView, 14, 0); page.addView(logView);
        showLog.setOnClickListener(v -> {
            boolean show = logView.getVisibility() != View.VISIBLE;
            logView.setVisibility(show ? View.VISIBLE : View.GONE); showLog.setText(show ? "Hide run log" : "Show run log");
        });
        TextView footer = text("ARM64 · research build 0.4.0\nNative original game · experimental build. Gameplay has not been verified.", 12, MUTED, false);
        margin(footer, 22, 0); page.addView(footer);
    }

    private void addCard(LinearLayout page, int index, String name, String description) {
        LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(16), dp(15), dp(16), dp(15)); card.setBackground(panel(CARD));
        LinearLayout row = new LinearLayout(this); row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        TextView title = text(name, 18, TEXT, true);
        row.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        badges[index] = text("NOT RUN", 11, MUTED, true); row.addView(badges[index]); card.addView(row);
        details[index] = text(description, 13, MUTED, false); margin(details[index], 7, 0); card.addView(details[index]);
        page.addView(card); margin(card, 0, 10);
    }
    private TextView text(String value, int size, int color, boolean bold) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        if (bold) view.setTypeface(Typeface.DEFAULT, Typeface.BOLD); view.setLineSpacing(dp(3), 1);
        return view;
    }
    private Button button(String value, boolean primary) {
        Button button = new Button(this); button.setText(value); button.setAllCaps(false); button.setTextSize(15);
        button.setMinHeight(dp(50)); button.setTextColor(primary ? BG : TEXT);
        button.setBackgroundTintList(android.content.res.ColorStateList.valueOf(primary ? ACCENT : CARD));
        return button;
    }
    private GradientDrawable panel(int color) { GradientDrawable d = new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(14)); return d; }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private void margin(View view, int top, int bottom) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(-1, -2); p.setMargins(0, dp(top), 0, dp(bottom)); view.setLayoutParams(p);
    }
    private void refreshFile() {
        File file = new File(root, "game/default.xex");
        fileLabel.setText(file.isFile() ? "Original file selected · verified when checks run" : "Choose default.xex from your extracted game files for the original-code check.");
    }
    private void setBusy(boolean value) {
        busy = value; pick.setEnabled(!value); run.setEnabled(!value); presentation.setEnabled(!value); gameBoot.setEnabled(!value); stop.setVisibility(value ? View.VISIBLE : View.GONE);
    }
    private void status(int index, String badge, String detail, int color) {
        ui(() -> { badges[index].setText(badge); badges[index].setTextColor(color); details[index].setText(detail); });
    }
    private void ui(Runnable task) { if (!destroyed) runOnUiThread(() -> { if (!destroyed) task.run(); }); }
    private void append(String value) {
        synchronized (log) { log.append(value).append('\n'); if (log.length() > 32000) log.delete(0, log.length() - 24000); }
        ui(() -> { synchronized (log) { logView.setText(log.toString()); } });
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_XEX || result != RESULT_OK || data == null || data.getData() == null || busy) return;
        final Uri uri = data.getData(); setBusy(true); overall.setText("Checking the selected original file…");
        worker.execute(() -> {
            File temporary = new File(root, "game/default.xex.import");
            try {
                MessageDigest digest = MessageDigest.getInstance("SHA-256"); long bytes = 0;
                try (InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(temporary)) {
                    if (in == null) throw new IOException("Cannot read the selected file.");
                    byte[] buffer = new byte[65536]; int count;
                    while ((count = in.read(buffer)) != -1) {
                        bytes += count; if (bytes > XEX_BYTES) throw new IOException("Select the supported default.xex (15 MB).");
                        out.write(buffer, 0, count); digest.update(buffer, 0, count);
                    }
                }
                if (bytes != XEX_BYTES || !hex(digest.digest()).equals(XEX_SHA)) throw new IOException("This file is not the supported default.xex revision.");
                Files.move(temporary.toPath(), new File(root, "game/default.xex").toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
                ui(() -> overall.setText("Original file verified. Ready to run all five checks."));
            } catch (Exception error) { append("File selection: " + error); ui(() -> overall.setText(error.getMessage())); }
            finally { temporary.delete(); ui(() -> { setBusy(false); refreshFile(); }); }
        });
    }
    private static String hex(byte[] bytes) {
        StringBuilder out = new StringBuilder(); for (byte b : bytes) out.append(String.format(java.util.Locale.ROOT, "%02x", b & 255)); return out.toString();
    }
    private boolean supportedXex() throws Exception {
        File file = new File(root, "game/default.xex"); if (!file.isFile()) return false;
        if (file.length() != XEX_BYTES) throw new IOException("The original file has an unsupported size. Select it again.");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream in = new FileInputStream(file)) { byte[] b = new byte[65536]; int n; while ((n = in.read(b)) != -1) digest.update(b, 0, n); }
        if (!hex(digest.digest()).equals(XEX_SHA)) throw new IOException("The original file failed verification. Select it again.");
        return true;
    }
    private void copyAsset(String name) throws IOException {
        try (InputStream in = getAssets().open("diagnostics/" + name); OutputStream out = new FileOutputStream(new File(root, name))) {
            byte[] b = new byte[16384]; int n; while ((n = in.read(b)) != -1) out.write(b, 0, n);
        }
    }
    private void startChecks() {
        if (busy) return;
        cancelled = false; setBusy(true); overall.setText("Running native checks…");
        synchronized (log) { log.setLength(0); }
        for (int i = 0; i < 5; ++i) status(i, "WAITING", "Queued for this run", MUTED);
        worker.execute(() -> {
            JSONObject report = new JSONObject(); int passed = 0, failed = 0, skipped = 0;
            try {
                for (String asset : new String[]{"texture_probe.spv", "clip_probe.vert.spv", "clip_probe.frag.spv", "clip_planes_gs.spv", "descriptor_probe.spv"}) copyAsset(asset);
                for (int index = 0; index < 5; ++index) {
                    if (cancelled) { status(index, "STOPPED", "Checks stopped", AMBER); continue; }
                    String key = new String[]{"core", "textures", "clipping", "descriptors", "renderer"}[index];
                    try {
                        if (index == 0 && !supportedXex()) {
                            status(0, "NEEDS FILE", "Select the original default.xex to run this check", AMBER);
                            report.put(key, new JSONObject().put("status", "needs_file")); skipped++; continue;
                        }
                        status(index, "RUNNING", index == 2 ? "Comparing geometry clipping at 1× and 4× MSAA…" : "Running the packaged ARM64 code…", ACCENT);
                        JSONObject result = execute(index); validate(index, result);
                        String detail = index == 0 ? "5 original routines · 3 guest threads · 32,765 registrations"
                            : index == 1 ? "4 formats · 108 mip/face subresources · 17,664 values"
                            : index == 2 ? "2,560 render comparisons · 18 interpolators · 1× / 4× MSAA"
                            : index == 3 ? "105 immutable snapshots · 3 texture dimensions · 2,520 values"
                            : "121 engine draws · uploads, swizzles, readback and resource reuse";
                        status(index, "PASSED", detail, ACCENT); report.put(key, new JSONObject().put("status", "passed").put("result", result)); passed++;
                    } catch (Exception error) {
                        failed++; status(index, cancelled ? "STOPPED" : "FAILED", error.getMessage(), cancelled ? AMBER : RED);
                        report.put(key, new JSONObject().put("status", cancelled ? "stopped" : "failed").put("error", error.toString()));
                        append(key + ": " + error);
                    }
                }
                report.put("package", getPackageName()).put("version", "0.4.0").put("passed", passed).put("failed", failed)
                    .put("needs_file", skipped).put("cancelled", cancelled).put("game_entry_executed", false).put("game_launch_available", false);
                try (OutputStream out = new FileOutputStream(new File(root, "app-result.json"))) { out.write(report.toString(2).getBytes(StandardCharsets.UTF_8)); }
                final String message = cancelled ? "Checks stopped." : passed == 5 ? "All five native checks passed. Original-game boot is experimental."
                    : passed + " passed · " + failed + " failed · " + skipped + " needs original file";
                ui(() -> overall.setText(message));
            } catch (Exception error) { append("Setup: " + error); ui(() -> overall.setText("Could not run checks: " + error.getMessage())); }
            finally { active = null; ui(() -> { setBusy(false); refreshFile(); }); }
        });
    }
    private JSONObject execute(int index) throws Exception {
        final String[] libraries = {"libbb_guest_probe.so", "libbb_texture_probe.so", "libbb_clip_probe.so", "libbb_descriptor_probe.so", "libbb_renderer_probe.so"};
        final String[] results = {"guest-result.json", "texture-result.json", "clip-result.json", "descriptor-result.json", "renderer-result.jsonl"};
        File result = new File(root, results[index]);
        if (result.exists() && !result.delete()) throw new IOException("Cannot clear the previous diagnostic result.");
        String nativeDir = getApplicationInfo().nativeLibraryDir;
        ProcessBuilder builder = new ProcessBuilder(new File(nativeDir, libraries[index]).getAbsolutePath(), index == 4 ? result.getAbsolutePath() : root.getAbsolutePath());
        builder.directory(root); builder.redirectErrorStream(true); builder.environment().put("LD_LIBRARY_PATH", nativeDir);
        append("Starting " + libraries[index]);
        Process process = builder.start(); active = process;
        Thread reader = new Thread(() -> {
            try (BufferedReader input = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
                String line; while ((line = input.readLine()) != null) append(line);
            } catch (IOException error) { if (!cancelled) append("Log reader: " + error); }
        }, "native-check-log"); reader.start();
        try {
            if (!process.waitFor(180, TimeUnit.SECONDS)) { process.destroyForcibly(); throw new IOException("Native check timed out. See the run log."); }
            reader.join(1500);
            if (cancelled) throw new IOException("Checks stopped.");
            if (process.exitValue() != 0) throw new IOException("Native check exited with code " + process.exitValue() + ". See the run log.");
            if (!result.isFile()) throw new IOException("Native check did not write a fresh result.");
            if (index == 4) {
                JSONObject verdict = null; int checks = 0;
                for (String line : Files.readAllLines(result.toPath(), StandardCharsets.UTF_8)) {
                    JSONObject entry = new JSONObject(line);
                    if (entry.optString("kind").equals("verdict")) verdict = entry;
                    else { checks++; if (!entry.optBoolean("ok")) throw new IOException("Renderer check failed: " + line); }
                }
                if (verdict == null) throw new IOException("Renderer verdict is missing.");
                return verdict.put("checks_completed", checks);
            }
            return new JSONObject(new String(Files.readAllBytes(result.toPath()), StandardCharsets.UTF_8));
        } finally { if (process.isAlive()) process.destroyForcibly(); active = null; }
    }
    private void validate(int index, JSONObject result) throws Exception {
        boolean good;
        if (index == 0) good = result.optBoolean("runtime_setup") && result.optBoolean("xex_loaded")
            && result.optInt("registered_functions_checked") == 32765 && result.optInt("original_routines_executed") == 5
            && result.optInt("original_routine_cases") == 8 && result.optInt("guest_threads_completed") == 3;
        else if (index == 1) good = result.optInt("formats_tested") == 4 && result.optInt("images_tested") == 8
            && result.optInt("subresources_tested") == 108 && result.optInt("sampled_values_checked") == 17664
            && result.optInt("policy_and_rejection_checks") == 16;
        else if (index == 2) good = result.optInt("render_comparisons") == 2560 && result.optInt("channel_comparisons") == 7864320
            && result.optInt("interpolators_checked") == 18 && result.optInt("max_polygon_vertices") == 9
            && result.optInt("max_channel_error", 999) <= 1;
        else if (index == 3) good = result.optInt("immutable_snapshots") == 105 && result.optInt("sampled_values_checked") == 2520
            && result.optInt("register_remap_checks") == 41 && result.optInt("unpopulated_rejections") == 3
            && result.optInt("slots_per_array") == 64 && result.optInt("texture_dimensions") == 3
            && !result.optBoolean("descriptor_indexing_enabled");
        else good = result.optBoolean("passed") && result.optInt("checks_completed") == 19;
        if (!good || result.optBoolean("guest_entry_executed") || result.optBoolean("game_renderer_started") || result.optBoolean("graphics_started"))
            throw new IOException("Native result did not complete the expected test contract.");
    }
    @Override protected void onDestroy() {
        destroyed = true; cancelled = true; Process process = active; if (process != null) process.destroyForcibly();
        worker.shutdownNow(); super.onDestroy();
    }
}
