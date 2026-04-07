//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "il/runtime/classes/RuntimeClasses.hpp"
#include "tests/TestHarness.hpp"

#include <sstream>
#include <string>
#include <vector>

TEST(RuntimeClassCatalogTypedReturns, RawTargetsMatchMethodMetadata) {
    const auto &registry = il::runtime::RuntimeRegistry::instance();
    std::vector<std::string> errors;

    const auto &classes = il::runtime::runtimeClassCatalog();
    for (const auto &cls : classes) {
        for (const auto &method : cls.methods) {
            if (!method.target || !method.signature)
                continue;

            auto methodSig = il::runtime::parseRuntimeSignature(method.signature);
            auto rawSig = registry.findFunction(method.target);
            if (!rawSig || !methodSig.isValid())
                continue;

            const bool methodHasTypedReturn = !methodSig.containerTypeName.empty() ||
                                              !methodSig.elementTypeName.empty() ||
                                              !methodSig.objectTypeName.empty();
            const bool rawHasTypedReturn = !rawSig->containerTypeName.empty() ||
                                           !rawSig->elementTypeName.empty() ||
                                           !rawSig->objectTypeName.empty();
            if (!methodHasTypedReturn && !rawHasTypedReturn)
                continue;

            if (methodSig.returnType != rawSig->returnType ||
                methodSig.containerTypeName != rawSig->containerTypeName ||
                methodSig.elementTypeName != rawSig->elementTypeName ||
                methodSig.objectTypeName != rawSig->objectTypeName) {
                std::ostringstream os;
                os << "typed return mismatch for " << cls.qname << "." << method.name
                   << " via target " << method.target;
                errors.push_back(os.str());
            }
        }
    }

    if (!errors.empty()) {
        std::ostringstream os;
        os << "Typed runtime return metadata drifted (" << errors.size() << "):\n";
        for (const auto &e : errors)
            os << "  - " << e << "\n";
        std::cerr << os.str();
    }

    EXPECT_TRUE(errors.empty());
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
