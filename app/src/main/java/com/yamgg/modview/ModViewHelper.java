package com.yamgg.modview;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.util.Log;

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * ModViewHelper — attach ModView to the current Activity.
 *
 * Register an Application.ActivityLifecycleCallbacks so we are notified
 * the moment an Activity reaches onResume. From there we can call
 * ModView.attach(activity). This avoids polling for the current Activity
 * and works on any Android version.
 *
 * Registration itself is done from native code once, after the Java
 * classes have been loaded and the ModView class is resolvable through
 * the DEX loader.
 */
public class ModViewHelper implements Application.ActivityLifecycleCallbacks {

    private static final String TAG = "YAMGG-MVH";

    private static final AtomicBoolean sInstalled = new AtomicBoolean(false);
    private static volatile ModViewHelper sInstance = null;

    /** The Activity currently resumed, if any. */
    private static volatile Activity sCurrentActivity = null;

    private ModViewHelper() {}

    /**
     * Install the lifecycle callbacks on the given Application.
     * Idempotent. Called from native code via JNI after Application
     * startup.
     */
    public static void install(Application application) {
        if (application == null) return;
        if (!sInstalled.compareAndSet(false, true)) return;

        ModViewHelper helper = new ModViewHelper();
        sInstance = helper;
        application.registerActivityLifecycleCallbacks(helper);
        Log.i(TAG, "lifecycle callbacks installed");

        // Try to attach to whatever Activity is already resumed, if any.
        Activity existing = sCurrentActivity;
        if (existing != null) {
            ModView.attach(existing);
        }
    }

    /**
     * The Activity most recently resumed, or null if none.
     * Called from native code to attach the UI right now.
     */
    public static Activity currentActivity() {
        return sCurrentActivity;
    }

    // ═══════════════════════════════════════════════════════════
    //  Application.ActivityLifecycleCallbacks
    // ═══════════════════════════════════════════════════════════

    @Override
    public void onActivityCreated(Activity activity, Bundle savedInstanceState) {
        sCurrentActivity = activity;
    }

    @Override
    public void onActivityStarted(Activity activity) {
        sCurrentActivity = activity;
    }

    @Override
    public void onActivityResumed(Activity activity) {
        sCurrentActivity = activity;
        try {
            ModView.attach(activity);
        } catch (Throwable t) {
            Log.e(TAG, "onActivityResumed", t);
        }
    }

    @Override
    public void onActivityPaused(Activity activity) {
        // Do not detach here: a short pause is common when the keyboard
        // or a system dialog appears. Detach only on destroy.
    }

    @Override
    public void onActivityStopped(Activity activity) {
        // Same as above: pause/stop do not detach.
    }

    @Override
    public void onActivitySaveInstanceState(Activity activity, Bundle outState) {
        // Nothing.
    }

    @Override
    public void onActivityDestroyed(Activity activity) {
        if (sCurrentActivity == activity) {
            sCurrentActivity = null;
        }
        try {
            Activity modActivity = ModView.getActivity();
            if (modActivity == activity) {
                ModView.detach();
            }
        } catch (Throwable t) {
            Log.e(TAG, "onActivityDestroyed", t);
        }
    }
}
