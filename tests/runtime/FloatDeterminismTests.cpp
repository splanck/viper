// File: tests/runtime/FloatDeterminismTests.cpp
// Purpose: Exercise VAL-style parsing for locale-independent behavior.
// Key invariants: Special values and decimal formats are deterministic regardless of locale.
// Ownership: Runtime numeric helpers.
// Links: docs/codemap.md

#include "viper/runtime/rt.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace
{
std::filesystem::path golden_dir()
{
    static const std::filesystem::path dir =
        std::filesystem::path(__FILE__).parent_path().parent_path() / "golden" / "float";
    return dir;
}

std::string read_text_file(const std::filesystem::path &path)
{
    std::ifstream stream(path);
    if (!stream)
    {
        std::cerr << "failed to open golden file: " << path << "\n";
        std::abort();
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::string build_print_report()
{
    struct PrintCase
    {
        const char *label;
        double value;
    };

    const std::array<PrintCase, 10> cases = {{{"0", 0.0},
                                              {"-0.0", -0.0},
                                              {"0.5", 0.5},
                                              {"1.5", 1.5},
                                              {"2.5", 2.5},
                                              {"1e20", 1e20},
                                              {"1e-20", 1e-20},
                                              {"NaN", std::numeric_limits<double>::quiet_NaN()},
                                              {"Inf", std::numeric_limits<double>::infinity()},
                                              {"-Inf", -std::numeric_limits<double>::infinity()}}};

    std::ostringstream out;
    for (const auto &test : cases)
    {
        char buffer[64] = {};
        RtError err = RT_ERROR_NONE;
        rt_str_from_double(test.value, buffer, sizeof(buffer), &err);
        assert(rt_ok(err));
        out << test.label << " -> " << buffer << '\n';
    }

    return out.str();
}

std::string build_parse_report()
{
    struct ParseCase
    {
        const char *input;
    };

    const std::array<ParseCase, 10> cases = {{{"0"},
                                              {"-0.0"},
                                              {"0.5"},
                                              {"1.5"},
                                              {"2.5"},
                                              {"1e20"},
                                              {"1e-20"},
                                              {"NaN"},
                                              {"Inf"},
                                              {"-Inf"}}};

    std::ostringstream out;
    for (const auto &test : cases)
    {
        bool ok = true;
        double value = rt_val_to_double(test.input, &ok);

        char buffer[64] = {};
        RtError err = RT_ERROR_NONE;
        rt_str_from_double(value, buffer, sizeof(buffer), &err);
        assert(rt_ok(err));

        out << "input=\"" << test.input << "\" -> " << (ok ? "ok" : "trap");
        out << " value=" << buffer;
        out << " signbit=" << (std::signbit(value) ? 1 : 0);
        out << " is_nan=" << (std::isnan(value) ? 1 : 0);

        int inf_class = 0;
        if (std::isinf(value))
            inf_class = (value > 0.0) ? 1 : -1;
        out << " is_inf=" << inf_class << '\n';
    }

    return out.str();
}
} // namespace

int main()
{
    const auto golden = golden_dir();

    const std::string expected_print = read_text_file(golden / "print.out");
    const std::string actual_print = build_print_report();
    if (actual_print != expected_print)
    {
        std::cerr << "print golden mismatch\nExpected:\n"
                  << expected_print << "\nActual:\n"
                  << actual_print;
        return 1;
    }

    const std::string expected_parse = read_text_file(golden / "parse.out");
    const std::string actual_parse = build_parse_report();
    if (actual_parse != expected_parse)
    {
        std::cerr << "parse golden mismatch\nExpected:\n"
                  << expected_parse << "\nActual:\n"
                  << actual_parse;
        return 1;
    }

    bool ok = true;
    double decimal_value = rt_val_to_double("1.2345", &ok);
    assert(ok);
    assert(decimal_value == 1.2345);

    ok = true;
    double comma_value = rt_val_to_double("1,234", &ok);
    assert(!ok);
    assert(comma_value == 0.0);

    ok = true;
    double spaced_nan = rt_val_to_double("   NaN", &ok);
    assert(!ok);
    assert(std::isnan(spaced_nan));

    return 0;
}
