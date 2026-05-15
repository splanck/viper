//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime_classes/TestRuntimeWishlistBindings.cpp
// Purpose: Regression coverage for runtime APIs added from Zia friction notes.
//
//===----------------------------------------------------------------------===//

#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <string>

TEST(RuntimeWishlistBindings, CanvasDeltaAliasesAreRegistered) {
    const auto &registry = il::runtime::RuntimeRegistry::instance();

    auto dtMs = registry.findProperty("Viper.Graphics.Canvas", "DeltaTimeMs");
    ASSERT_TRUE(dtMs.has_value());
    EXPECT_EQ(dtMs->type, il::runtime::ILScalarType::I64);
    EXPECT_EQ(std::string(dtMs->getter), "Viper.Graphics.Canvas.get_DeltaTimeMs");

    auto dtSec = registry.findProperty("Viper.Graphics.Canvas", "DeltaTimeSec");
    ASSERT_TRUE(dtSec.has_value());
    EXPECT_EQ(dtSec->type, il::runtime::ILScalarType::F64);
    EXPECT_EQ(std::string(dtSec->getter), "Viper.Graphics.Canvas.get_DeltaTimeSec");

    auto dt3dMs = registry.findProperty("Viper.Graphics3D.Canvas3D", "DeltaTimeMs");
    ASSERT_TRUE(dt3dMs.has_value());
    EXPECT_EQ(dt3dMs->type, il::runtime::ILScalarType::I64);

    auto dt3dSec = registry.findProperty("Viper.Graphics3D.Canvas3D", "DeltaTimeSec");
    ASSERT_TRUE(dt3dSec.has_value());
    EXPECT_EQ(dt3dSec->type, il::runtime::ILScalarType::F64);
}

TEST(RuntimeWishlistBindings, WorldProjectionMethodsAreRegistered) {
    const auto &registry = il::runtime::RuntimeRegistry::instance();

    auto linearX = registry.findMethod("Viper.Game.WorldToScreenProjection", "LinearX", 4);
    ASSERT_TRUE(linearX.has_value());
    EXPECT_EQ(linearX->signature.returnType, il::runtime::ILScalarType::F64);
    EXPECT_EQ(std::string(linearX->target), "Viper.Game.WorldToScreenProjection.LinearX");

    auto linearY = registry.findMethod("Viper.Game.WorldToScreenProjection", "LinearY", 5);
    ASSERT_TRUE(linearY.has_value());
    EXPECT_EQ(linearY->signature.returnType, il::runtime::ILScalarType::F64);
    ASSERT_EQ(linearY->signature.params.size(), static_cast<std::size_t>(5));
    EXPECT_EQ(linearY->signature.params[4], il::runtime::ILScalarType::Bool);

    auto isoX = registry.findMethod("Viper.Game.WorldToScreenProjection", "IsometricX", 4);
    ASSERT_TRUE(isoX.has_value());
    EXPECT_EQ(isoX->signature.returnType, il::runtime::ILScalarType::F64);

    auto perspectiveScale =
        registry.findMethod("Viper.Game.WorldToScreenProjection", "PerspectiveScale", 5);
    ASSERT_TRUE(perspectiveScale.has_value());
    EXPECT_EQ(perspectiveScale->signature.returnType, il::runtime::ILScalarType::F64);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
