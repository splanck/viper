# Events, Async, And Stateful Results Plan

## Goal

Make asynchronous operations, event delivery, and query results composable.
Public APIs should return explicit results or event objects instead of requiring
callers to read mutable "last" state after each operation.

## Problems

Several APIs currently expose operation results through side effects:

- pathfinding stores `LastFound` and `LastSteps`.
- quadtree queries expose `ResultCount` and indexed `GetResult`.
- animations expose event counts and indexed fired-event getters.
- UI table state exposes `LastHeaderClick`.
- REST clients expose `LastStatus`, `LastResponse`, and `LastOk`.
- terrain/navmesh APIs expose last telemetry or last path cost.
- asset loading exposes process/global last-load diagnostics.

This style is simple for demos but fragile in production code because results
can be overwritten before they are consumed.

## Target Patterns

### Operation result object

Use when one call produces a structured result:

```text
Pathfinder.FindPath(...) -> PathResult
Quadtree.Query(rect) -> QueryResult<Entity>
SceneAsset.Load(path) -> Result<SceneAsset>
RestClient.Send(request) -> Result<HttpResponse>
```

### Event batch

Use when polling produces zero or more events:

```text
AnimationController.PollEvents() -> AnimationEventBatch
Input.PollEvents() -> EventBatch<InputEvent>
Window.PollEvents() -> EventBatch<WindowEvent>
```

### Future or task result

Use when an async operation completes later:

```text
AsyncSocket.Send(data) -> Future<Result<SendReceipt>>
SemanticJob.Start(...) -> Future<Result<SemanticOutput>>
```

The future's `TryGet` should return `Option<Result<T>>`, not raw object/null.

### Stream or subscription

Use when consumers subscribe to ongoing events:

```text
Watcher.Events() -> EventStream<FileEvent>
MessageBus.Subscribe(topic, callback) -> Subscription
```

Subscriptions need explicit lifetime and callback-thread rules.

## Callback Contract Metadata

Every callback-taking API should document and expose metadata for:

- callback signature.
- invocation thread.
- whether callbacks can reenter the runtime.
- lifetime of callback and captured objects.
- cancellation/unsubscribe method.
- behavior after receiver is closed or destroyed.
- ordering guarantees.
- error propagation from callback traps.

This applies to:

- message buses.
- GUI event handlers.
- HTTP server handlers.
- schedulers and timers.
- file watchers.
- async sockets and language-service jobs.
- game loops and animation events.

## Stateful Result Migration

### Pathfinder

Current style:

```text
FindPath(...)
LastFound()
LastSteps()
```

Target:

```text
FindPath(...) -> PathResult
PathResult.Found -> i1
PathResult.Steps -> seq<Point>
PathResult.Cost -> f64
PathResult.Diagnostics -> Option<PathDiagnostics>
```

### Quadtree

Current style:

```text
Query(...)
ResultCount()
GetResult(index)
```

Target:

```text
Query(...) -> QueryResult
QueryResult.Count -> i64
QueryResult.Items -> seq<obj>
```

### Animation

Current style:

```text
EventsFiredCount()
EventFiredId(index)
```

Target:

```text
PollEvents() -> AnimationEventBatch
AnimationEventBatch.Events -> seq<AnimationEvent>
```

### HTTP/REST

Current style:

```text
RestClient.Get(...)
RestClient.LastStatus
RestClient.LastResponse
RestClient.LastOk
```

Target:

```text
RestClient.Get(...) -> Result<HttpResponse>
```

### Asset loading

Current style:

```text
SceneAsset.Load(path) -> obj or null
AssetDiagnostics3D.LastLoadError
```

Target:

```text
SceneAsset.Load(path) -> Result<SceneAsset>
```

## Async Naming Policy

- `Start` starts a long-running operation and returns a job/task handle.
- `Cancel` requests cancellation and returns whether the request was accepted.
- `Wait` blocks until completion and returns a result or traps on timeout,
  depending on name.
- `WaitFor` must declare units or accept `Duration`.
- `TryGet` on a future returns `Option<Result<T>>`.
- `Get` on a completed future returns `Result<T>` or traps if incomplete,
  depending on docs.

## Documentation Updates

Docs should teach:

- result objects as values users can store, pass, and inspect.
- event batches for polling loops.
- subscriptions for callbacks.
- callback lifetime and unsubscribe requirements.
- main-thread restrictions for GUI and graphics callbacks.
- how async results interact with `Option` and `Result`.

## Example Migration

Examples and demos should be updated in this order:

1. `examples/apiaudit/**` for focused API usage.
2. small `examples/zia/**` and `examples/vbasic/**` snippets.
3. `tests/runtime/demo_*.zia` 3D rendering demos.
4. larger `examples/apps/**` and `examples/games/**`.
5. presentation/video demos.

Large examples should avoid polling side-channel state when a result object is
available, even if the old API remains as compatibility.

## Audit Rules

Warn or fail on:

- `Last*` properties that are the only way to read an operation result.
- `ResultCount` plus indexed getter patterns when the operation can return a
  collection result.
- callback APIs without invocation-thread metadata.
- async APIs returning raw `obj` instead of `Future<Result<T>>`,
  `Future<Option<T>>`, or typed operation handles.
- docs that teach "call X, then inspect LastY" as the modern style.

## Acceptance Criteria

- Async and query APIs compose without hidden mutable state.
- Callbacks have explicit lifetime and threading contracts.
- Examples store and inspect returned results instead of relying on last-state.
- Language tooling can surface async/callback contracts from catalog metadata.
