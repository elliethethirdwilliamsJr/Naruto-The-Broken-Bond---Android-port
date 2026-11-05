package org.brokenbond.nativecheck;

import android.app.Activity;
import android.os.Bundle;
import android.os.Build;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

/** Presents through the real Vulkan backend. This never enters the game. */
public final class RendererActivity extends Activity implements SurfaceHolder.Callback {
    private static final ExecutorService renderer = Executors.newSingleThreadExecutor();
    private static final Throwable loadError;
    static {
        Throwable error = null;
        try { System.loadLibrary("broken_bond_renderer_jni"); }
        catch (Throwable failure) { error = failure; }
        loadError = error;
    }
    private static native String runRenderer(Surface surface, String output, AtomicBoolean cancelled);
    private AtomicBoolean cancellation;
    private TextView status;
    private boolean started;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setPadding(24, 32, 24, 32);
        page.setBackgroundColor(0xff10171f);
        page.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(24 + bars.left, 32 + bars.top, 24 + bars.right, 32 + bars.bottom);
            }
            return insets;
        });
        TextView title = new TextView(this);
        title.setText("Production Vulkan renderer test"); title.setTextSize(22); title.setTextColor(0xffeef4fa);
        page.addView(title);
        TextView note = new TextView(this);
        note.setText("This tests Android presentation using the engine backend and full mobile shader cache. The colored test pattern is not game footage.");
        note.setTextSize(14); note.setTextColor(0xfff0c77d); note.setPadding(0, 16, 0, 16); page.addView(note);
        SurfaceView surface = new SurfaceView(this);
        surface.getHolder().setFixedSize(640, 480);
        surface.getHolder().addCallback(this);
        page.addView(surface, new LinearLayout.LayoutParams(-1, 0, 1));
        status = new TextView(this);
        status.setText("Waiting for the Android surface…"); status.setTextSize(15); status.setTextColor(0xffeef4fa);
        status.setPadding(0, 16, 0, 16); page.addView(status);
        Button back = new Button(this); back.setText("Back to checks");
        back.setOnClickListener(v -> finish()); page.addView(back);
        setContentView(page);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        if (started || isFinishing()) return;
        started = true;
        if (loadError != null) { status.setText("Renderer library could not load: " + loadError); return; }
        cancellation = new AtomicBoolean(false);
        final AtomicBoolean token = cancellation;
        final Surface surface = holder.getSurface();
        final File output = new File(getFilesDir(), "renderer-surface-result.jsonl");
        status.setText("Initializing the real Vulkan backend…");
        renderer.execute(() -> {
            String result;
            try { result = runRenderer(surface, output.getAbsolutePath(), token); }
            catch (Throwable error) { result = "Renderer test failed: " + error; }
            final String message = result;
            runOnUiThread(() -> { if (!isDestroyed() && !isFinishing()) status.setText(message); });
        });
    }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { }
    @Override public void surfaceDestroyed(SurfaceHolder holder) { cancel(); }
    @Override protected void onPause() { cancel(); super.onPause(); }
    @Override protected void onDestroy() { cancel(); super.onDestroy(); }
    private void cancel() { if (cancellation != null) cancellation.set(true); }
}
