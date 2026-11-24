package io.github.deulfi.ringmapper

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/**
 * Starts the daemon after boot, then gets out of the way: no service stays running.
 * Root works immediately. Shizuku only works if it was started at boot (root mode or
 * Android's wireless-debugging autostart), so it waits for its binder for a while.
 */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED || !Config.autostart(ctx)) return
        val pending = goAsync()
        Thread {
            try {
                val mode = Config.mode(ctx)
                var p: Priv? = null
                for (i in 0 until 12) {               // up to ~36 s for Shizuku to come up
                    p = Priv.open(ctx, mode)
                    if (p != null) break
                    Thread.sleep(3000)
                }
                p?.use { Log.i("RingMapper", Daemon.apply(ctx, it, Config.toFileText(ctx))) }
                    ?: Log.w("RingMapper", "boot: no root/Shizuku available, not started")
            } catch (e: Exception) {
                Log.e("RingMapper", "boot start failed", e)
            } finally {
                pending.finish()
            }
        }.start()
    }
}
