#===----------------------------------------------------------------------===//
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===//
#
# Build configuration for rtgen - the runtime registry generator tool.
#
# This tool parses runtime.def and generates .inc files used by the runtime.
#
#===----------------------------------------------------------------------===//

add_executable(rtgen rtgen.cpp)

target_compile_features(rtgen PRIVATE cxx_std_17)

# On macOS, link with the standard library explicitly
if (APPLE)
    target_link_libraries(rtgen PRIVATE "-lc++")
endif ()
