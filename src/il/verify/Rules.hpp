#pragma once

#include <string>
#include <vector>

namespace il::core
{
struct Instr;
struct Function;
}

struct Rule
{
    const char *name;
    bool (*check)(const il::core::Function &, const il::core::Instr &, std::string &out_msg);
};

const std::vector<Rule> &viper_verifier_rules();
