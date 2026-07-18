//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/codegen/common/test_runtime_manifest.cpp
// Purpose: Validate the generated runtime component/archive manifest used by
//          native-link archive selection.
// Key invariants: Every private cross-archive runtime symbol must select the
//                 archive that defines it.
// Ownership/Lifetime: Tests use only stack-owned symbol views and manifest data.
// Links: src/codegen/common/RuntimeComponents.hpp
//        src/codegen/common/LinkerSupport.cpp
// Cross-platform touchpoints: Runtime archive closure is shared by Windows,
//                             macOS, and Linux native linkers.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/RuntimeComponents.hpp"
#include "tests/TestHarness.hpp"
#include "zanna/runtime/RuntimeComponentManifest.hpp"

using namespace zanna::codegen;

TEST(RuntimeManifest, GeneratedArchiveCountMatchesEnum) {
    EXPECT_EQ(zanna::runtime_manifest::kRuntimeComponentArchives.size(),
              static_cast<size_t>(RtComponent::Count));
}

TEST(RuntimeManifest, ArchiveNamesRemainStableForKnownComponents) {
    EXPECT_EQ("zanna_rt_base", archiveNameForComponent(RtComponent::Base));
    EXPECT_EQ("zanna_rt_graphics", archiveNameForComponent(RtComponent::Graphics));
    EXPECT_EQ("zanna_rt_audio", archiveNameForComponent(RtComponent::Audio));
    EXPECT_EQ("zanna_rt_network", archiveNameForComponent(RtComponent::Network));
}

TEST(RuntimeManifest, PrivateOggReaderSymbolsSelectAudioArchive) {
    constexpr std::string_view symbols[] = {
        "ogg_reader_free",
        "ogg_reader_next_packet_ex",
        "ogg_reader_open_mem",
        "ogg_reader_rewind",
    };

    for (const auto symbol : symbols) {
        const auto component = componentForRuntimeSymbol(symbol);
        ASSERT_TRUE(component.has_value());
        EXPECT_TRUE(*component == RtComponent::Audio);
    }
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
