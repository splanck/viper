# POLISH-10: Complete DWARF v5 Debug Information

## Context
**Validated existing infrastructure:**
- `DebugLineTable.hpp/cpp` (254 LOC) — COMPLETE .debug_line encoding for DWARF v5
- `MachOWriter.cpp:508-638` — Emits `__debug_line` in `__DWARF` Mach-O segment
- `ElfWriter.cpp:228-466` — Also emits `.debug_line` for ELF targets
- `ObjectFileWriter.hpp:51-55` — `setDebugLineData()` setter exists
- `CodegenPipeline.cpp` — Both AArch64 (line 512) and x86_64 (line 862) pass debug data

**Missing sections:** `.debug_info`, `.debug_abbrev`, `.debug_str`, `.debug_aranges`

**Validated gap:** NO function/variable metadata is collected during codegen.
The DebugLineTable only collects file paths + address→line mappings. Phases 2-3
(variable locations, type info) require new metadata collection infrastructure.

**Complexity: L** | **Priority: P3**

## Design — Phase 1 Only (Compilation Unit + Function DIEs)

Phase 1 is achievable with existing infrastructure. Phases 2-3 deferred.

### New Files

- `src/codegen/common/objfile/DebugInfoBuilder.hpp` — Build .debug_info
- `src/codegen/common/objfile/DebugInfoBuilder.cpp` — DWARF encoding

### DWARF Structure (Phase 1)

```
.debug_abbrev:
  [1] DW_TAG_compile_unit (DW_CHILDREN_yes)
      DW_AT_name      DW_FORM_strp
      DW_AT_language   DW_FORM_data2
      DW_AT_stmt_list  DW_FORM_sec_offset
      DW_AT_low_pc     DW_FORM_addr
      DW_AT_high_pc    DW_FORM_data8

  [2] DW_TAG_subprogram (DW_CHILDREN_no)
      DW_AT_name      DW_FORM_strp
      DW_AT_low_pc     DW_FORM_addr
      DW_AT_high_pc    DW_FORM_data8

.debug_info:
  Compilation Unit Header (DWARF v5)
  [1] DW_TAG_compile_unit { name="module.zia", lang=Zia, stmt_list=0 }
    [2] DW_TAG_subprogram { name="start", low_pc=..., high_pc=... }
    [2] DW_TAG_subprogram { name="helper", low_pc=..., high_pc=... }
    NULL terminator

.debug_str:
  "module.zia\0"
  "start\0"
  "helper\0"
```

### Modified Files

| File | Change |
|------|--------|
| `src/codegen/common/objfile/ObjectFileWriter.hpp` | Add `setDebugInfoData()`, `setDebugAbbrevData()`, `setDebugStrData()` |
| `src/codegen/common/objfile/MachOWriter.cpp` | Emit `__debug_info`, `__debug_abbrev`, `__debug_str` sections |
| `src/codegen/common/objfile/ElfWriter.cpp` | Same for ELF |
| `src/codegen/aarch64/CodegenPipeline.cpp` | Collect function names + address ranges, build debug info |
| NEW: `DebugInfoBuilder.hpp/cpp` | Encode .debug_info, .debug_abbrev, .debug_str |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`
- `docs/codegen/native-assembler.md` — Document DWARF support
- `docs/codemap.md` — Add new files

## Verification
```bash
viper build test.zia -o test
lldb test
(lldb) bt              # Should show function names
(lldb) b start         # Should set breakpoint by name
(lldb) list            # Should show source lines
```
