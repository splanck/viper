# LOC/SLOC Report

Notes:

- `LOC` = total lines. `SLOC` = non-blank, non-comment code lines (heuristic comment stripping).
- C/C++/Assembly: strips `//` and `/* */` comments. Shell/Python/CMake: strips `#` comment-only lines. Markdown: SLOC =
  non-blank lines.
- Excludes: `.git/`, `build/`, `cmake-build-debug/`, `disk.img`.

## Totals by language

| Language     | LOC   | SLOC  |
|--------------|-------|-------|
| C++          | 27038 | 16621 |
| Markdown     | 20398 | 15889 |
| C/C++ Header | 16064 | 4501  |
| Shell        | 1192  | 882   |
| C            | 863   | 409   |
| Assembly     | 639   | 314   |
| CMake        | 253   | 189   |

## Totals by subsystem (all languages)

| Subsystem      | LOC   | SLOC  |
|----------------|-------|-------|
| docs           | 20398 | 15889 |
| kernel/net     | 12519 | 6352  |
| kernel/drivers | 3658  | 1809  |
| kernel/fs      | 3154  | 1582  |
| user/vinit     | 2490  | 1502  |
| kernel/arch    | 2695  | 1485  |
| kernel/ipc     | 1766  | 931   |
| vboot          | 1717  | 820   |
| kernel/core    | 1146  | 757   |
| tools          | 1286  | 740   |
| scripts        | 921   | 671   |
| kernel/viper   | 1268  | 593   |
| kernel/sched   | 1251  | 569   |
| user/core      | 1562  | 525   |
| kernel/syscall | 1017  | 489   |
| kernel/mm      | 1177  | 482   |
| kernel/console | 1132  | 481   |
| kernel/kobj    | 1127  | 436   |
| kernel/assign  | 876   | 406   |
| kernel/include | 1077  | 405   |
| kernel/input   | 668   | 363   |
| kernel/loader  | 584   | 274   |
| kernel/boot    | 557   | 238   |
| root           | 293   | 222   |
| kernel/cap     | 552   | 201   |
| kernel/lib     | 493   | 197   |
| include        | 728   | 196   |
| kernel/build   | 109   | 94    |
| tests          | 156   | 49    |
| cmake          | 43    | 26    |
| user/build     | 27    | 21    |

## Totals by subsystem (code-only, excludes Markdown)

| Subsystem      | LOC   | SLOC |
|----------------|-------|------|
| kernel/net     | 12519 | 6352 |
| kernel/drivers | 3658  | 1809 |
| kernel/fs      | 3154  | 1582 |
| user/vinit     | 2490  | 1502 |
| kernel/arch    | 2695  | 1485 |
| kernel/ipc     | 1766  | 931  |
| vboot          | 1717  | 820  |
| kernel/core    | 1146  | 757  |
| tools          | 1286  | 740  |
| scripts        | 921   | 671  |
| kernel/viper   | 1268  | 593  |
| kernel/sched   | 1251  | 569  |
| user/core      | 1562  | 525  |
| kernel/syscall | 1017  | 489  |
| kernel/mm      | 1177  | 482  |
| kernel/console | 1132  | 481  |
| kernel/kobj    | 1127  | 436  |
| kernel/assign  | 876   | 406  |
| kernel/include | 1077  | 405  |
| kernel/input   | 668   | 363  |
| kernel/loader  | 584   | 274  |
| kernel/boot    | 557   | 238  |
| root           | 293   | 222  |
| kernel/cap     | 552   | 201  |
| kernel/lib     | 493   | 197  |
| include        | 728   | 196  |
| kernel/build   | 109   | 94   |
| tests          | 156   | 49   |
| cmake          | 43    | 26   |
| user/build     | 27    | 21   |

## Subsystem × language (top 80 by SLOC)

| Subsystem      | Language     | LOC   | SLOC  |
|----------------|--------------|-------|-------|
| docs           | Markdown     | 20398 | 15889 |
| kernel/net     | C++          | 8699  | 5400  |
| user/vinit     | C++          | 2490  | 1502  |
| kernel/drivers | C++          | 2200  | 1378  |
| kernel/fs      | C++          | 2010  | 1311  |
| kernel/arch    | C++          | 1825  | 1140  |
| kernel/net     | C/C++ Header | 3820  | 952   |
| kernel/ipc     | C++          | 1189  | 803   |
| kernel/core    | C++          | 1146  | 757   |
| tools          | C++          | 1286  | 740   |
| scripts        | Shell        | 921   | 671   |
| user/core      | C/C++ Header | 1562  | 525   |
| kernel/syscall | C++          | 977   | 483   |
| kernel/sched   | C++          | 732   | 442   |
| kernel/viper   | C++          | 689   | 433   |
| kernel/drivers | C/C++ Header | 1458  | 431   |
| kernel/console | C++          | 754   | 430   |
| vboot          | C            | 863   | 409   |
| kernel/include | C/C++ Header | 1077  | 405   |
| kernel/mm      | C++          | 719   | 401   |
| vboot          | C/C++ Header | 756   | 364   |
| kernel/assign  | C++          | 592   | 357   |
| kernel/kobj    | C++          | 477   | 278   |
| kernel/fs      | C/C++ Header | 1144  | 271   |
| kernel/arch    | Assembly     | 424   | 252   |
| kernel/input   | C++          | 359   | 238   |
| root           | Shell        | 271   | 211   |
| kernel/loader  | C++          | 309   | 201   |
| kernel/lib     | C/C++ Header | 493   | 197   |
| include        | C/C++ Header | 728   | 196   |
| kernel/boot    | C++          | 312   | 185   |
| kernel/viper   | C/C++ Header | 579   | 160   |
| kernel/kobj    | C/C++ Header | 650   | 158   |
| kernel/ipc     | C/C++ Header | 577   | 128   |
| kernel/input   | C/C++ Header | 309   | 125   |
| kernel/cap     | C++          | 172   | 106   |
| kernel/cap     | C/C++ Header | 380   | 95    |
| kernel/build   | CMake        | 109   | 94    |
| kernel/arch    | C/C++ Header | 446   | 93    |
| kernel/sched   | C/C++ Header | 405   | 88    |
| kernel/mm      | C/C++ Header | 458   | 81    |
| kernel/loader  | C/C++ Header | 275   | 73    |
| kernel/boot    | C/C++ Header | 245   | 53    |
| kernel/console | C/C++ Header | 378   | 51    |
| kernel/assign  | C/C++ Header | 284   | 49    |
| kernel/sched   | Assembly     | 114   | 39    |
| vboot          | CMake        | 52    | 37    |
| tests          | C++          | 101   | 36    |
| cmake          | CMake        | 43    | 26    |
| user/build     | CMake        | 27    | 21    |
| tests          | Assembly     | 55    | 13    |
| root           | CMake        | 22    | 11    |
| vboot          | Assembly     | 46    | 10    |
| kernel/syscall | C/C++ Header | 40    | 6     |

