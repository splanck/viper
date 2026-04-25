#pragma once

// Generated build/host capability header.

#define VIPER_HOST_WINDOWS 1
#define VIPER_HOST_MACOS 0
#define VIPER_HOST_LINUX 0

#define VIPER_COMPILER_MSVC 1
#define VIPER_COMPILER_CLANG 0
#define VIPER_COMPILER_GCC 0

#define VIPER_BUILD_HAS_GRAPHICS 1
#define VIPER_BUILD_HAS_AUDIO 1
#define VIPER_BUILD_HAS_GUI 1

#define VIPER_BUILD_NATIVE_LINK_X86_64 0
#define VIPER_BUILD_NATIVE_LINK_AARCH64 0

namespace viper::platform {
inline constexpr bool kHostWindows = VIPER_HOST_WINDOWS != 0;
inline constexpr bool kHostMacOS = VIPER_HOST_MACOS != 0;
inline constexpr bool kHostLinux = VIPER_HOST_LINUX != 0;

inline constexpr bool kCompilerMSVC = VIPER_COMPILER_MSVC != 0;
inline constexpr bool kCompilerClang = VIPER_COMPILER_CLANG != 0;
inline constexpr bool kCompilerGCC = VIPER_COMPILER_GCC != 0;

inline constexpr bool kBuildHasGraphics = VIPER_BUILD_HAS_GRAPHICS != 0;
inline constexpr bool kBuildHasAudio = VIPER_BUILD_HAS_AUDIO != 0;
inline constexpr bool kBuildHasGUI = VIPER_BUILD_HAS_GUI != 0;

inline constexpr bool kNativeLinkX86_64 = VIPER_BUILD_NATIVE_LINK_X86_64 != 0;
inline constexpr bool kNativeLinkAArch64 = VIPER_BUILD_NATIVE_LINK_AARCH64 != 0;

inline constexpr bool kCanFork = !kHostWindows;
inline constexpr bool kCanLocalBind = true;
inline constexpr bool kCanIpv6Loopback = true;
} // namespace viper::platform
