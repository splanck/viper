# Viper

Viper is an intermediate language (IL) based compiler toolchain. Source frontends such as a BASIC parser lower programs into Viper IL. Programs can then be executed directly by the virtual machine or compiled to native code by backends.

## How It Works

1. **Frontends** translate source languages into the SSA-style Viper IL.
2. The **IL** provides a stable, typed representation of the program.
3. The **virtual machine** interprets IL, while **codegen** components can emit machine code.

## Getting Started

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

After building, the tool binaries reside under `build/src/tools`. Utilities such as
`build/src/tools/ilc/ilc`, `build/src/tools/il-dis/il-dis`, and
`build/src/tools/il-verify/il-verify` can compile, run, and inspect IL programs.

## Example

### BASIC

```basic
10 LET X = 2 + 3
20 LET Y = X * 2
30 PRINT "HELLO"
40 PRINT "READY"
50 PRINT Y
60 IF Y > 8 THEN PRINT Y ELSE PRINT 4
70 END
```

### IL

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void

global const str @.L0 = "HELLO"
global const str @.L1 = "READY"
global const str @.L2 = "\n"

func @main() -> i32 {
entry:
  %x_slot = alloca 8
  %y_slot = alloca 8

  %t0 = add 2, 3
  store i64, %x_slot, %t0

  %xv = load i64, %x_slot
  %t1 = mul %xv, 2
  store i64, %y_slot, %t1

  %s0 = const_str @.L0
  call @rt_print_str(%s0)
  %nl0 = const_str @.L2
  call @rt_print_str(%nl0)

  %s1 = const_str @.L1
  call @rt_print_str(%s1)
  %nl1 = const_str @.L2
  call @rt_print_str(%nl1)

  %yv0 = load i64, %y_slot
  call @rt_print_i64(%yv0)
  %nl2 = const_str @.L2
  call @rt_print_str(%nl2)

  %yv1 = load i64, %y_slot
  %cond = scmp_gt %yv1, 8
  cbr %cond, label then80, label else60

else60:
  call @rt_print_i64(4)
  %nl3 = const_str @.L2
  call @rt_print_str(%nl3)
  br label after

then80:
  %yv2 = load i64, %y_slot
  call @rt_print_i64(%yv2)
  %nl4 = const_str @.L2
  call @rt_print_str(%nl4)
  br label after

after:
  ret 0
}
```

These examples are available in `examples/basic/ex1_hello_cond.bas` and `examples/il/ex1_hello_cond.il`.

### Running the Example

Run the BASIC source directly through the front end:

```bash
build/src/tools/ilc/ilc front basic -run examples/basic/ex1_hello_cond.bas
```

To execute IL with the interpreter:

```bash
build/src/tools/ilc/ilc -run examples/il/ex1_hello_cond.il
```

## References

- **IL Quickstart**: [docs/il-quickstart.md](docs/il-quickstart.md)
- [BASIC Reference](docs/basic-reference.md)
- [IL Reference](docs/il-reference.md)

## Contributing

We’re glad you’re interested in Viper! This project is evolving and we’re experimenting rapidly. You’re welcome to explore the code, file issues, and propose small fixes or documentation improvements. However, we’re **not currently seeking external contributors** or large feature PRs.

## License

This project is licensed under the **MIT License**. See [LICENSE](LICENSE) for details.
