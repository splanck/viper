#pragma once

#include <string>

inline std::string diag_rule_msg(const char *rule,
                                 const std::string &msg,
                                 const std::string &fn,
                                 int block,
                                 int insn)
{
    return "[RULE:" + std::string(rule) + "] " + msg + " at " + fn + ":" +
           std::to_string(block) + ":" + std::to_string(insn);
}
