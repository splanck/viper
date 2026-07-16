---
status: active
audience: public
last-verified: 2026-07-16
---

# Appendix C: Runtime Library Reference

The Viper runtime surface is generated from the live runtime registry. For exact signatures, use the tool instead of this prose appendix:

```bash
viper --dump-runtime-api
```

That command prints JSON for every runtime class, method, property, and signature available in the build you are using. It is the authoritative reference for code generation, documentation checks, and agent workflows.

For human-oriented documentation, see the [Runtime Library Index](../../viperlib/README.md) and the topic pages under `docs/viperlib/`, plus the [generated API reference](../../generated/runtime/README.md).

---

## Common Modules

- `Viper.Terminal`: console input and output.
- `Viper.IO.File`: whole-file text and byte IO.
- `Viper.Text.Fmt`: integer, boolean, and numeric formatting helpers.
- `Viper.Text.Json`: JSON parsing, validation, object helpers, and formatting.
- `Viper.Text.Csv`: CSV line/table parsing and formatting.
- `Viper.Math`: scalar math, bit helpers, random numbers, vectors, matrices, quaternions, splines, and easing.
- `Viper.Collections.Seq`, `Map`, `Set`, `Bytes`: runtime collection classes.
- `Viper.Network.Http`, `HttpReq`, `HttpRes`, `Tcp`, `Udp`, `WebSocket`: network APIs.
- `Viper.Threads.Thread`, `Pool`, `Promise`, `Future`, `Async`, `Channel`, `SafeI64`: concurrency APIs.
- `Viper.Graphics`, `Viper.GUI`, `Viper.Input`: application-facing UI, drawing, and input APIs.
- `Viper.System.Environment`, `Viper.System.Exec`: process and environment helpers.

Some older examples mention a `Viper.Test` module or Zia `test`/`assert` syntax. Those are not present in the current runtime registry or language grammar; prefer the live dump and the checked examples in the main chapters.

---

## Checked Zia Example

```rust
bind Json = Viper.Text.Json;
bind Fmt = Viper.Text.Fmt;
bind Viper.Terminal as Terminal;

func start() {
    var player = Json.NewObject();
    Json.SetStr(player, "name", "Hero");
    Json.SetInt(player, "score", 100);
    Json.SetBool(player, "active", true);

    var encoded = Json.Format(player);
    var decoded = Json.Parse(encoded);

    Terminal.Say(Json.GetStr(decoded, "name"));
    Terminal.Say(Fmt.Int(Json.GetInt(decoded, "score")));
    Terminal.Say(Fmt.Bool(Json.GetBool(decoded, "active")));
}
```

---

## Agent-Facing Commands

Use these commands when verifying examples or generating current reference data:

```bash
viper check path/to/file.zia --diagnostic-format=json
viper run path/to/file.zia --diagnostic-format=json
viper --dump-runtime-api
viper --dump-opcodes
viper explain V-ZIA-UNDEFINED --json
```

`viper run` currently accepts `.zia`, `.bas`, project directories, and `viper.project` targets. IL snippets in this book are illustrative unless they are wrapped in a runnable frontend example.
