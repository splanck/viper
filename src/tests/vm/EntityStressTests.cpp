//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/EntityStressTests.cpp
// Purpose: Stress test for VM entity allocation to detect memory management
//          issues under sustained allocation pressure (BUG-VM-001 regression).
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include "vm/VM.hpp"

#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// @brief Compile Zia source and run in VM, returning the exit code.
int64_t compileAndRun(const std::string &source)
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = "stress.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    if (!result.succeeded())
        return -1;

    il::vm::VM vm(result.module);
    return vm.run();
}

//===----------------------------------------------------------------------===//
// BUG-VM-001: Heavy entity allocation stress tests
//===----------------------------------------------------------------------===//

/// @brief Allocate many entity objects in a loop to test allocation pressure.
TEST(VMEntityStress, AllocateManyEntities)
{
    const std::string source = R"(
module Test;

entity Point {
    expose Integer x;
    expose Integer y;
}

func start() {
    var i = 0;
    while i < 500 {
        var p = new Point();
        p.x = i;
        p.y = i * 2;
        i = i + 1;
    }
}
)";
    int64_t rc = compileAndRun(source);
    EXPECT_EQ(rc, 0);
}

/// @brief Entity with List field — allocate and populate in a loop.
TEST(VMEntityStress, EntityWithListFieldStress)
{
    const std::string source = R"(
module Test;

entity Container {
    expose List[Integer] items;
    expose func init() { items = []; }
    expose func addItem(v: Integer) { items.add(v); }
}

func start() {
    var c = new Container();
    c.init();
    var i = 0;
    while i < 200 {
        c.addItem(i);
        i = i + 1;
    }
}
)";
    int64_t rc = compileAndRun(source);
    EXPECT_EQ(rc, 0);
}

/// @brief Chained entity allocation — entities referencing other entities.
TEST(VMEntityStress, ChainedEntityAllocation)
{
    const std::string source = R"(
module Test;

entity Node {
    expose Integer value;
    expose Node next;
}

func start() {
    var head = new Node();
    head.value = 0;
    var current = head;
    var i = 1;
    while i < 100 {
        var n = new Node();
        n.value = i;
        current.next = n;
        current = n;
        i = i + 1;
    }
}
)";
    int64_t rc = compileAndRun(source);
    EXPECT_EQ(rc, 0);
}

/// @brief Multiple entity types allocated in interleaved pattern.
TEST(VMEntityStress, InterleavedMultiEntityAlloc)
{
    const std::string source = R"(
module Test;

entity TypeA {
    expose Integer a;
}

entity TypeB {
    expose Integer b;
    expose Integer extra;
}

entity TypeC {
    expose Integer c;
    expose TypeA ref;
}

func start() {
    var i = 0;
    while i < 200 {
        var a = new TypeA();
        a.a = i;
        var b = new TypeB();
        b.b = i * 2;
        b.extra = i + 100;
        var c = new TypeC();
        c.c = i * 3;
        c.ref = a;
        i = i + 1;
    }
}
)";
    int64_t rc = compileAndRun(source);
    EXPECT_EQ(rc, 0);
}

/// @brief Entity with multiple List fields — forward reference stress.
TEST(VMEntityStress, ForwardRefEntityFieldChainStress)
{
    const std::string source = R"(
module Test;

entity Manager {
    expose Store store;
    expose func init() {
        store = new Store();
        store.init();
    }
    expose func populate() {
        var i = 0;
        while i < 100 {
            store.values.add(i);
            i = i + 1;
        }
    }
}

entity Store {
    expose List[Integer] values;
    expose func init() { values = []; }
}

func start() {
    var m = new Manager();
    m.init();
    m.populate();
}
)";
    int64_t rc = compileAndRun(source);
    EXPECT_EQ(rc, 0);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
