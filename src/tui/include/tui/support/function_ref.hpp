// tui/include/tui/support/function_ref.hpp
// @brief Lightweight non-owning callable reference for performance-critical callbacks.
// @invariant Referenced callable must outlive the FunctionRef instance.
// @ownership FunctionRef stores raw pointer to caller-owned callable without lifetime extension.
#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace viper::tui
{
/// @brief Non-owning reference to a callable with the specified signature.
///
/// FunctionRef allows passing inline lambdas or function objects without incurring
/// heap allocations. The referenced callable must remain alive for the duration of
/// the FunctionRef use.
template <typename Signature> class FunctionRef;

/// @brief Specialization handling invocation of callables matching the signature.
template <typename Ret, typename... Args> class FunctionRef<Ret(Args...)>
{
  public:
    /// @brief Construct from a function pointer.
    FunctionRef(Ret (*fn)(Args...)) noexcept
        : obj_(reinterpret_cast<void *>(fn)), callback_(&invokeFunctionPtr)
    {
    }

    /// @brief Construct from any callable matching the signature.
    template <
        typename Callable,
        typename = std::enable_if_t<!std::is_same_v<std::remove_cvref_t<Callable>, FunctionRef>>>
    FunctionRef(Callable &&callable) noexcept
        : obj_(const_cast<void *>(static_cast<const void *>(std::addressof(callable)))),
          callback_(&invoke<Callable>)
    {
    }

    /// @brief Invoke the referenced callable.
    Ret operator()(Args... args) const
    {
        return callback_(obj_, std::forward<Args>(args)...);
    }

  private:
    template <typename Callable> static Ret invoke(void *obj, Args... args)
    {
        return std::invoke(*static_cast<std::remove_reference_t<Callable> *>(obj),
                           std::forward<Args>(args)...);
    }

    static Ret invokeFunctionPtr(void *obj, Args... args)
    {
        auto *fn = reinterpret_cast<Ret (*)(Args...)>(obj);
        return std::invoke(fn, std::forward<Args>(args)...);
    }

    void *obj_{};
    Ret (*callback_)(void *, Args...){};
};
} // namespace viper::tui
