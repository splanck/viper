//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file RuntimeNames.hpp
/// @brief Centralized runtime function and type names for ViperLang code generation.
///
/// This header provides a single source of truth for all runtime function names
/// used during IL code generation. By centralizing these names, we ensure:
///
/// 1. **Consistency**: All parts of the compiler reference the same function names,
///    preventing typos or mismatches between caller and callee.
///
/// 2. **Maintainability**: When runtime function names change, only this file
///    needs to be updated rather than searching through the entire codebase.
///
/// 3. **Documentation**: Each runtime function's purpose is documented here,
///    serving as a reference for both compiler developers and runtime implementers.
///
/// The runtime functions are organized into logical categories:
/// - Terminal I/O: Console input/output operations
/// - String manipulation: String processing and conversion
/// - Math: Mathematical operations
/// - Collections: List, Set, and Map operations
/// - Boxing/Unboxing: Value type boxing for generic collections
/// - System: Process control and timing
/// - Threading: Multi-threaded execution support
///
/// @note All function names follow the pattern "Viper.<Category>.<Function>"
///       to match the runtime library's namespace structure.
///
//===----------------------------------------------------------------------===//

#pragma once

namespace il::frontends::viperlang::runtime {

//=============================================================================
// Terminal I/O Functions
//=============================================================================
/// @name Terminal I/O Functions
/// @brief Functions for console input and output operations.
///
/// These functions provide access to the terminal/console for text-based
/// interaction. They support ANSI escape codes on compatible terminals
/// for cursor positioning and color output.
/// @{

/// @brief Print a string followed by a newline to the console.
/// @details Signature: Say(str) -> void
/// This is the primary output function for ViperLang programs.
inline constexpr const char *kTerminalSay = "Viper.Terminal.Say";

/// @brief Print an integer followed by a newline to the console.
/// @details Signature: SayInt(i64) -> void
/// Convenience function that avoids string conversion overhead.
inline constexpr const char *kTerminalSayInt = "Viper.Terminal.SayInt";

/// @brief Print a floating-point number followed by a newline to the console.
/// @details Signature: SayNum(f64) -> void
/// Uses default formatting for double-precision numbers.
inline constexpr const char *kTerminalSayNum = "Viper.Terminal.SayNum";

/// @brief Print a string without a trailing newline.
/// @details Signature: Print(str) -> void
/// Useful for building output incrementally or creating progress indicators.
inline constexpr const char *kTerminalPrint = "Viper.Terminal.Print";

/// @brief Print a string followed by a platform-appropriate line ending.
/// @details Signature: PrintLine(str) -> void
/// Similar to Say but may use different line endings on different platforms.
inline constexpr const char *kTerminalPrintLine = "Viper.Terminal.PrintLine";

/// @brief Read a line of text from the console.
/// @details Signature: ReadLine() -> str
/// Blocks until the user presses Enter. Returns the input without the newline.
inline constexpr const char *kTerminalReadLine = "Viper.Terminal.ReadLine";

/// @brief Read a single keypress from the console.
/// @details Signature: ReadKey() -> i64
/// Returns the key code. May block until a key is pressed depending on terminal mode.
inline constexpr const char *kTerminalReadKey = "Viper.Terminal.ReadKey";

/// @brief Clear the console screen.
/// @details Signature: Clear() -> void
/// Clears all content and moves the cursor to the top-left corner.
inline constexpr const char *kTerminalClear = "Viper.Terminal.Clear";

/// @brief Set the cursor position on the console.
/// @details Signature: SetPosition(i64 row, i64 col) -> void
/// Coordinates are 1-based. Row 1, Col 1 is the top-left corner.
inline constexpr const char *kTerminalSetPosition = "Viper.Terminal.SetPosition";

/// @brief Set the text color for subsequent console output.
/// @details Signature: SetColor(i64 foreground, i64 background) -> void
/// Color values are platform-specific ANSI color codes (0-15).
inline constexpr const char *kTerminalSetColor = "Viper.Terminal.SetColor";

/// @brief Get the width of the console in characters.
/// @details Signature: GetWidth() -> i64
/// Returns the number of columns available for output.
inline constexpr const char *kTerminalGetWidth = "Viper.Terminal.GetWidth";

/// @brief Get the height of the console in lines.
/// @details Signature: GetHeight() -> i64
/// Returns the number of rows available for output.
inline constexpr const char *kTerminalGetHeight = "Viper.Terminal.GetHeight";

/// @brief Hide the text cursor.
/// @details Signature: HideCursor() -> void
/// Useful for games or animations where the cursor is distracting.
inline constexpr const char *kTerminalHideCursor = "Viper.Terminal.HideCursor";

/// @brief Show the text cursor.
/// @details Signature: ShowCursor() -> void
/// Restores cursor visibility after HideCursor.
inline constexpr const char *kTerminalShowCursor = "Viper.Terminal.ShowCursor";

/// @brief Check if a keypress is available without blocking.
/// @details Signature: KeyAvailable() -> i64
/// Returns non-zero if ReadKey would return immediately.
inline constexpr const char *kTerminalKeyAvailable = "Viper.Terminal.KeyAvailable";

/// @}

//=============================================================================
// String Functions
//=============================================================================
/// @name String Functions
/// @brief Functions for string manipulation and conversion.
///
/// ViperLang strings are immutable UTF-8 sequences. These functions create
/// new strings rather than modifying existing ones. String comparisons are
/// case-sensitive unless otherwise noted.
/// @{

/// @brief Concatenate two strings.
/// @details Signature: Concat(str, str) -> str
/// Returns a new string containing the first string followed by the second.
inline constexpr const char *kStringConcat = "Viper.String.Concat";

/// @brief Compare two strings for equality.
/// @details Signature: Equals(str, str) -> i1
/// Returns true if the strings contain identical byte sequences.
inline constexpr const char *kStringEquals = "Viper.Strings.Equals";

/// @brief Get the length of a string in bytes.
/// @details Signature: Length(str) -> i64
/// Note: Returns byte count, not character count for multi-byte UTF-8.
inline constexpr const char *kStringLength = "Viper.String.Length";

/// @brief Extract a substring from a string.
/// @details Signature: Substring(str, i64 start, i64 length) -> str
/// Returns a new string containing the specified portion.
inline constexpr const char *kStringSubstring = "Viper.String.Substring";

/// @brief Check if a string contains a substring.
/// @details Signature: Contains(str haystack, str needle) -> i64
/// Returns non-zero if needle is found anywhere in haystack.
inline constexpr const char *kStringContains = "Viper.String.Contains";

/// @brief Check if a string starts with a prefix.
/// @details Signature: StartsWith(str, str prefix) -> i64
/// Returns non-zero if the string begins with the prefix.
inline constexpr const char *kStringStartsWith = "Viper.String.StartsWith";

/// @brief Check if a string ends with a suffix.
/// @details Signature: EndsWith(str, str suffix) -> i64
/// Returns non-zero if the string ends with the suffix.
inline constexpr const char *kStringEndsWith = "Viper.String.EndsWith";

/// @brief Find the first occurrence of a substring.
/// @details Signature: IndexOf(str haystack, str needle) -> i64
/// Returns the byte index of the first match, or -1 if not found.
inline constexpr const char *kStringIndexOf = "Viper.String.IndexOf";

/// @brief Convert a string to uppercase.
/// @details Signature: ToUpper(str) -> str
/// Returns a new string with all ASCII letters converted to uppercase.
inline constexpr const char *kStringToUpper = "Viper.String.ToUpper";

/// @brief Convert a string to lowercase.
/// @details Signature: ToLower(str) -> str
/// Returns a new string with all ASCII letters converted to lowercase.
inline constexpr const char *kStringToLower = "Viper.String.ToLower";

/// @brief Remove leading and trailing whitespace from a string.
/// @details Signature: Trim(str) -> str
/// Returns a new string with spaces, tabs, and newlines removed from both ends.
inline constexpr const char *kStringTrim = "Viper.String.Trim";

/// @brief Split a string into a list of substrings.
/// @details Signature: Split(str, str delimiter) -> ptr (List)
/// Returns a List containing the parts of the string separated by the delimiter.
inline constexpr const char *kStringSplit = "Viper.String.Split";

/// @brief Convert an integer to its string representation.
/// @details Signature: FromInt(i64) -> str
/// Converts the integer to a decimal string (e.g., 42 -> "42").
inline constexpr const char *kStringFromInt = "Viper.Strings.FromInt";

/// @brief Convert a floating-point number to its string representation.
/// @details Signature: FromDouble(f64) -> str
/// Uses default formatting with appropriate precision.
inline constexpr const char *kStringFromNum = "Viper.Strings.FromDouble";

/// @brief Get the character at a specific index in a string.
/// @details Signature: CharAt(str, i64 index) -> i64
/// Returns the byte value at the given index. For ASCII, this is the character code.
inline constexpr const char *kStringCharAt = "Viper.String.CharAt";

/// @}

//=============================================================================
// Math Functions
//=============================================================================
/// @name Math Functions
/// @brief Mathematical operations for numeric computations.
///
/// These functions provide common mathematical operations. All functions
/// work with 64-bit floating-point numbers (f64) unless otherwise noted.
/// Results follow IEEE 754 semantics for special values (NaN, Infinity).
/// @{

/// @brief Compute the absolute value of a number.
/// @details Signature: Abs(f64) -> f64
/// Returns the non-negative value of the input.
inline constexpr const char *kMathAbs = "Viper.Math.Abs";

/// @brief Compute the square root of a number.
/// @details Signature: Sqrt(f64) -> f64
/// Returns NaN for negative inputs.
inline constexpr const char *kMathSqrt = "Viper.Math.Sqrt";

/// @brief Raise a number to a power.
/// @details Signature: Pow(f64 base, f64 exponent) -> f64
/// Computes base^exponent.
inline constexpr const char *kMathPow = "Viper.Math.Pow";

/// @brief Compute the sine of an angle in radians.
/// @details Signature: Sin(f64) -> f64
/// Returns a value in the range [-1, 1].
inline constexpr const char *kMathSin = "Viper.Math.Sin";

/// @brief Compute the cosine of an angle in radians.
/// @details Signature: Cos(f64) -> f64
/// Returns a value in the range [-1, 1].
inline constexpr const char *kMathCos = "Viper.Math.Cos";

/// @brief Compute the tangent of an angle in radians.
/// @details Signature: Tan(f64) -> f64
/// Returns infinity at odd multiples of pi/2.
inline constexpr const char *kMathTan = "Viper.Math.Tan";

/// @brief Round a number down to the nearest integer.
/// @details Signature: Floor(f64) -> f64
/// Returns the largest integer less than or equal to the input.
inline constexpr const char *kMathFloor = "Viper.Math.Floor";

/// @brief Round a number up to the nearest integer.
/// @details Signature: Ceil(f64) -> f64
/// Returns the smallest integer greater than or equal to the input.
inline constexpr const char *kMathCeil = "Viper.Math.Ceil";

/// @brief Round a number to the nearest integer.
/// @details Signature: Round(f64) -> f64
/// Uses round-half-to-even (banker's rounding) for .5 cases.
inline constexpr const char *kMathRound = "Viper.Math.Round";

/// @brief Return the smaller of two numbers.
/// @details Signature: Min(f64, f64) -> f64
/// Returns the first argument if they are equal.
inline constexpr const char *kMathMin = "Viper.Math.Min";

/// @brief Return the larger of two numbers.
/// @details Signature: Max(f64, f64) -> f64
/// Returns the first argument if they are equal.
inline constexpr const char *kMathMax = "Viper.Math.Max";

/// @brief Generate a random number between 0 and 1.
/// @details Signature: Random() -> f64
/// Returns a uniformly distributed value in [0, 1).
inline constexpr const char *kMathRandom = "Viper.Math.Random";

/// @brief Generate a random integer in a range.
/// @details Signature: RandomRange(i64 min, i64 max) -> i64
/// Returns a uniformly distributed integer in [min, max].
inline constexpr const char *kMathRandomRange = "Viper.Math.RandomRange";

/// @}

//=============================================================================
// Collection Functions
//=============================================================================
/// @name Collection Functions
/// @brief Functions for working with Lists, Sets, and Maps.
///
/// Collections in ViperLang store boxed values to support heterogeneous
/// element types. All collection operations that access elements return
/// boxed pointers that must be unboxed to the appropriate type.
/// @{

/// @brief Create a new empty List.
/// @details Signature: New() -> ptr
/// Returns a pointer to a newly allocated List object.
inline constexpr const char *kListNew = "Viper.Collections.List.New";

/// @brief Add an element to the end of a List.
/// @details Signature: Add(ptr list, ptr element) -> void
/// The element must be a boxed value.
inline constexpr const char *kListAdd = "Viper.Collections.List.Add";

/// @brief Get an element from a List by index.
/// @details Signature: get_Item(ptr list, i64 index) -> ptr
/// Returns a boxed value. Throws if index is out of bounds.
inline constexpr const char *kListGet = "Viper.Collections.List.get_Item";

/// @brief Set an element in a List by index.
/// @details Signature: set_Item(ptr list, i64 index, ptr element) -> void
/// Throws if index is out of bounds.
inline constexpr const char *kListSet = "Viper.Collections.List.set_Item";

/// @brief Get the number of elements in a List.
/// @details Signature: get_Count(ptr list) -> i64
/// Returns the current length of the list.
inline constexpr const char *kListCount = "Viper.Collections.List.get_Count";

/// @brief Remove all elements from a List.
/// @details Signature: Clear(ptr list) -> void
/// The list becomes empty but remains allocated.
inline constexpr const char *kListClear = "Viper.Collections.List.Clear";

/// @brief Remove an element from a List by index.
/// @details Signature: RemoveAt(ptr list, i64 index) -> void
/// Shifts subsequent elements down. Throws if index is out of bounds.
inline constexpr const char *kListRemoveAt = "Viper.Collections.List.RemoveAt";

/// @brief Check if a List contains an element.
/// @details Signature: Contains(ptr list, ptr element) -> i64
/// Returns non-zero if the element is found (using equality comparison).
inline constexpr const char *kListContains = "Viper.Collections.List.Contains";

/// @brief Create a new empty Set.
/// @details Signature: New() -> ptr
/// Returns a pointer to a newly allocated Set object.
inline constexpr const char *kSetNew = "Viper.Collections.Set.New";

/// @brief Create a new empty Map.
/// @details Signature: New() -> ptr
/// Returns a pointer to a newly allocated Map object.
inline constexpr const char *kMapNew = "Viper.Collections.Map.New";

/// @brief Set a key-value pair in a Map.
/// @details Signature: set_Item(ptr map, ptr key, ptr value) -> void
/// Inserts or updates the value associated with the key.
inline constexpr const char *kMapSet = "Viper.Collections.Map.set_Item";

/// @brief Get a value from a Map by key.
/// @details Signature: get_Item(ptr map, ptr key) -> ptr
/// Returns the value associated with the key, or null if not found.
inline constexpr const char *kMapGet = "Viper.Collections.Map.get_Item";

/// @brief Check if a Map contains a key.
/// @details Signature: ContainsKey(ptr map, ptr key) -> i64
/// Returns non-zero if the key exists in the map.
inline constexpr const char *kMapContainsKey = "Viper.Collections.Map.ContainsKey";

/// @brief Get the number of entries in a Map.
/// @details Signature: Count(ptr map) -> i64
/// Returns the number of key-value pairs in the map.
inline constexpr const char *kMapCount = "Viper.Collections.Map.Count";

/// @brief Remove a key-value pair from a Map.
/// @details Signature: Remove(ptr map, ptr key) -> i64
/// Returns non-zero if the key was found and removed.
inline constexpr const char *kMapRemove = "Viper.Collections.Map.Remove";

/// @brief Clear all entries from a Map.
/// @details Signature: Clear(ptr map) -> void
/// Removes all key-value pairs from the map.
inline constexpr const char *kMapClear = "Viper.Collections.Map.Clear";

/// @}

//=============================================================================
// Boxing/Unboxing Functions
//=============================================================================
/// @name Boxing/Unboxing Functions
/// @brief Functions for converting between primitive types and boxed objects.
///
/// Boxing wraps a primitive value in a heap-allocated object so it can be
/// stored in generic collections or passed where an object reference is expected.
/// Unboxing extracts the primitive value from a boxed object.
///
/// @note Boxing allocates memory and may trigger garbage collection.
/// @note Unboxing from an incorrectly-typed box results in undefined behavior.
/// @{

/// @brief Box a 64-bit integer value.
/// @details Signature: I64(i64) -> ptr
/// Returns a pointer to a heap-allocated box containing the integer.
inline constexpr const char *kBoxI64 = "Viper.Box.I64";

/// @brief Box a 64-bit floating-point value.
/// @details Signature: F64(f64) -> ptr
/// Returns a pointer to a heap-allocated box containing the float.
inline constexpr const char *kBoxF64 = "Viper.Box.F64";

/// @brief Box a boolean value.
/// @details Signature: I1(i1) -> ptr
/// Returns a pointer to a heap-allocated box containing the boolean.
inline constexpr const char *kBoxI1 = "Viper.Box.I1";

/// @brief Box a string value.
/// @details Signature: Str(str) -> ptr
/// Returns a pointer to a heap-allocated box containing the string reference.
inline constexpr const char *kBoxStr = "Viper.Box.Str";

/// @brief Unbox a boxed value to a 64-bit integer.
/// @details Signature: ToI64(ptr) -> i64
/// Extracts the integer from a box. The box must contain an i64.
inline constexpr const char *kUnboxI64 = "Viper.Box.ToI64";

/// @brief Unbox a boxed value to a 64-bit float.
/// @details Signature: ToF64(ptr) -> f64
/// Extracts the float from a box. The box must contain an f64.
inline constexpr const char *kUnboxF64 = "Viper.Box.ToF64";

/// @brief Unbox a boxed value to a boolean.
/// @details Signature: ToI1(ptr) -> i1
/// Extracts the boolean from a box. The box must contain an i1.
inline constexpr const char *kUnboxI1 = "Viper.Box.ToI1";

/// @brief Unbox a boxed value to a string.
/// @details Signature: ToStr(ptr) -> str
/// Extracts the string from a box. The box must contain a str.
inline constexpr const char *kUnboxStr = "Viper.Box.ToStr";

/// @}

//=============================================================================
// System Functions
//=============================================================================
/// @name System Functions
/// @brief Functions for process control and system interaction.
///
/// These functions provide access to operating system functionality
/// for process management and timing.
/// @{

/// @brief Pause execution for a specified duration.
/// @details Signature: Sleep(i64 milliseconds) -> void
/// Suspends the current thread for at least the specified number of milliseconds.
inline constexpr const char *kSystemSleep = "Viper.System.Sleep";

/// @brief Terminate the program with an exit code.
/// @details Signature: Exit(i64 code) -> void
/// Immediately terminates the program. Does not return.
inline constexpr const char *kSystemExit = "Viper.System.Exit";

/// @brief Get the current system time in milliseconds.
/// @details Signature: GetTime() -> i64
/// Returns milliseconds since the Unix epoch (1970-01-01 00:00:00 UTC).
inline constexpr const char *kSystemGetTime = "Viper.System.GetTime";

/// @}

//=============================================================================
// Threading Functions
//=============================================================================
/// @name Threading Functions
/// @brief Functions for multi-threaded program execution.
///
/// These functions provide basic threading primitives. Thread safety of
/// shared data is the programmer's responsibility.
///
/// @warning Threading support is experimental and may change.
/// @{

/// @brief Create and start a new thread.
/// @details Signature: Spawn(ptr function) -> ptr
/// The function must take no arguments and return void.
/// Returns a thread handle for use with Join.
inline constexpr const char *kThreadSpawn = "Viper.Thread.Spawn";

/// @brief Wait for a thread to complete.
/// @details Signature: Join(ptr thread) -> void
/// Blocks until the specified thread terminates.
inline constexpr const char *kThreadJoin = "Viper.Thread.Join";

/// @brief Pause the current thread for a duration.
/// @details Signature: Sleep(i64 milliseconds) -> void
/// Similar to System.Sleep but specifically for the current thread.
inline constexpr const char *kThreadSleep = "Viper.Thread.Sleep";

/// @}

//=============================================================================
// Runtime Allocator
//=============================================================================
/// @name Runtime Allocator
/// @brief Low-level memory allocation for runtime objects.
///
/// This function is used internally by the compiler to allocate entity
/// instances and other heap objects. User code should use `new` expressions
/// rather than calling this directly.
/// @{

/// @brief Allocate memory for a runtime object.
/// @details Signature: rt_alloc(i64 classId, i64 size) -> ptr
/// Allocates `size` bytes and initializes the object header with `classId`.
/// Returns a pointer to the allocated object.
/// @note This is an internal runtime function, not part of the Viper.* namespace.
inline constexpr const char *kRtAlloc = "rt_alloc";

/// @}

//=============================================================================
// Configuration Constants
//=============================================================================
/// @name Configuration Constants
/// @brief Compile-time constants for compiler behavior and object layout.
///
/// These constants define limits and sizes used during compilation.
/// They ensure consistent behavior and prevent resource exhaustion.
/// @{

/// @brief Maximum depth for import recursion to prevent stack overflow.
/// @details When processing imports, the compiler tracks the current depth
/// to detect and prevent import cycles or excessively deep import chains.
/// If this limit is exceeded, compilation fails with an error.
inline constexpr size_t kMaxImportDepth = 50;

/// @brief Maximum number of imported files to prevent runaway compilation.
/// @details Limits the total number of unique files that can be imported
/// during a single compilation. This prevents pathological cases where
/// the import graph grows exponentially.
inline constexpr size_t kMaxImportedFiles = 100;

/// @brief Object header size for entity types in bytes.
/// @details All entity instances begin with an 8-byte header containing
/// the vtable pointer (or class ID for runtime type identification).
/// Field offsets in EntityTypeInfo are calculated starting after this header.
inline constexpr size_t kObjectHeaderSize = 8;

/// @}

} // namespace il::frontends::viperlang::runtime
