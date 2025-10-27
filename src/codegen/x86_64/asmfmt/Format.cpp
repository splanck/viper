// src/codegen/x86_64/asmfmt/Format.cpp
// SPDX-License-Identifier: MIT
//
// Purpose: Implement assembly formatting helpers shared by the x86-64 backend.
// Notes: Keep logic side-effect free so emitters can reuse these utilities
//        without introducing dependencies on global state.

#include "Format.hpp"

#include "../TargetX64.hpp"

#include <cstddef>
#include <sstream>
#include <string>

namespace asmfmt
{
namespace
{
[[nodiscard]] bool is_printable(unsigned char ch) noexcept
{
    return ch >= 0x20U && ch <= 0x7EU;
}
} // namespace

std::string escape_ascii(std::string_view bytes)
{
    std::string out{};
    out.reserve(bytes.size());
    for (const unsigned char ch : bytes)
    {
        if (ch == static_cast<unsigned char>('\\') || ch == static_cast<unsigned char>('"'))
        {
            out.push_back('\\');
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::string format_imm(std::int64_t v)
{
    return std::string{"$"} + std::to_string(v);
}

std::string format_label(std::string_view name)
{
    return std::string{name};
}

std::string format_rip_label(std::string_view name)
{
    std::string result{name};
    result += "(%rip)";
    return result;
}

std::string fmt_reg(int reg)
{
    if (reg >= 0)
    {
        const auto phys = viper::codegen::x64::regName(static_cast<viper::codegen::x64::PhysReg>(reg));
        return std::string{phys != nullptr ? phys : "%r?"};
    }

    const unsigned virt = static_cast<unsigned>(-reg - 1);
    std::ostringstream os{};
    os << "%v" << virt;
    return os.str();
}

std::string format_mem(const MemAddr &a)
{
    std::ostringstream os{};
    if (a.disp != 0)
    {
        os << a.disp;
    }

    os << '(' << fmt_reg(a.base);
    if (a.has_index)
    {
        os << ',' << fmt_reg(a.index) << ',' << static_cast<unsigned>(a.scale);
    }
    os << ')';
    return os.str();
}

std::string format_rodata_bytes(std::string_view bytes)
{
    std::ostringstream os{};
    if (bytes.empty())
    {
        os << "  # empty literal\n";
        return os.str();
    }

    std::size_t index = 0;
    while (index < bytes.size())
    {
        const unsigned char current = static_cast<unsigned char>(bytes[index]);
        if (is_printable(current))
        {
            const std::size_t begin = index;
            while (index < bytes.size() && is_printable(static_cast<unsigned char>(bytes[index])))
            {
                ++index;
            }
            const std::string_view run{bytes.data() + begin, index - begin};
            os << "  .ascii \"" << escape_ascii(run) << "\"\n";
            continue;
        }

        os << "  .byte ";
        std::size_t emitted = 0;
        while (index < bytes.size() && emitted < 16U)
        {
            const unsigned char byte_val = static_cast<unsigned char>(bytes[index]);
            if (is_printable(byte_val))
            {
                break;
            }
            if (emitted != 0)
            {
                os << ", ";
            }
            os << static_cast<unsigned>(byte_val);
            ++index;
            ++emitted;
        }
        os << '\n';
    }

    return os.str();
}

} // namespace asmfmt

