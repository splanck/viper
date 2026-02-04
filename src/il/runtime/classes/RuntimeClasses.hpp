//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeClasses.hpp
/// @brief Runtime class metadata and unified signature registry for all frontends.
///
/// @details This file defines the data structures and interfaces for runtime
/// class metadata, enabling all Viper frontends to access type information
/// about runtime library classes like Viper.String, Viper.File, etc.
///
/// ## Architecture Overview
///
/// Runtime class information flows through the system as follows:
///
/// ```
/// runtime.def          Source of truth for all runtime definitions
///      │
///      ▼ (rtgen tool)
/// RuntimeClasses.inc   Generated C++ macro invocations
///      │
///      ▼ (macro expansion)
/// runtimeClassCatalog()  Immutable vector of RuntimeClass descriptors
///      │
///      ▼ (builds hash indexes)
/// RuntimeRegistry       O(1) method/property lookup with parsed signatures
///      │
///      ├─────────────────┬─────────────────┐
///      ▼                 ▼                 ▼
/// BASIC Frontend    Zia Frontend    Pascal Frontend
/// (toBasicType)     (toZiaType)     (future)
/// ```
///
/// ## Key Components
///
/// ### Raw Catalog (runtimeClassCatalog)
///
/// The catalog is a statically-initialized vector of RuntimeClass descriptors.
/// Each descriptor contains:
/// - Qualified name (e.g., "Viper.String")
/// - Type ID for runtime type identification
/// - Properties with getter/setter targets
/// - Methods with signature strings
///
/// ### RuntimeRegistry (Singleton)
///
/// The registry builds hash indexes over the catalog for O(1) lookup:
/// - Methods indexed by "class|method#arity"
/// - Properties indexed by "class.property"
/// - Functions indexed by canonical extern name
///
/// ### Frontend-Agnostic Types (ILScalarType)
///
/// Parsed signatures use ILScalarType to represent types in a
/// frontend-independent way. Each frontend provides an adapter
/// (toBasicType, toZiaType) to convert to their native type systems.
///
/// ## Signature String Format
///
/// Method signatures use the format: `returnType(param1,param2,...)`
///
/// Type tokens:
/// - `i64` - 64-bit signed integer
/// - `f64` - 64-bit floating point
/// - `i1` - Boolean
/// - `str` - String reference
/// - `obj` / `ptr` - Object pointer
/// - `void` - No return value
///
/// Examples:
/// - `"str(i64,i64)"` - Returns string, takes two integers (String.Substring)
/// - `"i64()"` - Returns integer, no parameters (String.Length getter)
/// - `"void(str)"` - Returns void, takes string (StringBuilder.Append)
///
/// ## Thread Safety
///
/// The catalog and registry are built using function-local statics with
/// guaranteed thread-safe initialization. Once built, all data is immutable.
///
/// ## Invariants
///
/// - The catalog is immutable after construction
/// - All string fields point to static string literals or are nullptr
/// - Signatures omit the receiver (self/this); it's implicit arg0
/// - The registry provides case-insensitive lookup
///
/// @see RuntimeClasses.cpp - Implementation of catalog and registry
/// @see RuntimeClasses.inc - Generated class descriptors
/// @see runtime.def - Source definitions for runtime library
/// @see docs/il-guide.md - IL specification reference
///

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::runtime
{

/// @brief Stable identifiers for runtime class types.
/// @note Only RTCLS_String is seeded for RC-1; future classes extend this enum.
enum class RuntimeTypeId : std::size_t
{
    RTCLS_String = 0,
    RTCLS_StringBuilder,
    RTCLS_Object,
    RTCLS_File,
    RTCLS_Path,
    RTCLS_Dir,
    RTCLS_List,
    RTCLS_Math,
    RTCLS_Convert,
    RTCLS_Random,
    RTCLS_Environment,
    RTCLS_Exec,
    RTCLS_Fmt,
    RTCLS_Canvas,
    RTCLS_Codec,
    RTCLS_Csv,
    RTCLS_Color,
    RTCLS_DateTime,
    RTCLS_Map,
    RTCLS_Seq,
    RTCLS_Stack,
    RTCLS_TreeMap,
    RTCLS_Queue,
    RTCLS_Heap,
    RTCLS_Ring,
    RTCLS_Bits,
    RTCLS_Bytes,
    RTCLS_Bag,
    RTCLS_Set,
    RTCLS_BinFile,
    RTCLS_MemStream,
    RTCLS_LineReader,
    RTCLS_LineWriter,
    RTCLS_Watcher,
    RTCLS_Compress,
    RTCLS_Archive,
    RTCLS_Log,
    RTCLS_Machine,
    RTCLS_Terminal,
    RTCLS_Clock,
    RTCLS_Countdown,
    RTCLS_Stopwatch,
    RTCLS_Guid,
    RTCLS_Hash,
    RTCLS_Json,
    RTCLS_KeyDerive,
    RTCLS_CryptoRand,
    RTCLS_Pattern,
    RTCLS_Template,
    RTCLS_Vec2,
    RTCLS_Vec3,
    RTCLS_Pixels,
    RTCLS_ThreadsMonitor,
    RTCLS_ThreadsSafeI64,
    RTCLS_ThreadsThread,
    RTCLS_ThreadsGate,
    RTCLS_ThreadsBarrier,
    RTCLS_ThreadsRwLock,
    RTCLS_Tcp,
    RTCLS_TcpServer,
    RTCLS_Udp,
    RTCLS_Dns,
    RTCLS_Http,
    RTCLS_HttpReq,
    RTCLS_HttpRes,
    RTCLS_Url,
    RTCLS_Tls,
    RTCLS_WebSocket,
    RTCLS_Keyboard,
    RTCLS_Mouse,
    RTCLS_Pad,
    RTCLS_Action,
    RTCLS_InputMgr,
    // Data structure classes
    RTCLS_Grid2D,
    RTCLS_Timer,
    // Game development abstractions
    RTCLS_StateMachine,
    RTCLS_Tween,
    RTCLS_ButtonGroup,
    RTCLS_SmoothValue,
    RTCLS_ParticleEmitter,
    RTCLS_SpriteAnimation,
    RTCLS_CollisionRect,
    RTCLS_Collision,
    RTCLS_ObjectPool,
    RTCLS_ScreenFX,
    RTCLS_PathFollower,
    RTCLS_Quadtree,
    // Audio classes
    RTCLS_Audio,
    RTCLS_Sound,
    RTCLS_Voice,
    RTCLS_Music,
    // Graphics classes (extended)
    RTCLS_Sprite,
    RTCLS_Tilemap,
    RTCLS_Camera,
    // GUI classes
    RTCLS_GuiApp,
    RTCLS_GuiFont,
    RTCLS_GuiWidget,
    RTCLS_GuiLabel,
    RTCLS_GuiButton,
    RTCLS_GuiTextInput,
    RTCLS_GuiCheckbox,
    RTCLS_GuiScrollView,
    RTCLS_GuiTreeView,
    RTCLS_GuiTreeNode,
    RTCLS_GuiTabBar,
    RTCLS_GuiTab,
    RTCLS_GuiSplitPane,
    RTCLS_GuiCodeEditor,
    RTCLS_GuiDropdown,
    RTCLS_GuiSlider,
    RTCLS_GuiProgressBar,
    RTCLS_GuiListBox,
    RTCLS_GuiRadioGroup,
    RTCLS_GuiRadioButton,
    RTCLS_GuiSpinner,
    RTCLS_GuiImage,
    RTCLS_GuiTheme,
    RTCLS_GuiVBox,
    RTCLS_GuiHBox,
    RTCLS_GuiMenuBar,
    RTCLS_GuiMenu,
    RTCLS_GuiMenuItem,
    RTCLS_GuiToolbar,
    RTCLS_GuiToolbarItem,
    RTCLS_GuiStatusBar,
    RTCLS_GuiStatusBarItem,
};

/// @brief Describes a property on a runtime class.
/// @details Properties surface as getters/setters. Setters may be null when
///          the property is read-only.
struct RuntimeProperty
{
    const char *name;   ///< Property name (e.g., "Length").
    const char *type;   ///< IL scalar type (e.g., "i64", "i1").
    const char *getter; ///< Canonical extern target (e.g., "Viper.String.get_Length").
    const char *setter; ///< Canonical extern target or nullptr if read-only.
    bool readonly;      ///< True if setter is nullptr.
};

/// @brief Describes a method on a runtime class.
struct RuntimeMethod
{
    const char *name;      ///< Method name (e.g., "Substring").
    const char *signature; ///< Signature string in compact IL grammar.
    const char *target;    ///< Canonical extern target (e.g., "Viper.String.Substring").
};

/// @brief Describes a runtime class and its members.
struct RuntimeClass
{
    const char *qname;    ///< Fully-qualified name (e.g., "Viper.String").
    const char *layout;   ///< Layout descriptor (opaque until object model defined).
    const char *ctor;     ///< Optional ctor helper extern; may be nullptr.
    RuntimeTypeId typeId; ///< Stable type identifier.
    std::vector<RuntimeProperty> properties; ///< Declared properties.
    std::vector<RuntimeMethod> methods;      ///< Declared methods.
};

//===----------------------------------------------------------------------===//
/// @name Frontend-Agnostic Type System
/// @brief Parsed signature types shared across all frontends.
/// @{
//===----------------------------------------------------------------------===//

/// @brief Frontend-agnostic scalar types for runtime signatures.
/// @details Frontends map these to their native type systems.
enum class ILScalarType : std::uint8_t
{
    Void,   ///< void return type
    I64,    ///< 64-bit signed integer
    F64,    ///< 64-bit floating point
    Bool,   ///< Boolean (i1)
    String, ///< String reference (str)
    Object, ///< Object pointer (obj/ptr)
    Unknown ///< Unrecognized or parse error
};

/// @brief Parsed signature with structured type information.
/// @details Extracted from signature strings like "str(i64,i64)".
struct ParsedSignature
{
    ILScalarType returnType{ILScalarType::Unknown};
    bool isOptionalReturn{false};
    std::vector<ILScalarType> params;

    /// @brief Check if the signature was parsed successfully.
    [[nodiscard]] bool isValid() const
    {
        return returnType != ILScalarType::Unknown;
    }

    /// @brief Get the number of parameters (excluding receiver).
    [[nodiscard]] std::size_t arity() const
    {
        return params.size();
    }
};

/// @brief Extended method descriptor with parsed signature.
struct ParsedMethod
{
    const char *name;   ///< Method name (e.g., "Substring").
    const char *target; ///< Canonical extern target.
    ParsedSignature signature;
};

/// @brief Extended property descriptor with parsed type.
struct ParsedProperty
{
    const char *name;   ///< Property name (e.g., "Length").
    ILScalarType type;  ///< Resolved property type.
    const char *getter; ///< Getter extern target.
    const char *setter; ///< Setter extern target or nullptr.
    bool readonly;      ///< True if setter is nullptr.
};

/// @brief Parse a signature string like "str(i64,i64)" into structured form.
/// @param sig The signature string from RuntimeMethod.
/// @return Parsed signature with return type and parameter types.
ParsedSignature parseRuntimeSignature(std::string_view sig);

/// @brief Map IL token (i64, f64, str, etc.) to ILScalarType.
/// @param tok Token from signature parsing.
/// @return Corresponding ILScalarType, or Unknown if unrecognized.
ILScalarType mapILToken(std::string_view tok);

/// @brief Unified runtime registry with parsed signatures and lookup.
/// @details Provides O(1) lookup for methods and properties by building
/// hash indexes over the runtime class catalog. Frontends use this
/// registry and map ILScalarType to their native type systems.
///
/// ## Usage
/// ```cpp
/// const auto& reg = RuntimeRegistry::instance();
/// auto method = reg.findMethod("Viper.String", "Substring", 2);
/// if (method) {
///     // method->signature.returnType, method->signature.params
/// }
/// ```
class RuntimeRegistry
{
  public:
    /// @brief Get the singleton instance.
    static const RuntimeRegistry &instance();

    /// @brief Find a method by class, name, and arity.
    /// @param classQName Fully-qualified class name (e.g., "Viper.String").
    /// @param methodName Method name (e.g., "Substring").
    /// @param arity Number of parameters (excluding receiver).
    /// @return Parsed method info if found, nullopt otherwise.
    /// @note Comparison is case-insensitive.
    [[nodiscard]] std::optional<ParsedMethod> findMethod(std::string_view classQName,
                                                         std::string_view methodName,
                                                         std::size_t arity) const;

    /// @brief Find a property by class and name.
    /// @param classQName Fully-qualified class name.
    /// @param propertyName Property name.
    /// @return Parsed property info if found, nullopt otherwise.
    /// @note Comparison is case-insensitive.
    [[nodiscard]] std::optional<ParsedProperty> findProperty(std::string_view classQName,
                                                             std::string_view propertyName) const;

    /// @brief Find a function signature by canonical extern name.
    /// @param canonicalName Full extern name (e.g., "Viper.String.Substring").
    /// @return Parsed signature if found, nullopt otherwise.
    /// @note Comparison is case-insensitive.
    [[nodiscard]] std::optional<ParsedSignature> findFunction(std::string_view canonicalName) const;

    /// @brief List available method arities for diagnostics.
    /// @param classQName Fully-qualified class name.
    /// @param methodName Method name.
    /// @return List of strings like "Substring/2" for each arity found.
    [[nodiscard]] std::vector<std::string> methodCandidates(std::string_view classQName,
                                                            std::string_view methodName) const;

    /// @brief Get the underlying raw catalog.
    /// @return Reference to the runtime class catalog.
    [[nodiscard]] const std::vector<RuntimeClass> &rawCatalog() const;

  private:
    RuntimeRegistry();
    void buildIndexes();

    static std::string methodKey(std::string_view cls, std::string_view method, std::size_t arity);
    static std::string propertyKey(std::string_view cls, std::string_view prop);
    static std::string functionKey(std::string_view name);
    static std::string toLower(std::string_view s);

    std::unordered_map<std::string, ParsedMethod> methodIndex_;
    std::unordered_map<std::string, ParsedProperty> propertyIndex_;
    std::unordered_map<std::string, ParsedSignature> functionIndex_;
};

/// @}

/// @brief Expose the immutable runtime class catalog.
/// @returns Const reference to a statically initialized vector of descriptors.
/// @signature-grammar
///   ret(args) where:
///     - ret, args are IL scalars: i64, f64, i1, str, obj, void
///     - Methods implicitly take the receiver as arg0 (not spelled in signature)
///     - Example: "str(i64,i64)" for String.Substring(start,len) -> string
const std::vector<RuntimeClass> &runtimeClassCatalog();

/// @brief Find a runtime class by its fully-qualified name.
/// @param qname Fully-qualified class name (e.g., "Viper.String").
/// @returns Pointer to the matching RuntimeClass, or nullptr if not found.
/// @note Comparison is case-insensitive.
const RuntimeClass *findRuntimeClassByQName(std::string_view qname);

} // namespace il::runtime
