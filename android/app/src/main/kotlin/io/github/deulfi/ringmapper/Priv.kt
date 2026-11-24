package io.github.deulfi.ringmapper

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import rikka.shizuku.Shizuku
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

data class Res(val code: Int, val out: String) {
    val ok get() = code == 0
}

/** A privileged shell: root via `su`, or shell/root via Shizuku. Never call on the main thread. */
interface Priv : AutoCloseable {
    val label: String
    fun run(cmd: String): Res
    override fun close() {}

    companion object {
        const val MODE_AUTO = 0
        const val MODE_ROOT = 1
        const val MODE_SHIZUKU = 2

        /** Opens the backend for [mode], or returns null with a reason in [why]. */
        fun open(ctx: Context, mode: Int, why: StringBuilder = StringBuilder()): Priv? {
            if (mode != MODE_ROOT) {
                when {
                    !ShizukuPriv.running() -> why.append("Shizuku is not running. ")
                    !ShizukuPriv.granted() -> why.append("Shizuku permission not granted. ")
                    else -> ShizukuPriv.connect(ctx)?.let { return it } ?: why.append("Shizuku service did not start. ")
                }
            }
            if (mode != MODE_SHIZUKU) {
                if (RootPriv.available()) return RootPriv() else why.append("Root (su) not available or denied. ")
            }
            return null
        }
    }
}

class RootPriv : Priv {
    override val label = "root (su)"

    override fun run(cmd: String): Res {
        val p = ProcessBuilder("su", "-c", cmd).redirectErrorStream(true).start()
        val out = p.inputStream.bufferedReader().readText()
        if (!p.waitFor(15, TimeUnit.SECONDS)) { p.destroy(); return Res(-1, out + "\n(timeout)") }
        return Res(p.exitValue(), out)
    }

    companion object {
        fun available(): Boolean = try {
            RootPriv().run("id -u").out.trim() == "0"
        } catch (_: Exception) {
            false
        }
    }
}

class ShizukuPriv private constructor(
    private val svc: IShellService,
    private val args: Shizuku.UserServiceArgs,
    private val conn: ServiceConnection,
) : Priv {
    override val label = if (runCatching { Shizuku.getUid() }.getOrNull() == 0) "Shizuku (root)" else "Shizuku (shell)"

    override fun run(cmd: String): Res {
        val r = svc.run(cmd)
        val nl = r.indexOf('\n')
        return Res(r.substring(0, nl).toIntOrNull() ?: -1, r.substring(nl + 1))
    }

    /** Unbind and kill the helper process: nothing Java-side stays in the background. */
    override fun close() {
        runCatching { Shizuku.unbindUserService(args, conn, true) }
    }

    companion object {
        fun running() = runCatching { Shizuku.pingBinder() }.getOrDefault(false)

        fun granted() = running() && runCatching {
            !Shizuku.isPreV11() && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        }.getOrDefault(false)

        fun connect(ctx: Context, timeoutMs: Long = 8000): ShizukuPriv? {
            val latch = CountDownLatch(1)
            var svc: IShellService? = null
            val args = Shizuku.UserServiceArgs(ComponentName(ctx.packageName, ShellService::class.java.name))
                .daemon(false)
                .processNameSuffix("shell")
                .debuggable(BuildConfig.DEBUG)
                .version(BuildConfig.VERSION_CODE)
            val conn = object : ServiceConnection {
                override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                    if (binder != null && binder.pingBinder()) svc = IShellService.Stub.asInterface(binder)
                    latch.countDown()
                }

                override fun onServiceDisconnected(name: ComponentName?) {}
            }
            Shizuku.bindUserService(args, conn)
            latch.await(timeoutMs, TimeUnit.MILLISECONDS)
            val s = svc ?: run { runCatching { Shizuku.unbindUserService(args, conn, true) }; return null }
            return ShizukuPriv(s, args, conn)
        }
    }
}
