// File: tests/e2e/support/FloatOut.cpp
// Purpose: Implement helper that checks floating-point outputs within tolerance.
// Key invariants: Parses EXPECT≈ lines; each expectation matches one output value.
// Ownership/Lifetime: Purely functional; no persistent state.
// Links: docs/testing.md
#include "FloatOut.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

int check_float_output(const std::string &out_file, const std::string &expect_file)
{
    std::ifstream out(out_file);
    if (!out.is_open())
    {
        std::cerr << "failed to open output file\n";
        return 1;
    }
    std::vector<double> actuals;
    std::string line;
    while (std::getline(out, line))
    {
        if (line.empty())
        {
            continue;
        }
        std::istringstream ls(line);
        double v;
        if (ls >> v)
        {
            actuals.push_back(v);
        }
    }
    std::ifstream ex(expect_file);
    if (!ex.is_open())
    {
        std::cerr << "failed to open expect file\n";
        return 1;
    }
    size_t idx = 0;
    while (std::getline(ex, line))
    {
        std::istringstream es(line);
        std::string tag;
        es >> tag;
        if (tag != "EXPECT≈")
        {
            continue;
        }
        double val;
        double tol;
        es >> val >> tol;
        if (idx >= actuals.size())
        {
            std::cerr << "missing output for expectation\n";
            return 1;
        }
        double diff = std::abs(actuals[idx] - val);
        if (diff > tol)
        {
            std::cerr << "mismatch at " << idx << ": " << actuals[idx] << " vs " << val << " tol "
                      << tol << "\n";
            return 1;
        }
        ++idx;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        return 1;
    }
    return check_float_output(argv[1], argv[2]);
}
