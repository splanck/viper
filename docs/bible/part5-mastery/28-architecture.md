# Chapter 28: Software Architecture

Small programs are easy. Write code, it works, done. But as programs grow, complexity explodes. Thousands of lines. Dozens of files. Multiple developers. Features piling on features.

Without good architecture, large programs become unmaintainable tangles of spaghetti code.

This chapter teaches you to organize large systems.

---

## What Is Architecture?

Architecture is the structure of your system — how pieces fit together.

**Good architecture:**
- Easy to understand
- Easy to change
- Easy to test
- Separates concerns

**Bad architecture:**
- Everything depends on everything
- Changes ripple everywhere
- Testing requires the whole system
- "I'm afraid to touch that code"

---

## Principles of Good Architecture

### Separation of Concerns

Each piece does one thing:

```rust
// Bad: Everything mixed together
entity UserManager {
    func registerUser(email: string, password: string) {
        // Validate email format
        if !email.contains("@") {
            showError("Invalid email");
            return;
        }

        // Hash password
        var hash = sha256(password);

        // Save to database
        var db = Database.connect("localhost:5432");
        db.query("INSERT INTO users...");

        // Send welcome email
        var smtp = SMTP.connect("mail.server.com");
        smtp.send(email, "Welcome!", "...");

        // Update UI
        userList.refresh();
        showSuccess("User created!");
    }
}
```

```rust
// Good: Separate concerns
entity UserValidator {
    func validateEmail(email: string) -> bool { ... }
    func validatePassword(password: string) -> bool { ... }
}

entity PasswordHasher {
    func hash(password: string) -> string { ... }
}

entity UserRepository {
    func save(user: User) { ... }
}

entity EmailService {
    func sendWelcome(email: string) { ... }
}

entity UserController {
    hide validator: UserValidator;
    hide hasher: PasswordHasher;
    hide repository: UserRepository;
    hide email: EmailService;

    func registerUser(email: string, password: string) -> Result {
        if !self.validator.validateEmail(email) {
            return Result.error("Invalid email");
        }

        var hash = self.hasher.hash(password);
        var user = User(email, hash);

        self.repository.save(user);
        self.email.sendWelcome(email);

        return Result.success();
    }
}
```

Each class has one responsibility. Easy to test, easy to change.

### Dependency Inversion

Depend on abstractions, not concrete implementations:

```rust
// Bad: Hard dependency
entity OrderProcessor {
    hide database: MySQLDatabase;  // Tied to MySQL!

    expose func init() {
        self.database = MySQLDatabase.connect(...);
    }
}

// Good: Depend on interface
interface IDatabase {
    func save(data: any);
    func find(id: string) -> any?;
}

entity OrderProcessor {
    hide database: IDatabase;

    expose func init(database: IDatabase) {  // Injected
        self.database = database;
    }
}

// Now you can use any database:
var processor1 = OrderProcessor(MySQLDatabase(...));
var processor2 = OrderProcessor(PostgresDatabase(...));
var processor3 = OrderProcessor(MockDatabase());  // For tests!
```

### Single Responsibility

Each module/class/function should have one reason to change:

```rust
// Bad: Multiple responsibilities
entity Report {
    func calculate() { ... }
    func format() -> string { ... }
    func print() { ... }
    func saveToFile() { ... }
    func sendEmail() { ... }
}

// Good: One responsibility each
entity ReportCalculator {
    func calculate(data: Data) -> Report { ... }
}

entity ReportFormatter {
    func format(report: Report) -> string { ... }
}

entity ReportPrinter {
    func print(formatted: string) { ... }
}
```

---

## Layered Architecture

Organize code into layers:

```
┌─────────────────────────────────────┐
│          Presentation Layer         │ ← UI, CLI, API endpoints
├─────────────────────────────────────┤
│          Application Layer          │ ← Use cases, workflows
├─────────────────────────────────────┤
│            Domain Layer             │ ← Business logic, entities
├─────────────────────────────────────┤
│         Infrastructure Layer        │ ← Database, files, network
└─────────────────────────────────────┘
```

**Rules:**
- Upper layers can use lower layers
- Lower layers never use upper layers
- Each layer has a clear purpose

### Example: E-commerce System

```
project/
├── presentation/
│   ├── web/
│   │   ├── CartController.viper
│   │   ├── ProductController.viper
│   │   └── OrderController.viper
│   └── api/
│       └── RestApi.viper
│
├── application/
│   ├── PlaceOrderUseCase.viper
│   ├── AddToCartUseCase.viper
│   └── SearchProductsUseCase.viper
│
├── domain/
│   ├── entities/
│   │   ├── Product.viper
│   │   ├── Cart.viper
│   │   └── Order.viper
│   ├── services/
│   │   ├── PricingService.viper
│   │   └── InventoryService.viper
│   └── repositories/
│       ├── IProductRepository.viper
│       └── IOrderRepository.viper
│
└── infrastructure/
    ├── database/
    │   ├── ProductRepository.viper
    │   └── OrderRepository.viper
    ├── payment/
    │   └── StripePaymentGateway.viper
    └── email/
        └── SendGridEmailService.viper
```

---

## Modules and Packages

Break large systems into modules:

```rust
// auth/authentication.viper
module Auth;

export entity Authenticator { ... }
export func hashPassword(pw: string) -> string { ... }

// Private to this module
func internalHelper() { ... }
```

```rust
// orders/order_service.viper
module Orders;

import Auth;  // Use the auth module

export entity OrderService {
    hide auth: Auth.Authenticator;

    func placeOrder(userId: string, items: [Item]) {
        if !self.auth.isAuthenticated(userId) {
            throw NotAuthenticatedError();
        }
        // ...
    }
}
```

### Module Guidelines

1. **High cohesion**: Related things together
2. **Low coupling**: Minimal dependencies between modules
3. **Clear interfaces**: Export only what's needed
4. **No circular dependencies**: A → B → C, not A → B → A

---

## Common Architectural Patterns

### Model-View-Controller (MVC)

Classic pattern for user interfaces:

```
       ┌──────────┐
       │   View   │ ← Displays data to user
       └────┬─────┘
            │ updates
       ┌────▼─────┐
       │Controller│ ← Handles user input
       └────┬─────┘
            │ modifies
       ┌────▼─────┐
       │  Model   │ ← Business logic and data
       └──────────┘
```

```rust
// Model
entity TodoList {
    hide items: [TodoItem];

    func addItem(text: string) { ... }
    func removeItem(id: i64) { ... }
    func getItems() -> [TodoItem] { ... }
}

// View
entity TodoView {
    hide list: HTMLElement;

    func render(items: [TodoItem]) {
        self.list.clear();
        for item in items {
            self.list.append(self.renderItem(item));
        }
    }

    func onAddClick(callback: func(string)) { ... }
    func onDeleteClick(callback: func(i64)) { ... }
}

// Controller
entity TodoController {
    hide model: TodoList;
    hide view: TodoView;

    expose func init(model: TodoList, view: TodoView) {
        self.model = model;
        self.view = view;

        self.view.onAddClick(self.handleAdd);
        self.view.onDeleteClick(self.handleDelete);

        self.updateView();
    }

    func handleAdd(text: string) {
        self.model.addItem(text);
        self.updateView();
    }

    func handleDelete(id: i64) {
        self.model.removeItem(id);
        self.updateView();
    }

    func updateView() {
        self.view.render(self.model.getItems());
    }
}
```

### Repository Pattern

Abstract data access:

```rust
interface IUserRepository {
    func findById(id: string) -> User?;
    func findByEmail(email: string) -> User?;
    func save(user: User);
    func delete(id: string);
    func findAll() -> [User];
}

// Implementation for database
entity DatabaseUserRepository implements IUserRepository {
    hide db: Database;

    func findById(id: string) -> User? {
        var row = self.db.query("SELECT * FROM users WHERE id = ?", [id]);
        return row ? User.fromRow(row) : null;
    }

    // ... other methods
}

// Implementation for testing
entity InMemoryUserRepository implements IUserRepository {
    hide users: Map<string, User>;

    func findById(id: string) -> User? {
        return self.users.get(id);
    }

    // ... other methods
}
```

### Service Layer

Encapsulate business logic:

```rust
entity OrderService {
    hide orderRepo: IOrderRepository;
    hide productRepo: IProductRepository;
    hide paymentGateway: IPaymentGateway;
    hide emailService: IEmailService;

    func placeOrder(userId: string, items: [CartItem]) -> Order {
        // Validate stock
        for item in items {
            var product = self.productRepo.findById(item.productId);
            if product.stock < item.quantity {
                throw InsufficientStockError(product.name);
            }
        }

        // Calculate total
        var total = self.calculateTotal(items);

        // Process payment
        var payment = self.paymentGateway.charge(userId, total);
        if !payment.success {
            throw PaymentFailedError(payment.error);
        }

        // Create order
        var order = Order(userId, items, total, payment.id);
        self.orderRepo.save(order);

        // Update stock
        for item in items {
            self.productRepo.decrementStock(item.productId, item.quantity);
        }

        // Send confirmation
        self.emailService.sendOrderConfirmation(userId, order);

        return order;
    }
}
```

### Event-Driven Architecture

Decouple components with events:

```rust
entity EventBus {
    hide listeners: Map<string, [func(Event)]>;

    func subscribe(eventType: string, handler: func(Event)) {
        if !self.listeners.has(eventType) {
            self.listeners.set(eventType, []);
        }
        self.listeners.get(eventType).push(handler);
    }

    func publish(event: Event) {
        var handlers = self.listeners.get(event.type);
        if handlers != null {
            for handler in handlers {
                handler(event);
            }
        }
    }
}

// Usage
var bus = EventBus();

// Components subscribe to events they care about
bus.subscribe("order_placed", func(e: Event) {
    inventoryService.updateStock(e.data);
});

bus.subscribe("order_placed", func(e: Event) {
    emailService.sendConfirmation(e.data);
});

bus.subscribe("order_placed", func(e: Event) {
    analyticsService.trackOrder(e.data);
});

// Order service just publishes the event
entity OrderService {
    func placeOrder(order: Order) {
        // ... create order ...
        bus.publish(Event("order_placed", order));
    }
}
```

Components don't know about each other — loose coupling!

---

## Dependency Injection

Pass dependencies in rather than creating them:

```rust
// Bad: Creates own dependencies
entity UserService {
    hide db: Database;
    hide emailer: EmailService;

    expose func init() {
        self.db = MySQLDatabase("localhost:3306");  // Hard-coded!
        self.emailer = SMTPEmailService("mail.com");  // Hard-coded!
    }
}

// Good: Dependencies injected
entity UserService {
    hide db: IDatabase;
    hide emailer: IEmailService;

    expose func init(db: IDatabase, emailer: IEmailService) {
        self.db = db;
        self.emailer = emailer;
    }
}

// Composition root wires everything together
func main() {
    var db = MySQLDatabase(Config.dbUrl);
    var emailer = SMTPEmailService(Config.smtpUrl);
    var userService = UserService(db, emailer);
    // ...
}
```

Benefits:
- Easy to test (inject mocks)
- Easy to change implementations
- Dependencies are explicit

---

## Configuration Management

Keep configuration separate from code:

```rust
// config.viper
module Config;

import Viper.Environment;

export value AppConfig {
    databaseUrl: string;
    redisUrl: string;
    smtpHost: string;
    smtpPort: i64;
    debugMode: bool;
}

export func load() -> AppConfig {
    return AppConfig {
        databaseUrl: Environment.get("DATABASE_URL", "localhost:5432"),
        redisUrl: Environment.get("REDIS_URL", "localhost:6379"),
        smtpHost: Environment.get("SMTP_HOST", "localhost"),
        smtpPort: Environment.getInt("SMTP_PORT", 25),
        debugMode: Environment.getBool("DEBUG", false)
    };
}
```

Usage:
```rust
var config = Config.load();
var db = Database.connect(config.databaseUrl);
```

---

## Error Handling Strategy

Consistent error handling across the system:

```rust
// Define application errors
entity AppError extends Error {
    code: string;
    httpStatus: i64;

    expose func init(message: string, code: string, httpStatus: i64) {
        super(message);
        self.code = code;
        self.httpStatus = httpStatus;
    }
}

entity NotFoundError extends AppError {
    expose func init(resource: string, id: string) {
        super(resource + " not found: " + id, "NOT_FOUND", 404);
    }
}

entity ValidationError extends AppError {
    expose func init(message: string) {
        super(message, "VALIDATION_ERROR", 400);
    }
}

entity UnauthorizedError extends AppError {
    expose func init() {
        super("Authentication required", "UNAUTHORIZED", 401);
    }
}

// Central error handler
entity ErrorHandler {
    func handle(error: Error) -> Response {
        if error is AppError {
            return Response(error.httpStatus, {
                "error": error.code,
                "message": error.message
            });
        }

        // Unknown error — log and return generic message
        Logger.error("Unhandled error", error);
        return Response(500, {
            "error": "INTERNAL_ERROR",
            "message": "An unexpected error occurred"
        });
    }
}
```

---

## A Complete Example: Task Management System

```
task-manager/
├── main.viper
│
├── config/
│   └── config.viper
│
├── domain/
│   ├── entities/
│   │   ├── Task.viper
│   │   ├── User.viper
│   │   └── Project.viper
│   ├── services/
│   │   └── TaskAssignmentService.viper
│   └── repositories/
│       ├── ITaskRepository.viper
│       └── IUserRepository.viper
│
├── application/
│   ├── CreateTaskUseCase.viper
│   ├── AssignTaskUseCase.viper
│   ├── CompleteTaskUseCase.viper
│   └── ListTasksUseCase.viper
│
├── infrastructure/
│   ├── database/
│   │   ├── TaskRepository.viper
│   │   └── UserRepository.viper
│   └── notifications/
│       └── EmailNotificationService.viper
│
├── presentation/
│   ├── cli/
│   │   └── TaskCLI.viper
│   └── api/
│       ├── TaskController.viper
│       └── Router.viper
│
└── tests/
    ├── domain/
    │   └── TaskTest.viper
    ├── application/
    │   └── CreateTaskUseCaseTest.viper
    └── integration/
        └── TaskApiTest.viper
```

### Sample Files

**domain/entities/Task.viper:**
```rust
module Domain.Entities;

export enum TaskStatus {
    PENDING,
    IN_PROGRESS,
    COMPLETED
}

export entity Task {
    id: string;
    title: string;
    description: string;
    status: TaskStatus;
    assigneeId: string?;
    projectId: string;
    createdAt: DateTime;
    completedAt: DateTime?;

    expose func init(title: string, description: string, projectId: string) {
        self.id = UUID.generate();
        self.title = title;
        self.description = description;
        self.status = TaskStatus.PENDING;
        self.assigneeId = null;
        self.projectId = projectId;
        self.createdAt = DateTime.now();
        self.completedAt = null;
    }

    func assign(userId: string) {
        self.assigneeId = userId;
        self.status = TaskStatus.IN_PROGRESS;
    }

    func complete() {
        if self.status == TaskStatus.COMPLETED {
            throw InvalidStateError("Task already completed");
        }
        self.status = TaskStatus.COMPLETED;
        self.completedAt = DateTime.now();
    }

    func isOverdue(deadline: DateTime) -> bool {
        return self.status != TaskStatus.COMPLETED &&
               DateTime.now() > deadline;
    }
}
```

**application/CreateTaskUseCase.viper:**
```rust
module Application;

import Domain.Entities.Task;
import Domain.Repositories.ITaskRepository;
import Domain.Repositories.IProjectRepository;

export entity CreateTaskUseCase {
    hide taskRepo: ITaskRepository;
    hide projectRepo: IProjectRepository;

    expose func init(taskRepo: ITaskRepository, projectRepo: IProjectRepository) {
        self.taskRepo = taskRepo;
        self.projectRepo = projectRepo;
    }

    func execute(request: CreateTaskRequest) -> Task {
        // Validate project exists
        var project = self.projectRepo.findById(request.projectId);
        if project == null {
            throw NotFoundError("Project", request.projectId);
        }

        // Validate title
        if request.title.trim().isEmpty() {
            throw ValidationError("Task title cannot be empty");
        }

        // Create and save task
        var task = Task(request.title, request.description, request.projectId);
        self.taskRepo.save(task);

        return task;
    }
}

export value CreateTaskRequest {
    title: string;
    description: string;
    projectId: string;
}
```

**presentation/api/TaskController.viper:**
```rust
module Presentation.Api;

import Application.CreateTaskUseCase;
import Application.ListTasksUseCase;

export entity TaskController {
    hide createTask: CreateTaskUseCase;
    hide listTasks: ListTasksUseCase;

    expose func init(createTask: CreateTaskUseCase, listTasks: ListTasksUseCase) {
        self.createTask = createTask;
        self.listTasks = listTasks;
    }

    func handleCreate(request: HttpRequest) -> HttpResponse {
        try {
            var body = request.json();
            var task = self.createTask.execute(CreateTaskRequest {
                title: body["title"],
                description: body["description"],
                projectId: body["projectId"]
            });

            return HttpResponse.created(task.toJSON());

        } catch ValidationError as e {
            return HttpResponse.badRequest({"error": e.message});
        } catch NotFoundError as e {
            return HttpResponse.notFound({"error": e.message});
        }
    }

    func handleList(request: HttpRequest) -> HttpResponse {
        var projectId = request.query.get("projectId");
        var tasks = self.listTasks.execute(projectId);
        return HttpResponse.ok(tasks.map(t => t.toJSON()));
    }
}
```

---

## Summary

Good architecture:
- **Separates concerns**: Each piece does one thing
- **Depends on abstractions**: Easy to swap implementations
- **Uses layers**: Clear hierarchy of responsibilities
- **Minimizes coupling**: Changes don't ripple everywhere
- **Enables testing**: Components work in isolation

Key patterns:
- **Layered architecture**: Presentation → Application → Domain → Infrastructure
- **MVC**: Separate UI, logic, and control
- **Repository**: Abstract data access
- **Dependency injection**: Pass dependencies in
- **Event-driven**: Decouple with events

Architecture isn't about following rules — it's about making systems maintainable as they grow.

---

## Exercises

**Exercise 28.1**: Refactor a monolithic script into layers (presentation, application, domain, infrastructure).

**Exercise 28.2**: Implement the Repository pattern for a simple entity. Create both database and in-memory implementations.

**Exercise 28.3**: Build a simple event bus and use it to decouple a notification system from order processing.

**Exercise 28.4**: Design the architecture for a blog system with posts, comments, users, and categories.

**Exercise 28.5**: Add dependency injection to an existing project. Create interfaces for external dependencies.

**Exercise 28.6** (Challenge): Design and implement a plugin system where functionality can be added without modifying core code.

---

*Congratulations! You've completed the main chapters of the Viper Bible. You've learned programming from the ground up — from your first "Hello, World!" to architecting complex systems.*

*The appendices provide quick references for the languages and runtime. Use them as you continue your journey.*

*[Continue to Appendix A: ViperLang Reference →](../appendices/a-viperlang-reference.md)*
