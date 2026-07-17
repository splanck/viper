//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

TEST(RuntimeWishlistBindings, CanvasDeltaPropertiesAreRegistered) {
    const auto &registry = il::runtime::RuntimeRegistry::instance();

    auto dt = registry.findProperty("Zanna.Graphics.Canvas", "DeltaTime");
    ASSERT_TRUE(dt.has_value());
    EXPECT_EQ(dt->type, il::runtime::ILScalarType::I64);
    EXPECT_EQ(std::string(dt->getter), "Zanna.Graphics.Canvas.get_DeltaTime");

    auto oldDtMs = registry.findProperty("Zanna.Graphics.Canvas", "DeltaTimeMs");
    EXPECT_FALSE(oldDtMs.has_value());

    auto dtSec = registry.findProperty("Zanna.Graphics.Canvas", "DeltaTimeSec");
    ASSERT_TRUE(dtSec.has_value());
    EXPECT_EQ(dtSec->type, il::runtime::ILScalarType::F64);
    EXPECT_EQ(std::string(dtSec->getter), "Zanna.Graphics.Canvas.get_DeltaTimeSec");

    auto dt3d = registry.findProperty("Zanna.Graphics3D.Canvas3D", "DeltaTime");
    ASSERT_TRUE(dt3d.has_value());
    EXPECT_EQ(dt3d->type, il::runtime::ILScalarType::I64);
    EXPECT_EQ(std::string(dt3d->getter), "Zanna.Graphics3D.Canvas3D.get_DeltaTime");

    auto oldDt3dMs = registry.findProperty("Zanna.Graphics3D.Canvas3D", "DeltaTimeMs");
    EXPECT_FALSE(oldDt3dMs.has_value());

    auto dt3dSec = registry.findProperty("Zanna.Graphics3D.Canvas3D", "DeltaTimeSec");
    ASSERT_TRUE(dt3dSec.has_value());
    EXPECT_EQ(dt3dSec->type, il::runtime::ILScalarType::F64);
}

TEST(RuntimeWishlistBindings, WorldProjectionMethodsAreRegistered) {
    const auto &registry = il::runtime::RuntimeRegistry::instance();

    auto linearX = registry.findMethod("Zanna.Game.WorldToScreenProjection", "LinearX", 4);
    ASSERT_TRUE(linearX.has_value());
    EXPECT_EQ(linearX->signature.returnType, il::runtime::ILScalarType::F64);
    EXPECT_EQ(std::string(linearX->target), "Zanna.Game.WorldToScreenProjection.LinearX");

    auto linearY = registry.findMethod("Zanna.Game.WorldToScreenProjection", "LinearY", 5);
    ASSERT_TRUE(linearY.has_value());
    EXPECT_EQ(linearY->signature.returnType, il::runtime::ILScalarType::F64);
    ASSERT_EQ(linearY->signature.params.size(), static_cast<std::size_t>(5));
    EXPECT_EQ(linearY->signature.params[4], il::runtime::ILScalarType::Bool);

    auto isoX = registry.findMethod("Zanna.Game.WorldToScreenProjection", "IsometricX", 4);
    ASSERT_TRUE(isoX.has_value());
    EXPECT_EQ(isoX->signature.returnType, il::runtime::ILScalarType::F64);

    auto perspectiveScale =
        registry.findMethod("Zanna.Game.WorldToScreenProjection", "PerspectiveScale", 5);
    ASSERT_TRUE(perspectiveScale.has_value());
    EXPECT_EQ(perspectiveScale->signature.returnType, il::runtime::ILScalarType::F64);
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
