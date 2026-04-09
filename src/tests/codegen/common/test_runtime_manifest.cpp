//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
#include "viper/runtime/RuntimeComponentManifest.hpp"

using namespace viper::codegen;

TEST(RuntimeManifest, GeneratedArchiveCountMatchesEnum) {
    EXPECT_EQ(viper::runtime_manifest::kRuntimeComponentArchives.size(),
              static_cast<size_t>(RtComponent::Count));
}

TEST(RuntimeManifest, ArchiveNamesRemainStableForKnownComponents) {
    EXPECT_EQ("viper_rt_base", archiveNameForComponent(RtComponent::Base));
    EXPECT_EQ("viper_rt_graphics", archiveNameForComponent(RtComponent::Graphics));
    EXPECT_EQ("viper_rt_audio", archiveNameForComponent(RtComponent::Audio));
    EXPECT_EQ("viper_rt_network", archiveNameForComponent(RtComponent::Network));
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
