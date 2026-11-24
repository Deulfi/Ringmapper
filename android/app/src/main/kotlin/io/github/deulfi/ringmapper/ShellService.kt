package io.github.deulfi.ringmapper

import kotlin.system.exitProcess

/** Shizuku UserService: lives in a short-lived process owned by Shizuku (shell or root uid). */
class ShellService : IShellService.Stub() {
    override fun destroy() {
        exitProcess(0)
    }

    override fun run(cmd: String): String {
        val p = ProcessBuilder("sh", "-c", cmd).redirectErrorStream(true).start()
        val out = p.inputStream.bufferedReader().readText()
        val rc = p.waitFor()
        return "$rc\n$out"
    }
}
