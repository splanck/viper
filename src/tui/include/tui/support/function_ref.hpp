//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the FunctionRef class template, a lightweight
// non-owning callable reference for Viper's TUI framework. FunctionRef
// provides a type-erased view of a callable (lambda, function pointer,
// or functor) without incurring the heap allocation overhead of
// std::function.
//
// FunctionRef is designed for use in callback parameters where the
// callable's lifetime is guaranteed to exceed the FunctionRef's usage
// (e.g., within a single function call). It is similar to the C++26
// std::function_ref proposal.
//
// Key invariants:
//   - The referenced callable must remain alive for the duration of
//     the FunctionRef's use (dangling references are undefined behavior).
//   - FunctionRef is trivially copyable (pointer-sized).
//   - Supports both function pointers and arbitrary callables.
//
// Ownership: FunctionRef does not own the callable. It stores a void*
// pointer to the callable object and a function pointer for type-erased
// invocation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <type_traits>
#include <utility>

namespace viper::tui
{
/// @brief Non-owning, lightweight reference to a callable with a given signature.
/// @details Provides type-erased callable access without heap allocation, similar to
///          the C++26 std::function_ref proposal. The referenced callable must outlive
///          the FunctionRef. Ideal for callback parameters in functions where the
///          caller controls the callable's lifetime.
/// @tparam Signature The function signature (e.g., bool(int, float)).
template <typename Signature> class FunctionRef;

/// @brief Specialization implementing the callable reference for a specific signature.
/// @details Stores a type-erased void pointer to the callable and a static dispatch
///          function that casts back to the original type for invocation. Supports
///          construction from both function pointers and arbitrary callable objects.
/// @tparam Ret Return type of the callable.
/// @tparam Args Parameter types of the callable.
template <typename Ret, typename... Args> class FunctionRef<Ret(Args...)>
{
  public:
    /// @brief Construct from a plain function pointer matching the signature.
    /// @details Stores the function pointer cast to void* and uses a dedicated
    ///          dispatch function for invocation. The function pointer must remain
    ///          valid for the lifetime of this reference.
    /// @param fn Function pointer to reference.
    FunctionRef(Ret (*fn)(Args...)) noexcept
        : obj_(reinterpret_cast<void *>(fn)), callback_(&invokeFunctionPtr)
    {
    }

    /// @brief Construct from any callable object (lambda, functor) matching the signature.
    /// @details Takes the address of the callable and stores it with a type-erased
    ///          dispatch function. The callable must outlive this FunctionRef.
    /// @tparam Callable The type of the callable object.
    /// @param callable The callable to reference. Must outlive this object.
    template <
        typename Callable,
        typename = std::enable_if_t<!std::is_same_v<std::remove_cvref_t<Callable>, FunctionRef>>>
    FunctionRef(Callable &&callable) noexcept
        : obj_(const_cast<void *>(static_cast<const void *>(std::addressof(callable)))),
          callback_(&invoke<Callable>)
    {
    }

    /// @brief Invoke the referenced callable with the given arguments.
    /// @details Dispatches through the stored function pointer, casting the void*
    ///          back to the original callable type for invocation.
    /// @param args Arguments forwarded to the callable.
    /// @return The callable's return value.
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
