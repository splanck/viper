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

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
