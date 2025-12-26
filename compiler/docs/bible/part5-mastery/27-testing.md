# Chapter 27: Testing

You write code. It works — or does it? You run it a few times, try some inputs, looks good. Ship it.

Then users find bugs. Edge cases you never considered. Inputs you never tried. Combinations that break everything.

Testing prevents this. This chapter teaches you to test your code systematically.

---

## Why Test?

**Confidence**: Know your code works, not hope it works.

**Safety**: Change code without fear of breaking things.

**Documentation**: Tests show how code should be used.

**Design**: Testable code is usually better designed.

A program without tests is a program with hidden bugs waiting to surprise you.

---

## Your First Test

```viper
import Viper.Test;

func add(a: i64, b: i64) -> i64 {
    return a + b;
}

test "add returns sum of two numbers" {
    assert add(2, 3) == 5;
    assert add(0, 0) == 0;
    assert add(-1, 1) == 0;
}
```

Run tests:
```bash
viper test myprogram.viper
```

Output:
```
Running tests...
✓ add returns sum of two numbers
1 test passed, 0 failed
```

---

## Assertions

Assertions are statements that must be true:

```viper
test "basic assertions" {
    // Equality
    assert 1 + 1 == 2;
    assert "hello" == "hello";

    // Inequality
    assert 5 != 3;

    // Comparisons
    assert 10 > 5;
    assert 3 <= 3;

    // Boolean
    assert true;
    assert !false;

    // Null checks
    let x: string? = "hello";
    assert x != null;
}
```

### Assertions with Messages

When assertions fail, messages help:

```viper
test "with messages" {
    let result = calculateTax(100);
    assert result == 10, "Expected tax of 10%, got " + result;
}
```

### Common Assert Functions

```viper
import Viper.Test;

test "assert variants" {
    // Basic
    assert condition;
    assert condition, "message";

    // Equality (better error messages)
    assertEqual(actual, expected);
    assertNotEqual(a, b);

    // Floating point (with tolerance)
    assertClose(3.14159, 3.14, 0.01);

    // Null
    assertNull(value);
    assertNotNull(value);

    // Exceptions
    assertThrows(func() {
        riskyOperation();
    });

    // Collections
    assertContains(list, item);
    assertEmpty(list);
    assertLength(list, 5);
}
```

---

## Test Structure: Arrange-Act-Assert

Good tests follow a pattern:

```viper
test "user can update email" {
    // Arrange: Set up test data
    let user = User("alice", "alice@old.com");

    // Act: Perform the action
    user.updateEmail("alice@new.com");

    // Assert: Check the result
    assert user.email == "alice@new.com";
}
```

Also called "Given-When-Then":
- **Given** a user with an old email
- **When** they update their email
- **Then** the email should change

---

## Testing Different Cases

### Normal Cases

Test typical usage:

```viper
test "divide normal cases" {
    assert divide(10, 2) == 5;
    assert divide(9, 3) == 3;
    assert divide(7, 2) == 3;  // Integer division
}
```

### Edge Cases

Test boundaries and unusual inputs:

```viper
test "divide edge cases" {
    assert divide(0, 5) == 0;      // Zero numerator
    assert divide(5, 1) == 5;      // Divide by one
    assert divide(-10, 2) == -5;   // Negative numbers
    assert divide(10, -2) == -5;
    assert divide(-10, -2) == 5;   // Both negative
}
```

### Error Cases

Test that errors are handled correctly:

```viper
test "divide by zero throws" {
    assertThrows(func() {
        divide(5, 0);
    });
}
```

---

## Testing Classes

```viper
class Stack<T> {
    private items: [T];

    constructor() {
        self.items = [];
    }

    func push(item: T) {
        self.items.push(item);
    }

    func pop() -> T? {
        if self.items.length == 0 {
            return null;
        }
        return self.items.pop();
    }

    func peek() -> T? {
        if self.items.length == 0 {
            return null;
        }
        return self.items[self.items.length - 1];
    }

    func isEmpty() -> bool {
        return self.items.length == 0;
    }
}

test "stack is empty when created" {
    let stack = Stack<i64>();
    assert stack.isEmpty();
}

test "stack push adds items" {
    let stack = Stack<i64>();
    stack.push(1);
    assert !stack.isEmpty();
    assert stack.peek() == 1;
}

test "stack pop removes items in LIFO order" {
    let stack = Stack<i64>();
    stack.push(1);
    stack.push(2);
    stack.push(3);

    assert stack.pop() == 3;
    assert stack.pop() == 2;
    assert stack.pop() == 1;
    assert stack.isEmpty();
}

test "stack pop returns null when empty" {
    let stack = Stack<i64>();
    assert stack.pop() == null;
}
```

---

## Test Setup and Teardown

When tests need common setup:

```viper
import Viper.Test;

// Setup runs before each test
setup {
    testDatabase = Database.createTemporary();
    testDatabase.seed(testData);
}

// Teardown runs after each test
teardown {
    testDatabase.destroy();
}

test "can insert user" {
    let user = User("alice");
    testDatabase.insert(user);
    assert testDatabase.findUser("alice") != null;
}

test "can delete user" {
    let user = testDatabase.findUser("testuser");
    testDatabase.delete(user);
    assert testDatabase.findUser("testuser") == null;
}
```

---

## Test Doubles: Mocks and Stubs

When testing code that depends on external systems:

### Stubs: Provide Canned Responses

```viper
// Real implementation
class WeatherService {
    func getTemperature(city: string) -> f64 {
        let response = Http.get("https://api.weather.com/" + city);
        return parseTemperature(response);
    }
}

// Stub for testing
class StubWeatherService implements IWeatherService {
    private temperature: f64;

    constructor(temp: f64) {
        self.temperature = temp;
    }

    func getTemperature(city: string) -> f64 {
        return self.temperature;  // Always returns same value
    }
}

test "thermostat turns on heat when cold" {
    let weather = StubWeatherService(30.0);  // Stub says 30°F
    let thermostat = Thermostat(weather);

    thermostat.check();

    assert thermostat.heatingOn == true;
}
```

### Mocks: Verify Interactions

```viper
class MockEmailService implements IEmailService {
    sentEmails: [Email];

    constructor() {
        self.sentEmails = [];
    }

    func send(email: Email) {
        self.sentEmails.push(email);
    }

    func verifySent(to: string, subject: string) -> bool {
        for email in self.sentEmails {
            if email.to == to && email.subject == subject {
                return true;
            }
        }
        return false;
    }
}

test "registration sends welcome email" {
    let emailService = MockEmailService();
    let registration = RegistrationService(emailService);

    registration.register("alice@example.com", "password");

    assert emailService.verifySent("alice@example.com", "Welcome!");
}
```

---

## Testing Patterns

### One Assert Per Test (Usually)

```viper
// Okay: Related assertions
test "user creation" {
    let user = User("alice", "alice@example.com");
    assert user.name == "alice";
    assert user.email == "alice@example.com";
}

// Better: Separate tests for clarity
test "user has name" {
    let user = User("alice", "alice@example.com");
    assert user.name == "alice";
}

test "user has email" {
    let user = User("alice", "alice@example.com");
    assert user.email == "alice@example.com";
}
```

### Test Names Describe Behavior

```viper
// Bad: Vague
test "test1" { ... }
test "user test" { ... }

// Good: Descriptive
test "new user has zero balance" { ... }
test "deposit increases balance" { ... }
test "withdraw fails when insufficient funds" { ... }
```

### Keep Tests Independent

```viper
// Bad: Tests depend on each other
test "create user" {
    globalUser = User("alice");
}
test "use user" {
    // Fails if previous test didn't run!
    assert globalUser.name == "alice";
}

// Good: Each test is self-contained
test "create user" {
    let user = User("alice");
    assert user.name == "alice";
}
```

---

## Test-Driven Development (TDD)

Write tests before code:

1. **Red**: Write a failing test
2. **Green**: Write minimum code to pass
3. **Refactor**: Improve code while tests pass

```viper
// Step 1: Write failing test
test "isPrime returns true for prime numbers" {
    assert isPrime(2);
    assert isPrime(3);
    assert isPrime(5);
    assert isPrime(7);
}

// Step 2: Make it pass (simple implementation)
func isPrime(n: i64) -> bool {
    if n < 2 { return false; }
    for i in 2..n {
        if n % i == 0 {
            return false;
        }
    }
    return true;
}

// Step 3: Refactor (optimize)
func isPrime(n: i64) -> bool {
    if n < 2 { return false; }
    if n == 2 { return true; }
    if n % 2 == 0 { return false; }

    let limit = Viper.Math.sqrt(n).toInt() + 1;
    for i in 3..limit step 2 {
        if n % i == 0 {
            return false;
        }
    }
    return true;
}
```

---

## Integration Tests

Unit tests test pieces in isolation. Integration tests test pieces together:

```viper
// Unit test: Just the validator
test "email validator rejects invalid format" {
    assert !EmailValidator.isValid("notanemail");
}

// Integration test: Full registration flow
test "registration rejects invalid email" {
    let db = TestDatabase.create();
    let emailService = TestEmailService.create();
    let registration = RegistrationService(db, emailService);

    let result = registration.register("notanemail", "password123");

    assert result.success == false;
    assert result.error == "Invalid email format";
    assert db.userCount() == 0;
    assert emailService.emailsSent() == 0;
}
```

---

## Property-Based Testing

Instead of specific examples, test properties that should always hold:

```viper
import Viper.Test;

test "reversing twice returns original" {
    for i in 0..100 {
        let original = generateRandomString();
        let reversed = original.reverse().reverse();
        assert original == reversed;
    }
}

test "sorting is idempotent" {
    for i in 0..100 {
        let list = generateRandomList();
        let sortedOnce = sort(list);
        let sortedTwice = sort(sortedOnce);
        assertEqual(sortedOnce, sortedTwice);
    }
}

test "sorted list is in order" {
    for i in 0..100 {
        let list = generateRandomList();
        let sorted = sort(list);

        for j in 0..(sorted.length - 1) {
            assert sorted[j] <= sorted[j + 1];
        }
    }
}
```

---

## Code Coverage

Measure how much code your tests exercise:

```bash
viper test --coverage myprogram.viper
```

Output:
```
File                  Statements    Branches    Coverage
─────────────────────────────────────────────────────────
math.viper            45/50         12/15       87%
user.viper            30/32         8/10        91%
utils.viper           20/40         5/12        52%
─────────────────────────────────────────────────────────
Total                 95/122        25/37       78%
```

Aim for high coverage, but 100% isn't always necessary or sufficient. Some code is hard to test; some bugs exist in tested code.

---

## A Complete Example: Testing a Calculator

```viper
module Calculator;

import Viper.Test;

class Calculator {
    private history: [string];

    constructor() {
        self.history = [];
    }

    func add(a: f64, b: f64) -> f64 {
        let result = a + b;
        self.history.push(a + " + " + b + " = " + result);
        return result;
    }

    func subtract(a: f64, b: f64) -> f64 {
        let result = a - b;
        self.history.push(a + " - " + b + " = " + result);
        return result;
    }

    func multiply(a: f64, b: f64) -> f64 {
        let result = a * b;
        self.history.push(a + " * " + b + " = " + result);
        return result;
    }

    func divide(a: f64, b: f64) -> f64 {
        if b == 0 {
            throw DivisionByZeroError();
        }
        let result = a / b;
        self.history.push(a + " / " + b + " = " + result);
        return result;
    }

    func getHistory() -> [string] {
        return self.history.clone();
    }

    func clearHistory() {
        self.history = [];
    }
}

// Tests

test "add returns sum" {
    let calc = Calculator();
    assertClose(calc.add(2, 3), 5.0, 0.001);
}

test "add handles negatives" {
    let calc = Calculator();
    assertClose(calc.add(-2, 3), 1.0, 0.001);
    assertClose(calc.add(2, -3), -1.0, 0.001);
    assertClose(calc.add(-2, -3), -5.0, 0.001);
}

test "add handles decimals" {
    let calc = Calculator();
    assertClose(calc.add(0.1, 0.2), 0.3, 0.001);
}

test "subtract returns difference" {
    let calc = Calculator();
    assertClose(calc.subtract(5, 3), 2.0, 0.001);
}

test "multiply returns product" {
    let calc = Calculator();
    assertClose(calc.multiply(4, 3), 12.0, 0.001);
}

test "multiply by zero returns zero" {
    let calc = Calculator();
    assertClose(calc.multiply(5, 0), 0.0, 0.001);
}

test "divide returns quotient" {
    let calc = Calculator();
    assertClose(calc.divide(10, 2), 5.0, 0.001);
}

test "divide by zero throws" {
    let calc = Calculator();
    assertThrows(func() {
        calc.divide(5, 0);
    });
}

test "history records operations" {
    let calc = Calculator();
    calc.add(2, 3);
    calc.multiply(4, 5);

    let history = calc.getHistory();
    assertLength(history, 2);
    assertContains(history[0], "+");
    assertContains(history[1], "*");
}

test "clear history removes entries" {
    let calc = Calculator();
    calc.add(1, 1);
    calc.clearHistory();

    assertEmpty(calc.getHistory());
}
```

---

## The Three Languages

**ViperLang**
```viper
import Viper.Test;

test "example test" {
    assert 1 + 1 == 2;
    assertEqual(actual, expected);
    assertThrows(func() { riskyCode(); });
}
```

**BASIC**
```basic
TEST "example test"
    ASSERT 1 + 1 = 2
    ASSERT_EQUAL actual, expected
    ASSERT_THROWS RiskyCode
END TEST
```

**Pascal**
```pascal
uses ViperTest;

procedure TestExample;
begin
    Assert(1 + 1 = 2);
    AssertEqual(actual, expected);
    AssertThrows(@RiskyCode);
end;
```

---

## Common Mistakes

**Testing implementation, not behavior**
```viper
// Bad: Tests internal details
test "uses hashmap internally" {
    let cache = Cache();
    // Checking internal data structure — fragile!
}

// Good: Tests behavior
test "cache returns stored value" {
    let cache = Cache();
    cache.set("key", "value");
    assert cache.get("key") == "value";
}
```

**Flaky tests**
```viper
// Bad: Depends on timing
test "async operation completes" {
    startAsyncOperation();
    Time.sleep(100);  // Might not be enough!
    assert isComplete();
}

// Good: Wait properly
test "async operation completes" {
    let future = startAsyncOperation();
    let result = future.get(timeout: 5000);
    assert result.success;
}
```

**Too many assertions**
```viper
// Bad: What exactly failed?
test "everything about user" {
    let user = createUser();
    assert user.name == "alice";
    assert user.email != null;
    assert user.age > 0;
    assert user.createdAt != null;
    assert user.role == "member";
    // 20 more assertions...
}

// Good: Focused tests
test "new user has member role" {
    let user = createUser();
    assert user.role == "member";
}
```

---

## Summary

- **Tests verify behavior**: Catch bugs before users do
- **Arrange-Act-Assert**: Structure for clear tests
- **Test edge cases**: Boundaries, errors, unusual inputs
- **Use test doubles**: Stubs and mocks for dependencies
- **Keep tests independent**: No shared state between tests
- **TDD**: Write tests first for better design
- **Integration tests**: Test components together
- **Coverage**: Measure what's tested, aim high

Testing is an investment. The time you spend writing tests saves debugging time later.

---

## Exercises

**Exercise 27.1**: Write tests for a `StringUtils` class with `reverse`, `capitalize`, and `isPalindrome` methods.

**Exercise 27.2**: Create a `BankAccount` class with `deposit`, `withdraw`, and `transfer` methods. Write comprehensive tests including edge cases.

**Exercise 27.3**: Write tests for a sorting function. Use property-based testing to verify it always produces sorted output.

**Exercise 27.4**: Create a mock for a file system and use it to test a log rotation function without creating real files.

**Exercise 27.5**: Practice TDD: Write tests first for a `ShoppingCart` class, then implement the class.

**Exercise 27.6** (Challenge): Build a simple test framework that can run tests, report results, and measure coverage.

---

*Your code works and is tested. Now let's think bigger — how do you organize a large system? Next chapter: Architecture.*

*[Continue to Chapter 28: Architecture →](28-architecture.md)*
