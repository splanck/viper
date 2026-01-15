# Zia Bugs

This log captures Zia language defects found during the runtime sweep.

## Format

- **ID**: VL-###
- **Area**: parser / sema / lowering / runtime integration
- **Summary**: Brief description
- **Root Cause**: file:line + explanation
- **Notes**: Additional context

---

## Open Bugs

- **ID**: VL-001
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.LastIndexOf` but runtime has no such function.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1118` declares `Viper.String.LastIndexOf` but there is no corresponding `RT_FUNC` in `runtime.def`.
  - **Notes**: Runtime has `IndexOfFrom` for searching from a position, but no reverse search. Calling this function will fail at IL lowering or link time.

- **ID**: VL-002
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.Contains` but runtime has `Viper.String.Has`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1119` declares `Viper.String.Contains`, but runtime binds to `Viper.String.Has` at `runtime.def:StrHas`.
  - **Fix**: Either rename in sema to `Has` or add `RT_ALIAS("Viper.String.Contains", StrHas)`.

- **ID**: VL-003
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.TrimLeft`/`TrimRight` but runtime has `TrimStart`/`TrimEnd`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1125-1126` declares `TrimLeft`/`TrimRight`, but runtime binds to `TrimStart`/`TrimEnd`.
  - **Fix**: Either rename in sema or add aliases in runtime.def.

- **ID**: VL-004
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.ReplaceAll` but runtime only has `Replace`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1128` declares `ReplaceAll`, but runtime's `Replace` already replaces all occurrences.
  - **Fix**: Add `RT_ALIAS("Viper.String.ReplaceAll", StrReplace)`.

- **ID**: VL-005
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.Reverse` but runtime has `Viper.String.Flip`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1132` declares `Reverse`, but runtime binds to `Flip`.
  - **Fix**: Either rename in sema or add `RT_ALIAS("Viper.String.Reverse", StrFlip)`.

- **ID**: VL-006
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.IsBlank` but runtime has no such function.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1137` declares `IsBlank` with no runtime implementation.
  - **Fix**: Implement `rt_str_is_blank()` in runtime or remove from sema.

- **ID**: VL-007
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.Compare` but runtime has `Viper.String.Cmp`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1138` declares `Compare`, but runtime binds to `Cmp`.
  - **Fix**: Either rename in sema or add `RT_ALIAS("Viper.String.Compare", StrCmp)`.

- **ID**: VL-008
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.CharAt` but runtime has no such function.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1112` declares `CharAt` with no runtime implementation.
  - **Notes**: Runtime has `Mid` which can extract single characters, but CharAt is not bound.

- **ID**: VL-009
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.Format` but runtime has no such function.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1135` declares `Format` with no runtime implementation.
  - **Notes**: This would require implementing printf-style or template-style formatting in runtime.

- **ID**: VL-010
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.Join` but runtime has `Viper.Strings.Join` (different namespace).
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1130` declares `Viper.String.Join`, but runtime binds to `Viper.Strings.Join`.
  - **Fix**: Add `RT_ALIAS("Viper.String.Join", StringsJoin)`.

- **ID**: VL-011
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.String.IsEmpty` but runtime has `Viper.String.get_IsEmpty`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:1136` declares `Viper.String.IsEmpty`, but runtime binds property getter to `Viper.String.get_IsEmpty`.
  - **Fix**: Add `RT_ALIAS("Viper.String.IsEmpty", StrIsEmpty)`.

- **ID**: VL-012
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.Collections.Seq.Add` but runtime has `Viper.Collections.Seq.Push`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:777` declares `Seq.Add`, but runtime binds to `Seq.Push` at `runtime.def:SeqPush`.
  - **Fix**: Either rename in sema to `Push` or add `RT_ALIAS("Viper.Collections.Seq.Add", SeqPush)`.

- **ID**: VL-013
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.Collections.TreeMap.Remove` but runtime has `TreeMap.Drop`.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:796` declares `TreeMap.Remove`, but runtime binds to `TreeMap.Drop` at `runtime.def:TreeMapDrop`.
  - **Fix**: Either rename in sema or add `RT_ALIAS("Viper.Collections.TreeMap.Remove", TreeMapDrop)`.

- **ID**: VL-014
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Multiple `Viper.IO.Path.*` name mismatches.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:958-967` declares different names than runtime:
    - `Path.GetDir` → runtime has `Path.Dir`
    - `Path.GetName` → runtime has `Path.Name`
    - `Path.GetExt` → runtime has `Path.Ext`
    - `Path.GetBase` → runtime has `Path.Stem`
    - `Path.Normalize` → runtime has `Path.Norm`
    - `Path.Absolute` → runtime has `Path.Abs`
  - **Fix**: Either rename in sema or add aliases for each.

- **ID**: VL-015
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Sema declares `Viper.IO.Path.Exists/IsFile/IsDir` but runtime has no such functions.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:963-965` declares `Path.Exists`, `Path.IsFile`, `Path.IsDir`, but runtime only has `File.Exists` and `Dir.Exists`.
  - **Fix**: Either implement in runtime or remove from sema.

- **ID**: VL-016
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: Multiple `Viper.IO.Dir.*` name mismatches and missing functions.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:944-953` declares:
    - `Dir.GetCurrent` → runtime has `Dir.Current`
    - `Dir.SetCurrent` → runtime has `Dir.SetCurrent` (OK)
    - `Dir.GetHome` → runtime has no equivalent
    - `Dir.GetTemp` → runtime has no equivalent
    - `Dir.Create` → runtime has `Dir.Make`
    - `Dir.Delete` → runtime has `Dir.Remove`
  - **Fix**: Either rename in sema or add aliases/implementations in runtime.

- **ID**: VL-017
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: `Viper.IO.File.*` signature and naming mismatches.
  - **Root Cause**: `src/frontends/zia/Sema.cpp:928-939`:
    - `File.Delete` returns `boolean` in sema but runtime returns `void`
    - `File.Copy` returns `boolean` in sema but runtime returns `void`
    - `File.Move` returns `boolean` in sema but runtime returns `void`
    - `File.AppendText` → runtime has `File.Append`
    - `File.GetModTime` → runtime has `File.Modified`
    - `File.SetModTime` → runtime has no equivalent
  - **Fix**: Align return types and names between sema and runtime.

- **ID**: VL-018
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: `Viper.Convert` numeric parsing names are mismatched between sema and runtime.
  - **Root Cause**:
    - `src/frontends/zia/Sema.cpp:1265-1267` registers `Viper.Convert.StrToInt/StrToNum/StrToBool`
    - `src/il/runtime/runtime.def:1008-1010` exposes `Viper.Convert.ToDouble/ToInt64` (no `StrTo*` entry)
  - **Notes**: Calling `StrToInt/StrToNum` fails with `unknown callee` at IL/runtime, while calling `ToInt64/ToDouble` fails in sema as undefined.
  - **Fix**: Add runtime aliases for `StrTo*` or update sema to use `ToInt64/ToDouble` (and add `ToBool` if desired).

- **ID**: VL-019
  - **Area**: sema (Runtime Function Registry)
  - **Summary**: `Viper.Parse` API names are out of sync with the runtime.
  - **Root Cause**:
    - `src/frontends/zia/Sema.cpp:1092-1096` registers `Viper.Parse.Int/Num/Bool/TryInt/TryNum`
    - `src/il/runtime/runtime.def:1006-1026` exposes `Viper.Parse.Int64/Double/TryBool/IntOr/NumOr/BoolOr/IsInt/IsNum/IntRadix`
  - **Notes**: Many documented `Viper.Parse` helpers cannot be called from Zia because sema does not register them.
  - **Fix**: Align sema with runtime names and add missing bindings for `TryBool`, `IntOr`, `NumOr`, `BoolOr`, `IsInt`, `IsNum`, and `IntRadix`.

---

## Closed/Fixed

- **ID**: VL-000
  - **Area**: runtime sweep
  - **Summary**: No Zia defects observed in the current sweep.
  - **Repro**: `tests/zia_runtime/*`
  - **Expected**: All programs pass.
  - **Actual**: All programs passed.
  - **Notes**: Logged as baseline for the 2025-12-22 sweep.
