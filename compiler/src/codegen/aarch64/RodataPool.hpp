//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/aarch64/RodataPool.hpp
// Purpose: Deduplicate string literals and emit a rodata section for AArch64.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il::core
{
struct Module;
}

namespace viper::codegen::aarch64
{

class RodataPool
{
  public:
    // Map IL global string name to pooled label
    const std::unordered_map<std::string, std::string> &nameToLabel() const noexcept
    {
        return nameToLabel_;
    }

    // Build the pool from module globals (string constants only)
    void buildFromModule(const il::core::Module &mod);

    // Emit a platform-correct rodata section with pooled labels and asciz data
    void emit(std::ostream &os) const;

  private:
    // content -> pooled label
    std::unordered_map<std::string, std::string> contentToLabel_;
    // IL name -> pooled label
    std::unordered_map<std::string, std::string> nameToLabel_;
    // emission order: (label, content)
    std::vector<std::pair<std::string, std::string>> ordered_;

    static std::string makeLabel(std::size_t index);
    static std::string escapeAsciz(std::string_view bytes);
    void addString(const std::string &ilName, const std::string &bytes);
};

} // namespace viper::codegen::aarch64
