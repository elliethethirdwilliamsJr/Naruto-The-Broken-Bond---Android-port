package org.brokenbond.nativecheck;

import android.os.Bundle;
import android.content.pm.ActivityInfo;
import android.widget.Toast;
import android.widget.FrameLayout;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;

/** Development boot of original code in its own process, with real SDL services. */
public final class GameActivity extends SDLActivity {
    private File session;
    private File game;
    private TouchControls controls;
    static native void setTouchButtons(int buttons);
    static native void resetTouchButtons();
    static native int pendingGraphicsJobs();

    @Override protected void onCreate(Bundle state) {
        File external = getExternalFilesDir(null);
        game = external == null ? null : new File(external, "game");
        if (game == null || !new File(game, "default.xex").isFile() || !new File(game, "Naruto.BF").isFile() && !new File(game, "naruto.bf").isFile()) {
            super.onCreate(state);
            Toast.makeText(this, "Original game files are not staged on this phone.", Toast.LENGTH_LONG).show();
            finish(); return;
        }
        session = new File(getFilesDir(), "game_boot/run-" + System.currentTimeMillis());
        session.mkdirs();
        try (FileOutputStream out = new FileOutputStream(new File(session, "android-process.txt"))) {
            out.write(Integer.toString(android.os.Process.myPid()).getBytes(StandardCharsets.UTF_8));
        } catch (Exception error) { throw new IllegalStateException("Could not record game process", error); }
        try (FileOutputStream out = new FileOutputStream(new File(getFilesDir(), "game_boot/latest-session.txt"))) {
            out.write(session.getAbsolutePath().getBytes(StandardCharsets.UTF_8));
        } catch (Exception error) { throw new IllegalStateException("Could not create game session", error); }
        super.onCreate(state);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        controls = new TouchControls(this);
        addContentView(controls, new FrameLayout.LayoutParams(-1, -1));
    }
    @Override protected void onResume() { super.onResume(); if (controls != null) controls.startMonitoring(); }
    @Override protected void onPause() {
        if (controls != null) { controls.releaseAll(); controls.stopMonitoring(); }
        super.onPause();
    }
    @Override public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // SDL maps two landscape hints to USER_LANDSCAPE, which follows the
        // phone's portrait rotation lock. The original game always needs a
        // landscape surface, including when that system lock is enabled.
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }
    @Override protected String[] getLibraries() {
        return new String[]{"rexruntime", "broken_bond_game"};
    }
    @Override protected String[] getArguments() {
        if (session == null || game == null) return new String[0];
        int seconds = Math.max(0, Math.min(600, getIntent().getIntExtra("boot_seconds", 0)));
        int capturePeriod = Math.max(0, Math.min(3600, getIntent().getIntExtra("capture_period", 0)));
        ArrayList<String> arguments = new ArrayList<>(Arrays.asList(session.getAbsolutePath(),
            "--game_data_root=" + game.getAbsolutePath(),
            "--user_data_root=" + new File(getFilesDir(), "game_user").getAbsolutePath(),
            "--cache_root=" + new File(getFilesDir(), "game_cache").getAbsolutePath(),
            "--native_backend=vulkan", "--fullscreen=true", "--android_boot_seconds=" + seconds,
            "--android_capture_period=" + capturePeriod,
            "--android_fast_pipeline_compile=" + getIntent().getBooleanExtra("fast_compile", false),
            "--log_level=" + (seconds > 0 || capturePeriod > 0 ? "debug" : "info")));
        if (seconds > 0 || capturePeriod > 0) arguments.add("--native_renderer_bootstrap_path=" + new File(session, "frontend.jsonl").getAbsolutePath());
        return arguments.toArray(new String[0]);
    }
}
