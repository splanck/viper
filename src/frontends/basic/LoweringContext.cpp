// File: src/frontends/basic/LoweringContext.cpp
// Purpose: Implements state used during BASIC-to-IL lowering.
// Key invariants: None.
// Ownership/Lifetime: Context references module owned externally.
// Links: docs/class-catalog.md

#include "frontends/basic/LoweringContext.hpp"
#include "il/build/IRBuilder.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include <cassert>

namespace il::frontends::basic
{

void LoweringContext::beginProgram(build::IRBuilder &b)
{
    builder = &b;
    strings.clear();
    nextStringId = 0;
    beginProcedure();
}

void LoweringContext::beginProcedure()
{
    function = nullptr;
    varNames.clear();
    arrayNames.clear();
    varTypes.clear();
    varSlots.clear();
    arrayLenSlots.clear();
    lineBlocks.clear();
}

void LoweringContext::bindFunction(core::Function &fn)
{
    function = &fn;
}

void LoweringContext::registerVariable(const std::string &name)
{
    varNames.insert(name);
}

void LoweringContext::markArray(const std::string &name)
{
    arrayNames.insert(name);
    registerVariable(name);
}

void LoweringContext::recordVarType(const std::string &name, Type type)
{
    varTypes[name] = type;
}

std::optional<Type> LoweringContext::lookupVarType(const std::string &name) const
{
    auto it = varTypes.find(name);
    if (it == varTypes.end())
        return std::nullopt;
    return it->second;
}

void LoweringContext::recordVarSlot(const std::string &name, unsigned slot)
{
    varSlots[name] = slot;
}

std::optional<unsigned> LoweringContext::lookupVarSlot(const std::string &name) const
{
    auto it = varSlots.find(name);
    if (it == varSlots.end())
        return std::nullopt;
    return it->second;
}

void LoweringContext::recordArrayLengthSlot(const std::string &name, unsigned slot)
{
    arrayLenSlots[name] = slot;
}

std::optional<unsigned> LoweringContext::lookupArrayLengthSlot(const std::string &name) const
{
    auto it = arrayLenSlots.find(name);
    if (it == arrayLenSlots.end())
        return std::nullopt;
    return it->second;
}

void LoweringContext::registerLineBlock(int line, std::size_t blockIndex)
{
    lineBlocks[line] = blockIndex;
}

core::BasicBlock *LoweringContext::lookupLineBlock(int line) const
{
    auto it = lineBlocks.find(line);
    if (it == lineBlocks.end())
        return nullptr;
    assert(function && "function must be bound before looking up blocks");
    if (!function)
        return nullptr;
    auto idx = it->second;
    if (idx >= function->blocks.size())
        return nullptr;
    return &function->blocks[idx];
}

std::string LoweringContext::internString(const std::string &value)
{
    auto it = strings.find(value);
    if (it != strings.end())
        return it->second;
    std::string name = ".L" + std::to_string(nextStringId++);
    assert(builder && "builder must be set before interning strings");
    builder->addGlobalStr(name, value);
    strings[value] = name;
    return name;
}

const std::unordered_set<std::string> &LoweringContext::variables() const
{
    return varNames;
}

const std::unordered_set<std::string> &LoweringContext::arrays() const
{
    return arrayNames;
}

} // namespace il::frontends::basic
