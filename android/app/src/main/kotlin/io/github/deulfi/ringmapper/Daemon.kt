package io.github.deulfi.ringmapper

import android.content.Context
import android.util.Base64

/**
 * Controls the native ring_mapper daemon through a privileged shell.
 * Files live in /data/local/tmp/ringmapper, which both root and the shell user can use.
 */
object Daemon {
    const val DIR = "/data/local/tmp/ringmapper"
    private const val BIN = "$DIR/ring_mapper"
    private const val CFG = "$DIR/config.txt"
    private const val LOG = "$DIR/log.txt"

    /** Copy the daemon from the APK's extracted libs (atomic mv, safe while running). */
    fun install(ctx: Context, p: Priv): Res {
        val src = "${ctx.applicationInfo.nativeLibraryDir}/libringmapper.so"
        return p.run(
            "mkdir -p $DIR && cp '$src' $DIR/.ring_mapper.new && chmod 755 $DIR/.ring_mapper.new" +
                " && mv -f $DIR/.ring_mapper.new $BIN && chown -R 2000:2000 $DIR 2>/dev/null; [ -x $BIN ]"
        )
    }

    fun writeConfig(p: Priv, text: String): Res {
        val b64 = Base64.encodeToString(text.toByteArray(), Base64.NO_WRAP)
        return p.run("echo '$b64' | base64 -d > $CFG")
    }

    /** Lines "USER PID ARGS" for every running ring_mapper (ours or the Magisk/APatch module's). */
    fun status(p: Priv): String =
        p.run("ps -A -o USER,PID,CMDLINE | grep -E '/[r]ing_mapper( |$)' ; true").out.trim()

    fun isOursRunning(p: Priv) = p.run("pgrep -f '^$BIN' >/dev/null").ok

    /** Writes config, then reloads the running daemon (SIGHUP) or starts it detached. */
    fun apply(ctx: Context, p: Priv, cfgText: String): String {
        val sb = StringBuilder()
        install(ctx, p).let { if (!it.ok) return "Install failed: ${it.out}" }
        writeConfig(p, cfgText).let { if (!it.ok) return "Writing config failed: ${it.out}" }
        if (isOursRunning(p)) {
            p.run("pkill -HUP -f '^$BIN'")
            sb.append("Config reloaded.")
        } else {
            val r = p.run("$BIN -d -l $LOG $CFG")
            sb.append(
                when (r.code) {
                    0 -> "Started via ${p.label}."
                    4 -> "Another ring_mapper is already running (probably the Magisk/APatch module). Stop it first."
                    else -> "Start failed (${r.code}): ${r.out}"
                }
            )
        }
        return sb.toString()
    }

    fun stop(p: Priv): String {
        p.run("pkill -TERM -f '^$BIN'")
        return if (isOursRunning(p)) "Could not stop (owned by another user?)" else "Stopped."
    }

    fun listDevices(ctx: Context, p: Priv): List<String> {
        install(ctx, p)
        return p.run("$BIN --list").out.lines()
            .mapNotNull { it.substringAfter('\t', "").takeIf { n -> n.isNotBlank() } }
            .filterNot { it.startsWith("ring-") }
            .distinct().sorted()
    }

    fun log(p: Priv): String = p.run("tail -n 20 $LOG 2>/dev/null").out.trim()
}
