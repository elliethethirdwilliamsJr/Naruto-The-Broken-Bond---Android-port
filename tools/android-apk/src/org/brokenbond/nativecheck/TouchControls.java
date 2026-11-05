package org.brokenbond.nativecheck;

import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.widget.FrameLayout;
import android.widget.TextView;

/** Touch buttons feed the same guest controller state as a physical gamepad. */
final class TouchControls extends FrameLayout {
    private int held;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final TextView preparation;
    private final Runnable update = new Runnable() {
        @Override public void run() {
            preparation.setVisibility(GameActivity.pendingGraphicsJobs() > 0 ? VISIBLE : GONE);
            handler.postDelayed(this, 300);
        }
    };
    TouchControls(Context context) {
        super(context);
        setFocusable(false); setClickable(false); setMotionEventSplittingEnabled(true);
        FrameLayout dpad = new FrameLayout(context);
        LayoutParams dp = new LayoutParams(dp(156), dp(156), Gravity.BOTTOM | Gravity.LEFT);
        dp.leftMargin = dp(18); dp.bottomMargin = dp(16); addView(dpad, dp);
        button(dpad, "▲", "Up", 0x0001, 52, 0, 48, 48, Color.WHITE);
        button(dpad, "▼", "Down", 0x0002, 52, 104, 48, 48, Color.WHITE);
        button(dpad, "◀", "Left", 0x0004, 0, 52, 48, 48, Color.WHITE);
        button(dpad, "▶", "Right", 0x0008, 104, 52, 48, 48, Color.WHITE);

        FrameLayout face = new FrameLayout(context);
        LayoutParams fp = new LayoutParams(dp(132), dp(120), Gravity.BOTTOM | Gravity.RIGHT);
        fp.rightMargin = dp(20); fp.bottomMargin = dp(20); addView(face, fp);
        button(face, "A", "A — confirm", 0x1000, 0, 56, 60, 60, 0xffa9f5bc);
        button(face, "B", "B — cancel", 0x2000, 68, 0, 60, 60, 0xffffb7b7);

        FrameLayout middle = new FrameLayout(context);
        LayoutParams mp = new LayoutParams(dp(188), dp(42), Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        mp.bottomMargin = dp(18); addView(middle, mp);
        button(middle, "BACK", "Back", 0x0020, 0, 0, 82, 42, Color.WHITE);
        button(middle, "START", "Start", 0x0010, 100, 0, 88, 42, Color.WHITE);

        preparation = new TextView(context);
        preparation.setText("Preparing game…"); preparation.setTextSize(15);
        preparation.setTextColor(Color.WHITE); preparation.setGravity(Gravity.CENTER);
        preparation.setPadding(dp(16), dp(8), dp(16), dp(8));
        preparation.setBackgroundColor(0xb0101822); preparation.setVisibility(GONE);
        LayoutParams pp = new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        pp.topMargin = dp(12); addView(preparation, pp);
    }
    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private void button(FrameLayout parent, String label, String description, int mask,
                        int x, int y, int width, int height, int color) {
        TextView button = new TextView(getContext());
        button.setText(label); button.setContentDescription(description); button.setGravity(Gravity.CENTER);
        button.setTextSize(label.length() > 2 ? 12 : 22); button.setTextColor(color);
        button.setFocusable(false); button.setClickable(true); button.setAlpha(.6f);
        GradientDrawable background = new GradientDrawable(); background.setColor(0xbb122235);
        background.setCornerRadius(dp(20)); background.setStroke(dp(1), 0x99ffffff); button.setBackground(background);
        LayoutParams p = new LayoutParams(dp(width), dp(height)); p.leftMargin = dp(x); p.topMargin = dp(y);
        parent.addView(button, p);
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    held |= mask; button.setAlpha(.95f); GameActivity.setTouchButtons(held); return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    held &= ~mask; button.setAlpha(.6f); GameActivity.setTouchButtons(held); return true;
                default: return true;
            }
        });
    }
    void releaseAll() { held = 0; GameActivity.resetTouchButtons(); }
    void startMonitoring() { handler.removeCallbacks(update); handler.post(update); }
    void stopMonitoring() { handler.removeCallbacks(update); }
    @Override protected void onDetachedFromWindow() { stopMonitoring(); releaseAll(); super.onDetachedFromWindow(); }
}
