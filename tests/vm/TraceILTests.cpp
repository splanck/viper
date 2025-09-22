// File: tests/vm/TraceILTests.cpp
// Purpose: Verify IL tracing emits deterministic lines and disables by default.
// Key invariants: Trace output matches golden file exactly.
// Ownership/Lifetime: Test owns temporary files and cleans them up.
// Links: docs/testing.md
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::cerr << "usage: TraceILTests <ilc> <il file> <il golden> <src golden>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string ilGolden = argv[3];
    std::string srcGolden = argv[4];
    auto checkTrace = [](const std::string &outPath,
                         const std::string &goldPath,
                         const char *label) -> bool {
        std::ifstream out(outPath);
        std::ifstream gold(goldPath);
        std::string o, g;
        while (std::getline(out, o))
        {
            if (!std::getline(gold, g) || o != g)
            {
                std::cerr << label << " trace mismatch\n";
                return false;
            }
        }
        if (std::getline(gold, g))
        {
            std::cerr << label << " golden has extra lines\n";
            return false;
        }
        return true;
    };
    std::string ilOutFile = "trace_il.out";
    std::string cmd = ilc + " -run " + ilFile + " --trace=il 2>" + ilOutFile;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    if (!checkTrace(ilOutFile, ilGolden, "IL"))
        return 1;
    std::string srcOutFile = "trace_src.out";
    cmd = ilc + " -run " + ilFile + " --trace=src 2>" + srcOutFile;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    if (!checkTrace(srcOutFile, srcGolden, "SRC"))
        return 1;
    std::string noneFile = "none.out";
    cmd = ilc + " -run " + ilFile + " 2>" + noneFile;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream none(noneFile);
    if (none.peek() != EOF)
    {
        std::cerr << "trace emitted without flag\n";
        return 1;
    }
    std::remove(ilOutFile.c_str());
    std::remove(srcOutFile.c_str());
    std::remove(noneFile.c_str());
    return 0;
}
