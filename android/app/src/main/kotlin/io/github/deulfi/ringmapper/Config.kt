package io.github.deulfi.ringmapper

import android.content.Context

/** Mapping settings, stored in SharedPreferences and written to the daemon's config.txt. */
object Config {
    val DIRS = listOf("up", "down", "left", "right")

    /** value to label; "custom" becomes "key:<n>" in the config file. */
    val ACTIONS = listOf(
        "scroll_up" to "Scroll up",
        "scroll_down" to "Scroll down",
        "swipe" to "Touch swipe",
        "alt_swipe" to "Alt swipe",
        "dpad_up" to "D-pad up",
        "dpad_down" to "D-pad down",
        "dpad_left" to "D-pad left",
        "dpad_right" to "D-pad right",
        "page_up" to "Page up",
        "page_down" to "Page down",
        "volume_up" to "Volume up",
        "volume_down" to "Volume down",
        "media_prev" to "Media previous",
        "media_next" to "Media next",
        "media_play_pause" to "Media play/pause",
        "space" to "Space",
        "enter" to "Enter",
        "back" to "Back",
        "custom" to "Custom keycode…",
        "none" to "None",
    )

    private val DEFAULTS = mapOf("up" to "scroll_up", "down" to "scroll_down", "left" to "dpad_left", "right" to "dpad_right")

    fun prefs(ctx: Context) = ctx.getSharedPreferences("config", Context.MODE_PRIVATE)

    fun name(ctx: Context) = prefs(ctx).getString("name", "JX-11")!!
    fun action(ctx: Context, d: String) = prefs(ctx).getString(d, DEFAULTS[d])!!
    fun key(ctx: Context, d: String) = prefs(ctx).getInt("${d}_key", 22)
    fun mult(ctx: Context, d: String) = prefs(ctx).getInt("${d}_mult", 1)
    fun swipeMs(ctx: Context) = prefs(ctx).getInt("swipe_duration_ms", 200)
    fun mode(ctx: Context) = prefs(ctx).getInt("mode", Priv.MODE_AUTO)
    fun autostart(ctx: Context) = prefs(ctx).getBoolean("autostart", false)

    fun toFileText(ctx: Context): String = buildString {
        append("name=").append(name(ctx)).append('\n')
        for (d in DIRS) {
            val a = action(ctx, d)
            append(d).append('=').append(if (a == "custom") "key:${key(ctx, d)}" else a).append('\n')
        }
        for (d in DIRS) append(d).append("_mult=").append(mult(ctx, d)).append('\n')
        append("swipe_duration_ms=").append(swipeMs(ctx)).append('\n')
    }
}
