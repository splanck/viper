//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/WatchTests.cpp
// Purpose: Verify watched scalars print only on value changes.
// Key invariants: Output lines appear only when the value differs.
// Ownership/Lifetime: Test creates and removes a temporary file.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: WatchTests <ilc> <il file>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string outFile = "watch.out";
    std::string cmd = ilc + " -run " + ilFile + " --watch x 2>" + outFile;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream out(outFile);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(out, line))
        lines.push_back(line);
    if (lines.size() != 6)
    {
        std::cerr << "unexpected line count\n";
        return 1;
    }
    const std::vector<std::string> expected = {
        "[WATCH] x=i64:1  (fn=@main blk=entry ip=#1)",
        "[WATCH] x=f64:1  (fn=@main blk=entry ip=#3)",
        "[WATCH] x=f64:2  (fn=@main blk=entry ip=#4)",
        "[WATCH] x=i64:2  (fn=@main blk=entry ip=#5)",
        "[WATCH] x=f64:2  (fn=@main blk=entry ip=#7)",
        "[WATCH] x=f64:3  (fn=@main blk=entry ip=#8)",
    };
    if (lines != expected)
    {
        std::cerr << "unexpected watch output\n";
        return 1;
    }
    std::remove(outFile.c_str());
    return 0;
}
