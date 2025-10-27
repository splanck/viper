// File: src/common/RunProcess.cpp
// Purpose: Provide a portable subprocess launcher for CLI utilities.
// Key invariants: Commands are executed via the platform `popen` facility, aggregating stdout
//                 and stderr text while returning normalised status codes when available.
// Ownership/Lifetime: Callers own argument buffers; the helper duplicates them into a command
//                     string suitable for the host shell.
// Links: docs/codemap.md

#include "common/RunProcess.hpp"

#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#    define POPEN _popen
#    define PCLOSE _pclose
#else
#    define POPEN popen
#    define PCLOSE pclose
#endif

RunResult run_process(const std::vector<std::string> &argv,
                      std::optional<std::string> cwd,
                      const std::vector<std::pair<std::string, std::string>> &env)
{
    (void)env; // Environment forwarding is not yet implemented.

    std::string cmd;
    for (std::size_t i = 0; i < argv.size(); ++i)
    {
        if (i != 0)
        {
            cmd += ' ';
        }
        cmd += '"';
        cmd += argv[i];
        cmd += '"';
    }
#ifndef _WIN32
    cmd += " 2>&1";
#endif

    if (cwd)
    {
#ifdef _WIN32
        // Windows popen lacks a straightforward chdir hook; callers should adjust when needed.
        (void)cwd;
#else
        (void)cwd;
#endif
    }

    RunResult rr{0, "", ""};
    FILE *pipe = POPEN(cmd.c_str(), "r");
    if (!pipe)
    {
        rr.exit_code = -1;
        rr.err = "failed to popen";
        return rr;
    }

    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe))
    {
        rr.out += buffer;
    }

    rr.exit_code = PCLOSE(pipe);
#ifndef _WIN32
    // When stderr is redirected to stdout the captured text lives in `out`.
    rr.err = rr.out;
#endif
    return rr;
}

#undef POPEN
#undef PCLOSE
