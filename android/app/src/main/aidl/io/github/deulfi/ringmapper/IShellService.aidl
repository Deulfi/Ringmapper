package io.github.deulfi.ringmapper;

// Runs inside Shizuku's process as the shell (or root) user.
interface IShellService {
    // Shizuku calls this transaction code to stop the service process.
    void destroy() = 16777114;

    // Runs `sh -c cmd`. Returns "<exit code>\n<combined stdout+stderr>".
    String run(String cmd) = 1;
}
