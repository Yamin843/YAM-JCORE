package com.yamgg.modview;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
import android.text.InputType;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

import java.util.concurrent.atomic.AtomicBoolean;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * ModView — OpenGL ES 3.0 surface that hosts the native ImGui UI.
 *
 * The native library (libyamgg.so) attaches an instance of this view to
 * the currently visible Activity's DecorView. This class is only the
 * Android-side plumbing: it forwards surface lifecycle, touch, key, and
 * text input events into the native renderer, and it owns the hidden
 * EditText that gives ImGui a native IME backend.
 *
 * Everything else — window geometry, tab content, RPC to the Java
 * bridge, the entire UI — is implemented in C++ and JS.
 */
public class ModView extends GLSurfaceView implements GLSurfaceView.Renderer {

    private static final String TAG = "YAMGG-ModView";

    // ── Native methods (matched by native_methods.cpp) ────────
    private static native void    nativeOnSurfaceCreated();
    private static native void    nativeOnSurfaceChanged(int width, int height);
    private static native void    nativeOnDrawFrame(int width, int height);
    private static native void    nativeOnTouch(int action, float x, float y, int pointerId);
    private static native void    nativeOnKey(int keyCode, int action);
    private static native void    nativeOnChar(int codepoint);
    private static native void    nativeOnScroll(float dx, float dy);
    private static native boolean nativeWantCaptureMouse();
    private static native boolean nativeWantTextInput();
    private static native void    nativeSetDensity(float density);
    private static native boolean nativeHitTest(float x, float y);
    private static native boolean nativeIsTitleBar(float y);
    private static native String  nativeGetSessionState();
    private static native boolean nativeIsBridgeAvailable();
    private static native String  nativeGetBridgeInfo();
    private static native void    nativeCopyToClipboard(String text);

    // ── Singleton state ──────────────────────────────────────
    private static final Object sLock = new Object();
    private static volatile ModView sInstance = null;
    private static volatile Activity sActivity = null;
    private static volatile EditText sHiddenInput = null;

    // ── Touch state ──────────────────────────────────────────
    // Two-finger drag scroll.
    private boolean twoFingerScrollActive = false;
    private float   twoFingerLastY = 0f;
    private long    lastScrollSentMs = 0L;
    private float   smoothedDy = 0f;

    // Single-finger state.
    private boolean touchOwned    = false;
    private float   touchStartX   = 0f;
    private float   touchStartY   = 0f;
    private long    touchStartMs  = 0L;
    private boolean touchOnTitle  = false;
    private boolean scrollMode    = false;
    private float   lastScrollY   = 0f;

    // Soft keyboard tracking.
    private volatile boolean keyboardShown = false;

    // Density, forwarded to native once, at first frame.
    private final AtomicBoolean densitySent = new AtomicBoolean(false);

    // ═══════════════════════════════════════════════════════════
    //  Construction
    // ═══════════════════════════════════════════════════════════

    public ModView(Context context) {
        super(context);

        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
        setZOrderOnTop(true);
        setRenderer(this);
        setRenderMode(RENDERMODE_CONTINUOUSLY);
        setPreserveEGLContextOnPause(true);

        setFocusable(true);
        setFocusableInTouchMode(true);
        setClickable(true);
        setLongClickable(false);
        setHapticFeedbackEnabled(false);
        setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    // ═══════════════════════════════════════════════════════════
    //  GLSurfaceView.Renderer
    // ═══════════════════════════════════════════════════════════

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        if (!densitySent.getAndSet(true)) {
            float d = getResources().getDisplayMetrics().density;
            try { nativeSetDensity(d); }
            catch (Throwable t) { Log.e(TAG, "nativeSetDensity", t); }
        }
        try { nativeOnSurfaceCreated(); }
        catch (Throwable t) { Log.e(TAG, "nativeOnSurfaceCreated", t); }
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        try { nativeOnSurfaceChanged(width, height); }
        catch (Throwable t) { Log.e(TAG, "nativeOnSurfaceChanged", t); }
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        try { nativeOnDrawFrame(getWidth(), getHeight()); }
        catch (Throwable t) { Log.e(TAG, "nativeOnDrawFrame", t); }
        syncSoftKeyboard();
    }

    // ═══════════════════════════════════════════════════════════
    //  Keyboard sync
    // ═══════════════════════════════════════════════════════════

    private void syncSoftKeyboard() {
        try {
            final boolean want = nativeWantTextInput();
            if (want == keyboardShown) return;
            keyboardShown = want;

            final Activity activity = sActivity;
            if (activity == null) return;

            activity.runOnUiThread(new Runnable() {
                @Override public void run() {
                    try {
                        EditText input = sHiddenInput;
                        if (input == null) return;
                        InputMethodManager imm = (InputMethodManager)
                            activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                        if (imm == null) return;

                        if (want) {
                            input.requestFocus();
                            input.postDelayed(new Runnable() {
                                @Override public void run() {
                                    try {
                                        input.requestFocus();
                                        imm.toggleSoftInput(
                                            InputMethodManager.SHOW_FORCED, 0);
                                    } catch (Throwable t) {}
                                }
                            }, 60);
                        } else {
                            imm.hideSoftInputFromWindow(
                                input.getWindowToken(), 0);
                        }
                    } catch (Throwable t) {
                        Log.e(TAG, "syncSoftKeyboard", t);
                    }
                }
            });
        } catch (Throwable t) {
            // nativeWantTextInput can throw if the renderer isn't ready.
        }
    }

    // ═══════════════════════════════════════════════════════════
    //  Touch dispatch
    // ═══════════════════════════════════════════════════════════

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int pointerCount = event.getPointerCount();

        // ── Two-finger drag → scroll ──
        if (pointerCount >= 2) {
            if (action == MotionEvent.ACTION_POINTER_DOWN) {
                if (!touchOwned) return false;
                try { nativeOnTouch(MotionEvent.ACTION_CANCEL, 0f, 0f, 0); }
                catch (Throwable t) {}
                twoFingerScrollActive = true;
                twoFingerLastY = (event.getY(0) + event.getY(1)) * 0.5f;
                lastScrollSentMs = 0L;
                smoothedDy = 0f;
                return true;
            }
            if (action == MotionEvent.ACTION_MOVE && twoFingerScrollActive) {
                float curY = (event.getY(0) + event.getY(1)) * 0.5f;
                float dy = curY - twoFingerLastY;
                twoFingerLastY = curY;
                smoothedDy = smoothedDy * 0.70f + dy * 0.30f;
                long now = System.currentTimeMillis();
                if (now - lastScrollSentMs >= 8L) {
                    lastScrollSentMs = now;
                    float scroll = -smoothedDy * 0.025f;
                    try { nativeOnScroll(0f, scroll); } catch (Throwable t) {}
                }
                return true;
            }
            if (action == MotionEvent.ACTION_POINTER_UP) {
                twoFingerScrollActive = false;
                smoothedDy = 0f;
                try { nativeOnTouch(MotionEvent.ACTION_CANCEL, 0f, 0f, 0); }
                catch (Throwable t) {}
                return touchOwned;
            }
            if (action == MotionEvent.ACTION_UP
                || action == MotionEvent.ACTION_CANCEL) {
                twoFingerScrollActive = false;
                smoothedDy = 0f;
                touchOwned = false;
                try { nativeOnTouch(action, 0f, 0f, 0); }
                catch (Throwable t) {}
                return true;
            }
            if (twoFingerScrollActive) return true;
        }

        // ── Single-finger ──
        if (action == MotionEvent.ACTION_DOWN) {
            twoFingerScrollActive = false;
            smoothedDy = 0f;

            float x = event.getX();
            float y = event.getY();
            boolean inside = false;
            try { inside = nativeHitTest(x, y); }
            catch (Throwable t) { inside = false; }
            touchOwned = inside;
            if (!touchOwned) return false;

            touchStartX  = x;
            touchStartY  = y;
            lastScrollY  = y;
            touchStartMs = System.currentTimeMillis();
            scrollMode   = false;
            try { touchOnTitle = nativeIsTitleBar(y); }
            catch (Throwable t) { touchOnTitle = false; }

            try { nativeOnTouch(action, x, y, event.getPointerId(0)); }
            catch (Throwable t) {}
            return true;
        }

        if (!touchOwned) return false;

        if (action == MotionEvent.ACTION_MOVE) {
            float x = event.getX();
            float y = event.getY();

            if (!scrollMode && !touchOnTitle) {
                float dx = x - touchStartX;
                float dy = y - touchStartY;
                long dt = System.currentTimeMillis() - touchStartMs;

                // Vertical swipe in content area → enter scroll mode.
                if (dt > 50 && Math.abs(dy) > 30 && Math.abs(dx) < 12) {
                    try { nativeOnTouch(MotionEvent.ACTION_CANCEL, 0f, 0f, 0); }
                    catch (Throwable t) {}
                    scrollMode = true;
                    lastScrollY = y;
                    float delta = -dy * 0.025f;
                    try { nativeOnScroll(0f, delta); } catch (Throwable t) {}
                    return true;
                }
            } else if (scrollMode) {
                // Important: forward ACTION_MOVE before scrolling, so that
                // ImGui's mouse position is up-to-date and the wheel event
                // lands on the right widget.
                try { nativeOnTouch(MotionEvent.ACTION_MOVE, x, y, 0); }
                catch (Throwable t) {}
                float dy = y - lastScrollY;
                lastScrollY = y;
                float delta = -dy * 0.025f;
                try { nativeOnScroll(0f, delta); } catch (Throwable t) {}
                return true;
            }

            try { nativeOnTouch(action, x, y, event.getPointerId(0)); }
            catch (Throwable t) {}
            return true;
        }

        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            boolean wasScroll = scrollMode;
            touchOwned = false;
            scrollMode = false;
            if (!wasScroll) {
                try { nativeOnTouch(action, event.getX(), event.getY(),
                                    event.getPointerId(0)); }
                catch (Throwable t) {}
            }
            return true;
        }

        return false;
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (event.getAction() == MotionEvent.ACTION_SCROLL
            && (event.getSource()
                & android.view.InputDevice.SOURCE_CLASS_POINTER) != 0) {
            float v = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            float h = event.getAxisValue(MotionEvent.AXIS_HSCROLL);
            try { nativeOnScroll(h, v); } catch (Throwable t) {}
            try { return nativeWantCaptureMouse(); } catch (Throwable t) {}
        }
        return super.onGenericMotionEvent(event);
    }

    // ═══════════════════════════════════════════════════════════
    //  Keys
    // ═══════════════════════════════════════════════════════════

    private static boolean isConsumedKey(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BACK:
            case KeyEvent.KEYCODE_ESCAPE:
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_TAB:
            case KeyEvent.KEYCODE_DEL:
            case KeyEvent.KEYCODE_FORWARD_DEL:
            case KeyEvent.KEYCODE_DPAD_UP:
            case KeyEvent.KEYCODE_DPAD_DOWN:
            case KeyEvent.KEYCODE_DPAD_LEFT:
            case KeyEvent.KEYCODE_DPAD_RIGHT:
                return true;
            default:
                return false;
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        try { nativeOnKey(keyCode, 0); } catch (Throwable t) {}
        return isConsumedKey(keyCode) || super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        try { nativeOnKey(keyCode, 1); } catch (Throwable t) {}
        return isConsumedKey(keyCode) || super.onKeyUp(keyCode, event);
    }

    // ═══════════════════════════════════════════════════════════
    //  Lifecycle: attach to Activity, detach
    // ═══════════════════════════════════════════════════════════

    public static ModView getInstance() {
        return sInstance;
    }

    public static Activity getActivity() {
        return sActivity;
    }

    public static void detach() {
        final ModView view;
        final Activity act;
        synchronized (sLock) {
            view = sInstance;
            act  = sActivity;
            sInstance = null;
            sActivity = null;
        }
        if (view == null || act == null) return;

        act.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    ViewGroup parent = (ViewGroup) view.getParent();
                    if (parent != null) parent.removeView(view);
                } catch (Throwable t) {
                    Log.e(TAG, "detach", t);
                }
            }
        });
    }

    /**
     * Attach a ModView to `activity`'s DecorView. If an instance already
     * exists and is attached, only sActivity is updated.
     */
    public static void attach(final Activity activity) {
        if (activity == null) return;

        synchronized (sLock) {
            if (sInstance != null && sInstance.getParent() != null) {
                sActivity = activity;
                return;
            }
        }

        activity.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();
                    if (decor == null) return;

                    // Avoid stacking multiple ModViews.
                    for (int i = 0; i < decor.getChildCount(); i++) {
                        if (decor.getChildAt(i) instanceof ModView) return;
                    }

                    final ModView view = new ModView(activity);
                    synchronized (sLock) {
                        sInstance = view;
                        sActivity = activity;
                    }

                    ViewGroup.LayoutParams lp = new ViewGroup.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT);
                    decor.addView(view, lp);

                    installHiddenInput(decor, activity);
                    Log.i(TAG, "ModView attached");
                } catch (Throwable t) {
                    Log.e(TAG, "attach", t);
                }
            }
        });
    }

    /**
     * The hidden EditText is the IME target. Every committed character
     * and navigation key is forwarded into native code, but the
     * EditText itself stays invisible and empty.
     */
    private static void installHiddenInput(ViewGroup decor, final Activity activity) {
        if (sHiddenInput != null) return;

        EditText et = new EditText(activity) {
            @Override
            public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
                InputConnection ic = super.onCreateInputConnection(outAttrs);
                if (ic == null) return null;

                outAttrs.inputType = InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                    | InputType.TYPE_TEXT_FLAG_MULTI_LINE;
                outAttrs.imeOptions |= EditorInfo.IME_FLAG_NO_EXTRACT_UI
                    | EditorInfo.IME_FLAG_NO_FULLSCREEN
                    | EditorInfo.IME_FLAG_NO_ENTER_ACTION;

                return new InputConnectionWrapper(ic, false) {
                    @Override
                    public boolean commitText(CharSequence text, int newCursor) {
                        try {
                            for (int i = 0; i < text.length(); i++) {
                                int cp = (int) text.charAt(i);
                                if (cp == '\n') {
                                    nativeOnKey(KeyEvent.KEYCODE_ENTER, 0);
                                    nativeOnKey(KeyEvent.KEYCODE_ENTER, 1);
                                } else {
                                    nativeOnChar(cp);
                                }
                            }
                        } catch (Throwable t) {}
                        return true;
                    }

                    @Override
                    public boolean setComposingText(CharSequence text, int newCursor) {
                        return commitText(text, newCursor);
                    }

                    @Override
                    public boolean deleteSurroundingText(int before, int after) {
                        try {
                            for (int i = 0; i < before; i++) {
                                nativeOnKey(KeyEvent.KEYCODE_DEL, 0);
                                nativeOnKey(KeyEvent.KEYCODE_DEL, 1);
                            }
                        } catch (Throwable t) {}
                        return true;
                    }

                    @Override
                    public boolean sendKeyEvent(KeyEvent event) {
                        int code = event.getKeyCode();
                        if (code == KeyEvent.KEYCODE_ENTER
                            || code == KeyEvent.KEYCODE_DEL
                            || code == KeyEvent.KEYCODE_TAB
                            || code == KeyEvent.KEYCODE_ESCAPE) {
                            int act = event.getAction();
                            try {
                                nativeOnKey(code,
                                    act == KeyEvent.ACTION_DOWN ? 0 : 1);
                            } catch (Throwable t) {}
                            return true;
                        }
                        return super.sendKeyEvent(event);
                    }
                };
            }
        };

        et.setLayoutParams(new ViewGroup.LayoutParams(1, 1));
        et.setAlpha(0.01f);
        et.setBackground(null);
        et.setCursorVisible(false);
        et.setTextColor(0);
        et.setPadding(0, 0, 0, 0);
        et.setSingleLine(false);
        et.setInputType(InputType.TYPE_CLASS_TEXT
            | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            | InputType.TYPE_TEXT_FLAG_MULTI_LINE);
        et.setImeOptions(EditorInfo.IME_ACTION_NONE
            | EditorInfo.IME_FLAG_NO_EXTRACT_UI
            | EditorInfo.IME_FLAG_NO_FULLSCREEN
            | EditorInfo.IME_FLAG_NO_ENTER_ACTION);
        et.setFocusable(true);
        et.setFocusableInTouchMode(true);

        decor.addView(et);
        sHiddenInput = et;
    }

    /**
     * Copy text to the system clipboard. Called from native code.
     * Kept as a public static helper so tabs can call it without going
     * through JNI themselves.
     */
    public static void copyToClipboard(String text) {
        Activity activity = sActivity;
        if (activity == null || text == null) return;
        try {
            ClipboardManager cm = (ClipboardManager)
                activity.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm != null) {
                cm.setPrimaryClip(ClipData.newPlainText("YAM-JCORE", text));
            }
        } catch (Throwable t) {
            Log.e(TAG, "copyToClipboard", t);
        }
    }
}
