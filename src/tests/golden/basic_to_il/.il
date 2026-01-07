error: specify exactly one of -emit-il or -run
ilc v0.1.1-dev
Usage: ilc -run <file.il> [--trace=il|src] [--stdin-from <file>] [--max-steps N] [--break label|file:line]* [--break-src file:line]* [--watch name]* [--bounds-checks] [--count] [--time] [--dump-trap]
       ilc front basic -emit-il <file.bas> [--bounds-checks]
       ilc front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>] [--max-steps N] [--bounds-checks] [--dump-trap]
       ilc codegen x64 -S <in.il> [-o <exe>] [--run-native]
       ilc il-opt <in.il> -o <out.il> [--passes p1,p2] [-print-before] [-print-after] [-verify-each]

IL notes:
  IL modules executed with -run must define func @main().

BASIC notes:
  FUNCTION must RETURN a value on all paths.
  SUB cannot be used as an expression.
  Array parameters are ByRef; pass the array variable, not an index.
  Intrinsics: LEFT$ RIGHT$ MID$ INSTR LEN LTRIM$ RTRIM$ TRIM$ UCASE$ LCASE$ CHR$ ASC VAL STR$
