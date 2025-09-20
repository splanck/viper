// File: src/frontends/basic/ScopeTracker.cpp
// Purpose: Implements lexical scope tracking with name mangling for the BASIC front end.
// Key invariants: Scopes form a stack; resolving searches innermost to outermost.
// Ownership/Lifetime: Owned by SemanticAnalyzer; no AST ownership.
// Links: docs/class-catalog.md

#include "frontends/basic/ScopeTracker.hpp"

namespace il::frontends::basic
{

ScopeTracker::ScopedScope::ScopedScope(ScopeTracker &st) : st_(st)
{
    st_.pushScope();
}

ScopeTracker::ScopedScope::~ScopedScope()
{
    st_.popScope();
}

void ScopeTracker::reset()
{
    stack_.clear();
    nextId_ = 0;
}

void ScopeTracker::pushScope()
{
    stack_.emplace_back();
}

void ScopeTracker::popScope()
{
    if (!stack_.empty())
        stack_.pop_back();
}

void ScopeTracker::bind(const std::string &name, const std::string &mapped)
{
    if (!stack_.empty())
        stack_.back()[name] = mapped;
}

bool ScopeTracker::isDeclaredInCurrentScope(const std::string &name) const
{
    return !stack_.empty() && stack_.back().count(name);
}

std::string ScopeTracker::declareLocal(const std::string &name)
{
    std::string unique = name + "_" + std::to_string(nextId_++);
    if (!stack_.empty())
        stack_.back()[name] = unique;
    return unique;
}

std::optional<std::string> ScopeTracker::resolve(const std::string &name) const
{
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it)
    {
        auto found = it->find(name);
        if (found != it->end())
            return found->second;
    }
    return std::nullopt;
}

bool ScopeTracker::hasScope() const
{
    return !stack_.empty();
}

} // namespace il::frontends::basic
