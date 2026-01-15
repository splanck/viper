//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia entity types (OOP features).
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Basic Entity Definition
//===----------------------------------------------------------------------===//

/// @brief Test basic entity with fields.
TEST(ZiaEntities, BasicFields)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Point {
    expose Integer x;
    expose Integer y;
}

func start() {
    var p = new Point();
    p.x = 10;
    p.y = 20;
    Viper.Terminal.SayInt(p.x);
    Viper.Terminal.SayInt(p.y);
}
)";
    CompilerInput input{.source = source, .path = "point.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test entity with methods.
TEST(ZiaEntities, BasicMethods)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Counter {
    expose Integer count;

    expose func increment() {
        count = count + 1;
    }

    expose func decrement() {
        count = count - 1;
    }

    expose func getCount() -> Integer {
        return count;
    }
}

func start() {
    var c = new Counter();
    c.count = 0;
    c.increment();
    c.increment();
    c.increment();
    c.decrement();
    Viper.Terminal.SayInt(c.getCount());
}
)";
    CompilerInput input{.source = source, .path = "counter.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test entity with method parameters.
TEST(ZiaEntities, MethodWithParameters)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Calculator {
    expose Integer result;

    expose func add(Integer a, Integer b) -> Integer {
        return a + b;
    }

    expose func multiply(Integer a, Integer b) -> Integer {
        return a * b;
    }

    expose func setResult(Integer value) {
        result = value;
    }
}

func start() {
    var calc = new Calculator();
    Integer sum = calc.add(5, 3);
    Integer product = calc.multiply(4, 7);
    calc.setResult(sum + product);
    Viper.Terminal.SayInt(calc.result);
}
)";
    CompilerInput input{.source = source, .path = "calculator.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Visibility
//===----------------------------------------------------------------------===//

/// @brief Test expose visibility modifier on entity members.
TEST(ZiaEntities, Visibility)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity SecureData {
    expose Integer secretValue;
    expose Integer publicValue;

    expose func getDoubled() -> Integer {
        return secretValue * 2;
    }

    expose func setSecret(Integer value) {
        secretValue = value;
    }
}

func start() {
    var data = new SecureData();
    data.publicValue = 10;
    data.setSecret(21);
    Viper.Terminal.SayInt(data.getDoubled());
}
)";
    CompilerInput input{.source = source, .path = "visibility.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Self Reference
//===----------------------------------------------------------------------===//

/// @brief Test self reference in methods.
TEST(ZiaEntities, SelfReference)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Node {
    expose Integer value;
    expose Node? next;

    expose func setNext(Node n) {
        self.next = n;
    }

    expose func getValue() -> Integer {
        return self.value;
    }
}

func start() {
    var n1 = new Node();
    var n2 = new Node();
    n1.value = 1;
    n2.value = 2;
    n1.setNext(n2);
    Viper.Terminal.SayInt(n1.getValue());
}
)";
    CompilerInput input{.source = source, .path = "selfref.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Entity Inheritance
//===----------------------------------------------------------------------===//

/// @brief Test entity composition (alternative to inheritance).
TEST(ZiaEntities, EntityComposition)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Animal {
    expose String name;

    expose func speak() -> String {
        return "...";
    }
}

entity Dog {
    expose Animal animal;
    expose Integer age;

    expose func bark() -> String {
        return "Woof!";
    }
}

func start() {
    var dog = new Dog();
    dog.animal = new Animal();
    dog.animal.name = "Buddy";
    dog.age = 3;
    Viper.Terminal.Say(dog.animal.name);
    Viper.Terminal.SayInt(dog.age);
    Viper.Terminal.Say(dog.bark());
}
)";
    CompilerInput input{.source = source, .path = "composition.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Multiple Fields and Types
//===----------------------------------------------------------------------===//

/// @brief Test entity with various field types.
TEST(ZiaEntities, VariousFieldTypes)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Player {
    expose String name;
    expose Integer score;
    expose Integer health;
    expose Boolean alive;
    expose List[String] inventory;
}

func start() {
    var p = new Player();
    p.name = "Hero";
    p.score = 1000;
    p.health = 100;
    p.alive = true;
    p.inventory = [];
    p.inventory.add("sword");
    p.inventory.add("shield");

    Viper.Terminal.Say(p.name);
    Viper.Terminal.SayInt(p.score);
    Viper.Terminal.SayInt(p.health);
    Viper.Terminal.SayBool(p.alive);
    Viper.Terminal.SayInt(p.inventory.count());
}
)";
    CompilerInput input{.source = source, .path = "player.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Entity with List Fields
//===----------------------------------------------------------------------===//

/// @brief Test entity containing list of entities.
TEST(ZiaEntities, EntityLists)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Item {
    expose String name;
    expose Integer value;
}

entity Inventory {
    expose List[Item] items;

    expose func addItem(Item item) {
        items.add(item);
    }

    expose func totalValue() -> Integer {
        var total = 0;
        for item in items {
            total = total + item.value;
        }
        return total;
    }
}

func start() {
    var inv = new Inventory();
    inv.items = [];

    var sword = new Item();
    sword.name = "Sword";
    sword.value = 100;

    var shield = new Item();
    shield.name = "Shield";
    shield.value = 50;

    inv.addItem(sword);
    inv.addItem(shield);

    Viper.Terminal.SayInt(inv.totalValue());
}
)";
    CompilerInput input{.source = source, .path = "inventory.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Entity Initialization
//===----------------------------------------------------------------------===//

/// @brief Test entity field initialization with defaults.
TEST(ZiaEntities, FieldDefaults)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Config {
    expose Integer width = 800;
    expose Integer height = 600;
    expose String title = "Default Title";
    expose Boolean fullscreen = false;
}

func start() {
    var config = new Config();
    Viper.Terminal.SayInt(config.width);
    Viper.Terminal.SayInt(config.height);
    Viper.Terminal.Say(config.title);
    Viper.Terminal.SayBool(config.fullscreen);
}
)";
    CompilerInput input{.source = source, .path = "fielddefaults.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Return Type Syntax Variants
//===----------------------------------------------------------------------===//

/// @brief Test arrow return type syntax.
TEST(ZiaEntities, ArrowReturnType)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Math {
    expose func add(Integer a, Integer b) -> Integer {
        return a + b;
    }

    expose func isPositive(Integer n) -> Boolean {
        return n > 0;
    }
}

func start() {
    var m = new Math();
    Viper.Terminal.SayInt(m.add(3, 4));
    Viper.Terminal.SayBool(m.isPositive(5));
}
)";
    CompilerInput input{.source = source, .path = "arrowret.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test colon return type syntax (Bug #43 fix).
TEST(ZiaEntities, ColonReturnType)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Math {
    expose func add(Integer a, Integer b): Integer {
        return a + b;
    }

    expose func isPositive(Integer n): Boolean {
        return n > 0;
    }
}

func start() {
    var m = new Math();
    Viper.Terminal.SayInt(m.add(3, 4));
    Viper.Terminal.SayBool(m.isPositive(5));
}
)";
    CompilerInput input{.source = source, .path = "colonret.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
