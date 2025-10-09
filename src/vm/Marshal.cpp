//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements helpers that translate between the VM's lightweight string view
// abstraction and the runtime's reference-counted string handles.  Centralising
// the conversions keeps the bridge consistent and documents the ownership
// expectations for the temporary wrappers returned to opcode handlers.
//
//===----------------------------------------------------------------------===//

#include "vm/Marshal.hpp"

#include "rt_string.h"
#include "vm/RuntimeBridge.hpp"

#include <cassert>

namespace il::vm
{

/// @brief Convert an immutable VM string view into a runtime handle.
///
/// The helper preserves the `nullptr` sentinel used throughout the VM to mean
/// "no string" and reuses the runtime's constant-string fast path when the
/// input has no embedded NULs.  Otherwise a fresh runtime allocation mirrors
/// the byte sequence so handlers can safely share the returned handle.
///
/// @param text Non-owning reference to the source character range.
/// @return Runtime handle suitable for passing to C helpers; may be null when
///         @p text lacks backing storage.
ViperString toViperString(StringRef text)
{
    if (text.empty())
    {
        if (text.data() == nullptr)
            return rt_const_cstr("");
        return rt_string_from_bytes(text.data(), 0);
    }
    if (text.data() == nullptr)
        return nullptr;
    if (text.find('\0') != StringRef::npos)
        return rt_string_from_bytes(text.data(), text.size());
    return rt_const_cstr(text.data());
}

/// @brief Convert a runtime string handle back into the VM's view type.
///
/// Valid runtime handles expose a contiguous UTF-8 byte sequence and length via
/// the C ABI helpers.  The returned @ref StringRef borrows that storage without
/// taking ownership, so callers must ensure the runtime string outlives the
/// view.  Null or invalid handles produce an empty view.
///
/// @param str Runtime string handle to translate.
/// @return Non-owning view of the runtime string's contents, or an empty view
///         when the handle is null.
StringRef fromViperString(const ViperString &str)
{
    if (!str)
        return {};
    const char *data = rt_string_cstr(str);
    if (!data)
        return {};
    const int64_t length = rt_len(str);
    if (length < 0)
    {
        RuntimeBridge::trap(TrapKind::DomainError,
                            "rt_string reported negative length",
                            {},
                            "",
                            "");
        return {};
    }
    return StringRef{data, static_cast<size_t>(length)};
}

int64_t toI64(const il::core::Value &value)
{
    using Kind = il::core::Value::Kind;
    switch (value.kind)
    {
        case Kind::ConstInt:
            return static_cast<int64_t>(value.i64);
        case Kind::ConstFloat:
            return static_cast<int64_t>(value.f64);
        case Kind::NullPtr:
            return 0;
        default:
            assert(false && "value kind is not convertible to i64");
            return 0;
    }
}

double toF64(const il::core::Value &value)
{
    using Kind = il::core::Value::Kind;
    switch (value.kind)
    {
        case Kind::ConstFloat:
            return value.f64;
        case Kind::ConstInt:
            return static_cast<double>(value.i64);
        case Kind::NullPtr:
            return 0.0;
        default:
            assert(false && "value kind is not convertible to f64");
            return 0.0;
    }
}

} // namespace il::vm
