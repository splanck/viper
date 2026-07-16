# Tests for the agent-facing CLI surface: viper check, viper eval,
# viper explain, --print-error-codes, --dump-runtime-api, --dump-opcodes.
# Validates exit-code contracts and machine-readable output shapes.
cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED VIPER_EXE)
    message(FATAL_ERROR "VIPER_EXE must be provided")
endif ()
if (NOT DEFINED TEST_WORK_DIR)
    message(FATAL_ERROR "TEST_WORK_DIR must be provided")
endif ()

file(REMOVE_RECURSE "${TEST_WORK_DIR}")
file(MAKE_DIRECTORY "${TEST_WORK_DIR}")

# ===== viper check: clean file exits 0 =====
set(_ok_zia "${TEST_WORK_DIR}/ok.zia")
file(WRITE "${_ok_zia}" "module T;\nbind Viper.Terminal;\nfunc start() {\n    Say(\"hi\");\n}\n")

execute_process(
        COMMAND "${VIPER_EXE}" check "${_ok_zia}"
        RESULT_VARIABLE _check_ok_rc
        OUTPUT_VARIABLE _check_ok_out
        ERROR_VARIABLE _check_ok_err)
if (NOT _check_ok_rc EQUAL 0)
    message(FATAL_ERROR "viper check on a clean file should exit 0, got ${_check_ok_rc}:\n${_check_ok_err}")
endif ()

# ===== viper check: compile errors exit 2 with structured JSON =====
set(_bad_zia "${TEST_WORK_DIR}/bad.zia")
file(WRITE "${_bad_zia}" "module T;\nbind Viper.Terminal;\nfunc start() {\n    var count = 1;\n    SayInt(cout + count);\n}\n")

execute_process(
        COMMAND "${VIPER_EXE}" check "${_bad_zia}" --diagnostic-format=json
        RESULT_VARIABLE _check_bad_rc
        OUTPUT_VARIABLE _check_bad_out
        ERROR_VARIABLE _check_bad_err)
if (NOT _check_bad_rc EQUAL 2)
    message(FATAL_ERROR "viper check on a broken file should exit 2, got ${_check_bad_rc}:\n${_check_bad_err}")
endif ()
if (NOT _check_bad_err MATCHES "\"code\":\"V-ZIA-UNDEFINED\"")
    message(FATAL_ERROR "viper check JSON did not include the V-ZIA-UNDEFINED code:\n${_check_bad_err}")
endif ()
if (NOT _check_bad_err MATCHES "\"fixits\"" OR NOT _check_bad_err MATCHES "\"replacement\":\"count\"")
    message(FATAL_ERROR "viper check JSON did not carry the did-you-mean fix-it:\n${_check_bad_err}")
endif ()

# ===== viper check: unresolvable target exits 1 =====
execute_process(
        COMMAND "${VIPER_EXE}" check "${TEST_WORK_DIR}/does-not-exist.zia"
        RESULT_VARIABLE _check_missing_rc
        OUTPUT_VARIABLE _check_missing_out
        ERROR_VARIABLE _check_missing_err)
if (NOT _check_missing_rc EQUAL 1)
    message(FATAL_ERROR "viper check on a missing target should exit 1, got ${_check_missing_rc}")
endif ()

# ===== viper check: rejects run/build-only flags =====
execute_process(
        COMMAND "${VIPER_EXE}" check "${_ok_zia}" -o out.il
        RESULT_VARIABLE _check_o_rc
        OUTPUT_VARIABLE _check_o_out
        ERROR_VARIABLE _check_o_err)
if (_check_o_rc EQUAL 0)
    message(FATAL_ERROR "viper check should reject -o")
endif ()

# ===== viper eval: expression auto-print, exit 0 =====
execute_process(
        COMMAND "${VIPER_EXE}" eval "2 + 3 * 4"
        RESULT_VARIABLE _eval_rc
        OUTPUT_VARIABLE _eval_out
        ERROR_VARIABLE _eval_err)
if (NOT _eval_rc EQUAL 0)
    message(FATAL_ERROR "viper eval should exit 0, got ${_eval_rc}:\n${_eval_err}")
endif ()
if (NOT _eval_out MATCHES "14")
    message(FATAL_ERROR "viper eval '2 + 3 * 4' should print 14, got:\n${_eval_out}")
endif ()

# ===== viper eval --json --type: structured result =====
execute_process(
        COMMAND "${VIPER_EXE}" eval --json --type "1 + 1"
        RESULT_VARIABLE _eval_json_rc
        OUTPUT_VARIABLE _eval_json_out
        ERROR_VARIABLE _eval_json_err)
if (NOT _eval_json_rc EQUAL 0)
    message(FATAL_ERROR "viper eval --json should exit 0, got ${_eval_json_rc}:\n${_eval_json_err}")
endif ()
if (NOT _eval_json_out MATCHES "\"success\":true" OR NOT _eval_json_out MATCHES "\"resultType\":\"Integer\"")
    message(FATAL_ERROR "viper eval --json output malformed:\n${_eval_json_out}")
endif ()
if (NOT _eval_json_out MATCHES "\"type\":\"Integer\"")
    message(FATAL_ERROR "viper eval --type did not report the inferred type:\n${_eval_json_out}")
endif ()

# ===== viper eval: compile errors exit 2 =====
execute_process(
        COMMAND "${VIPER_EXE}" eval "thisIsNotDefined + 1"
        RESULT_VARIABLE _eval_bad_rc
        OUTPUT_VARIABLE _eval_bad_out
        ERROR_VARIABLE _eval_bad_err)
if (NOT _eval_bad_rc EQUAL 2)
    message(FATAL_ERROR "viper eval on bad code should exit 2, got ${_eval_bad_rc}")
endif ()

# ===== viper eval: runtime traps exit 3 =====
execute_process(
        COMMAND "${VIPER_EXE}" eval "1 / 0"
        RESULT_VARIABLE _eval_trap_rc
        OUTPUT_VARIABLE _eval_trap_out
        ERROR_VARIABLE _eval_trap_err)
if (NOT _eval_trap_rc EQUAL 3)
    message(FATAL_ERROR "viper eval on trapping code should exit 3, got ${_eval_trap_rc}")
endif ()

# ===== viper explain: cataloged code =====
execute_process(
        COMMAND "${VIPER_EXE}" explain V-ZIA-UNDEFINED
        RESULT_VARIABLE _explain_rc
        OUTPUT_VARIABLE _explain_out
        ERROR_VARIABLE _explain_err)
if (NOT _explain_rc EQUAL 0)
    message(FATAL_ERROR "viper explain V-ZIA-UNDEFINED should exit 0, got ${_explain_rc}:\n${_explain_err}")
endif ()
if (NOT _explain_out MATCHES "zia-sema")
    message(FATAL_ERROR "viper explain output missing subsystem:\n${_explain_out}")
endif ()

# ===== viper explain --json =====
execute_process(
        COMMAND "${VIPER_EXE}" explain W001 --json
        RESULT_VARIABLE _explain_json_rc
        OUTPUT_VARIABLE _explain_json_out
        ERROR_VARIABLE _explain_json_err)
if (NOT _explain_json_rc EQUAL 0 OR NOT _explain_json_out MATCHES "\"code\":\"W001\"")
    message(FATAL_ERROR "viper explain W001 --json malformed:\n${_explain_json_out}")
endif ()

# ===== viper explain: uncataloged code with known prefix still resolves =====
execute_process(
        COMMAND "${VIPER_EXE}" explain B2113
        RESULT_VARIABLE _explain_family_rc
        OUTPUT_VARIABLE _explain_family_out
        ERROR_VARIABLE _explain_family_err)
if (NOT _explain_family_rc EQUAL 0)
    message(FATAL_ERROR "viper explain on an uncataloged BASIC code should fall back to its family, got ${_explain_family_rc}")
endif ()

# ===== viper explain: unknown code exits 1 =====
execute_process(
        COMMAND "${VIPER_EXE}" explain TOTALLY-BOGUS
        RESULT_VARIABLE _explain_unknown_rc
        OUTPUT_VARIABLE _explain_unknown_out
        ERROR_VARIABLE _explain_unknown_err)
if (_explain_unknown_rc EQUAL 0)
    message(FATAL_ERROR "viper explain on an unknown code should exit non-zero")
endif ()

# ===== --print-error-codes --json =====
execute_process(
        COMMAND "${VIPER_EXE}" --print-error-codes --json
        RESULT_VARIABLE _codes_rc
        OUTPUT_VARIABLE _codes_out
        ERROR_VARIABLE _codes_err)
if (NOT _codes_rc EQUAL 0)
    message(FATAL_ERROR "--print-error-codes --json should exit 0, got ${_codes_rc}")
endif ()
if (NOT _codes_out MATCHES "\"code\":\"V-ZIA-TYPE-MISMATCH\"" OR NOT _codes_out MATCHES "\"code\":\"B2001\"")
    message(FATAL_ERROR "--print-error-codes catalog is missing expected entries:\n${_codes_out}")
endif ()

# ===== --dump-runtime-api =====
execute_process(
        COMMAND "${VIPER_EXE}" --dump-runtime-api
        RESULT_VARIABLE _api_rc
        OUTPUT_VARIABLE _api_out
        ERROR_VARIABLE _api_err)
if (NOT _api_rc EQUAL 0)
    message(FATAL_ERROR "--dump-runtime-api should exit 0, got ${_api_rc}")
endif ()
if (NOT _api_out MATCHES "\"functions\":" OR NOT _api_out MATCHES "\"classes\":")
    message(FATAL_ERROR "--dump-runtime-api missing top-level sections")
endif ()
if (NOT _api_out MATCHES "\"schema_version\":4" OR
    NOT _api_out MATCHES "\"signature_dialect\":\"runtime-def-v1\"" OR
    NOT _api_out MATCHES "\"public_boundary\":\"registry\"" OR
    NOT _api_out MATCHES "\"c_abi_status\":\"internal-embedding\"")
    message(FATAL_ERROR "--dump-runtime-api missing schema metadata")
endif ()
if (NOT _api_out MATCHES "\"documentation\":\\{\"summary\":" OR
    NOT _api_out MATCHES "\"format\":\"markdown\"")
    message(FATAL_ERROR "--dump-runtime-api missing authored class documentation")
endif ()
if (NOT _api_out MATCHES "Provides immutable runtime string values" OR
    NOT _api_out MATCHES "`Viper.String`")
    message(FATAL_ERROR "--dump-runtime-api did not preserve authored String documentation")
endif ()
if (NOT _api_out MATCHES "\"fallibility\":" OR NOT _api_out MATCHES "\"class_kind\":")
    message(FATAL_ERROR "--dump-runtime-api missing production contract metadata")
endif ()
# VDOC-198: IO conversion/allocation entries carry accurate trapping + owned
# contracts (previously mislabeled infallible / unknown ownership).
if (NOT _api_out MATCHES
        "\"name\":\"Viper.IO.Stream.AsBinFile\"[^\n]*\"fallibility\":\"traps\"[^\n]*\"ownership\":\"owned\"")
    message(FATAL_ERROR "--dump-runtime-api Stream.AsBinFile contract regressed (expected traps/owned)")
endif ()
if (NOT _api_out MATCHES
        "\"name\":\"Viper.IO.LineWriter.Append\"[^\n]*\"fallibility\":\"traps\"[^\n]*\"ownership\":\"owned\"")
    message(FATAL_ERROR "--dump-runtime-api LineWriter.Append contract regressed (expected traps/owned)")
endif ()
# VDOC-209: trapping/allocating Math entries carry accurate traps + owned
# contracts (were mislabeled infallible / unknown ownership).
if (NOT _api_out MATCHES
        "\"name\":\"Viper.Math.Mat4.Inverse\"[^\n]*\"fallibility\":\"traps\"[^\n]*\"ownership\":\"owned\"")
    message(FATAL_ERROR "--dump-runtime-api Mat4.Inverse contract regressed (expected traps/owned)")
endif ()
if (NOT _api_out MATCHES
        "\"name\":\"Viper.Math.BigInt.Div\"[^\n]*\"fallibility\":\"traps\"[^\n]*\"ownership\":\"owned\"")
    message(FATAL_ERROR "--dump-runtime-api BigInt.Div contract regressed (expected traps/owned)")
endif ()
# VDOC-222: System entries surface their hidden nulls, traps, and statuses.
# Process spawns return NULL on failure (nullable return, not a live handle),
# Unsafe releases trap on an invalid object, and PtySession.Resize returns a
# boolean status rather than an infallible void.
if (NOT _api_out MATCHES
        "\"name\":\"Viper.System.Process.Start\"[^\n]*\"nullable\":true[^\n]*\"fallibility\":\"infallible\"[^\n]*\"ownership\":\"owned\"")
    message(FATAL_ERROR "--dump-runtime-api Process.Start contract regressed (expected nullable return / owned)")
endif ()
if (NOT _api_out MATCHES
        "\"name\":\"Viper.Runtime.Unsafe.Release\"[^\n]*\"fallibility\":\"traps\"")
    message(FATAL_ERROR "--dump-runtime-api Unsafe.Release contract regressed (expected traps)")
endif ()
if (NOT _api_out MATCHES
        "\"name\":\"Viper.System.Pty.PtySession.Resize\"[^\n]*\"fallibility\":\"status\"")
    message(FATAL_ERROR "--dump-runtime-api PtySession.Resize contract regressed (expected status)")
endif ()
if (NOT _api_out MATCHES
        "\"name\":\"Viper.Graphics3D.Canvas3D.TryCopyScreenshotTo\"[^\n]*\"c_symbol\":\"rt_canvas3d_try_copy_screenshot_to\"[^\n]*\"fallibility\":\"status\"[^\n]*\"ownership\":\"value\"[^\n]*\"contract_source\":\"three-d-boundary-policy\"")
    message(FATAL_ERROR "--dump-runtime-api missing the allocation-reusing Canvas3D status contract")
endif ()
if (NOT _api_out MATCHES
        "\"name\":\"Viper.Graphics3D.Canvas3D.Screenshot\"[^\n]*\"c_symbol\":\"rt_canvas3d_screenshot\"[^\n]*\"nullable\":true[^\n]*\"fallibility\":\"nullable\"[^\n]*\"ownership\":\"managed\"")
    message(FATAL_ERROR "--dump-runtime-api missing the Canvas3D screenshot ownership contract")
endif ()
if (NOT _api_out MATCHES
        "\"name\":\"TryCopyScreenshotTo\"[^\n]*\"target\":\"Viper.Graphics3D.Canvas3D.TryCopyScreenshotTo\"[^\n]*\"c_symbol\":\"rt_canvas3d_try_copy_screenshot_to\"")
    message(FATAL_ERROR "--dump-runtime-api class methods must resolve to backing C symbols")
endif ()
if (NOT _api_out MATCHES "Viper.Terminal.Say")
    message(FATAL_ERROR "--dump-runtime-api missing canonical function entries")
endif ()
if (NOT _api_out MATCHES "Viper.Diagnostics.CurrentTrap" OR
    NOT _api_out MATCHES "Viper.Diagnostics.TrapInfo")
    message(FATAL_ERROR "--dump-runtime-api missing diagnostics trap snapshot entries")
endif ()
if (NOT _api_out MATCHES "Viper.Runtime.Unsafe.Retain" OR
    NOT _api_out MATCHES "Viper.Runtime.Unsafe.SetThrowMsg" OR
    NOT _api_out MATCHES "Viper.Runtime.Unsafe.SetTrapFields" OR
    NOT _api_out MATCHES "Viper.Runtime.GC.Collect")
    message(FATAL_ERROR "--dump-runtime-api missing runtime namespace memory entries")
endif ()
if (NOT _api_out MATCHES "Viper.Core.Parse.TryDouble" OR
    NOT _api_out MATCHES "Viper.Core.Parse.DoubleOr")
    message(FATAL_ERROR "--dump-runtime-api missing canonical parse double entries")
endif ()
if (NOT _api_out MATCHES "Viper.Core.Convert.ToStringInt" OR
    NOT _api_out MATCHES "Viper.Core.Convert.ToStringDouble")
    message(FATAL_ERROR "--dump-runtime-api missing canonical convert string entries")
endif ()
if (NOT _api_out MATCHES "Viper.Collections.Seq.get_Capacity" OR
    NOT _api_out MATCHES "Viper.Threads.Channel.get_Capacity" OR
    NOT _api_out MATCHES "Viper.IO.BinaryBuffer.NewCapacity")
    message(FATAL_ERROR "--dump-runtime-api missing canonical capacity entries")
endif ()
if (NOT _api_out MATCHES "Viper.Collections.BloomFilter.FalsePositiveRate")
    message(FATAL_ERROR "--dump-runtime-api missing canonical BloomFilter rate entry")
endif ()
if (NOT _api_out MATCHES "Viper.Text.Fmt.Scientific" OR
    NOT _api_out MATCHES "Viper.Text.Fmt.Percent" OR
    NOT _api_out MATCHES "Viper.Text.Fmt.YesNo")
    message(FATAL_ERROR "--dump-runtime-api missing canonical formatting entries")
endif ()
if (NOT _api_out MATCHES "Viper.Collections.LruCache.Set" OR
    NOT _api_out MATCHES "Viper.Collections.BiMap.Set" OR
    NOT _api_out MATCHES "Viper.Collections.MultiMap.Add")
    message(FATAL_ERROR "--dump-runtime-api missing canonical collection write entries")
endif ()
if (NOT _api_out MATCHES "Viper.Graphics3D.Mesh3D.Box" OR
    NOT _api_out MATCHES "Viper.Graphics3D.Material3D.FromColor" OR
    NOT _api_out MATCHES "Viper.Graphics3D.Light3D.Directional" OR
    NOT _api_out MATCHES "Viper.Graphics3D.Collider3D.Capsule" OR
    NOT _api_out MATCHES "Viper.Game3D.World3D.WithHorizontalCamera")
    message(FATAL_ERROR "--dump-runtime-api missing canonical factory entries")
endif ()
if (NOT _api_out MATCHES "Viper.Collections.Queue.TryPop" OR
    NOT _api_out MATCHES "Viper.Collections.Heap.TryPeek" OR
    NOT _api_out MATCHES "Viper.Collections.Deque.TryPopBack" OR
    NOT _api_out MATCHES "Viper.Threads.Promise.GetFuture.*obj<Viper.Threads.Future>" OR
    NOT _api_out MATCHES "Viper.Threads.Async.Delay.*obj<Viper.Threads.Future>" OR
    NOT _api_out MATCHES "Viper.Threads.Channel.TryRecv" OR
    NOT _api_out MATCHES "Viper.Threads.Future.TryGet" OR
    NOT _api_out MATCHES "Viper.Time.DateTime.TryParse" OR
    NOT _api_out MATCHES "Viper.Localization.MessageBundle.TryGet" OR
    NOT _api_out MATCHES "Viper.Localization.MessageBundle.GetOr" OR
    NOT _api_out MATCHES "Viper.Localization.Locale.TryParse" OR
    NOT _api_out MATCHES "Viper.Collections.Bytes.Find" OR
    NOT _api_out MATCHES "Viper.Collections.UnionFind.FindRoot" OR
    NOT _api_out MATCHES "Viper.Collections.Seq.FindWhereOption" OR
    NOT _api_out MATCHES "Viper.Text.Pattern.Find" OR
    NOT _api_out MATCHES "Viper.Data.Xml.Find" OR
    NOT _api_out MATCHES "Viper.Graphics2D.SceneGraph.Find" OR
    NOT _api_out MATCHES "Viper.Audio.Mixer.FindGroup" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneGraph.Find" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneNode.Find" OR
    NOT _api_out MATCHES "Viper.Graphics3D.Skeleton3D.FindBoneOption" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneAsset.FindNode" OR
    NOT _api_out MATCHES "Viper.Game3D.World3D.FindNode" OR
    NOT _api_out MATCHES "Viper.Game3D.World3D.FindEntity" OR
    NOT _api_out MATCHES "Viper.Graphics3D.NavMesh3D.FindPathOption" OR
    NOT _api_out MATCHES "Viper.GUI.FindBar.FindNext" OR
    NOT _api_out MATCHES "Viper.GUI.TestHarness.FindById" OR
    NOT _api_out MATCHES "Viper.GUI.CommandRegistry.Find" OR
    NOT _api_out MATCHES "Viper.Game.UI.HudTableClickResult.RowOption" OR
    NOT _api_out MATCHES "Viper.Game.UI.HudTableClickResult.ColumnOption")
    message(FATAL_ERROR "--dump-runtime-api missing modern Option-returning failure entries")
endif ()
if (NOT _api_out MATCHES "Viper.Graphics.Canvas.SetMaxDeltaTime" OR
    NOT _api_out MATCHES "Viper.Graphics3D.Canvas3D.SetMaxDeltaTime")
    message(FATAL_ERROR "--dump-runtime-api missing canonical frame delta entries")
endif ()
if (NOT _api_out MATCHES "Viper.Input.Key" OR
    NOT _api_out MATCHES "Viper.Input.Key.get_A" OR
    NOT _api_out MATCHES "Viper.Input.Key.get_LeftShift" OR
    NOT _api_out MATCHES "Viper.Input.Key.get_NumpadDecimal")
    message(FATAL_ERROR "--dump-runtime-api missing canonical input key entries")
endif ()
if (NOT _api_out MATCHES "\"name\":\"Viper.Math.Random.Chance\"[^\\n]*\"signature\":\"i1\\(f64\\)\"")
    message(FATAL_ERROR "--dump-runtime-api missing boolean Random.Chance row")
endif ()
if (NOT _api_out MATCHES "Viper.Crypto.Compliance.EnableApprovedModeForProcess" OR
    NOT _api_out MATCHES "Viper.Crypto.Compliance.DisableApprovedModeForProcess" OR
    NOT _api_out MATCHES "Viper.Crypto.Compliance.IsApprovedModeForProcess")
    message(FATAL_ERROR "--dump-runtime-api missing process-scoped Crypto.Compliance policy rows")
endif ()
if (NOT _api_out MATCHES "Viper.Runtime.Unsafe.ValueType" OR
    NOT _api_out MATCHES "Viper.Runtime.Unsafe.ValueTypeAddField")
    message(FATAL_ERROR "--dump-runtime-api missing unsafe boxed value-type rows")
endif ()
if (NOT _api_out MATCHES "Viper.Game3D.Prefab.Load" OR
    NOT _api_out MATCHES "Viper.Game3D.Prefab.LoadAsset" OR
    NOT _api_out MATCHES "Viper.Game3D.AssetHandle3D.GetPrefab")
    message(FATAL_ERROR "--dump-runtime-api missing canonical Game3D prefab loading rows")
endif ()
if (NOT _api_out MATCHES "Viper.Network.HttpReq.SendResult" OR
    NOT _api_out MATCHES "Viper.Network.RestClient.GetResult" OR
    NOT _api_out MATCHES "Viper.Network.RestClient.PostResult" OR
    NOT _api_out MATCHES "Viper.Network.SmtpClient.SendResult" OR
    NOT _api_out MATCHES "Viper.Network.SmtpClient.SendHtmlResult" OR
    NOT _api_out MATCHES "Viper.Network.RestClient.HeadResult")
    message(FATAL_ERROR "--dump-runtime-api missing network Result-returning HTTP rows")
endif ()
if (NOT _api_out MATCHES "Viper.Data.JsonStream.NextResult" OR
    NOT _api_out MATCHES "Viper.Data.Xml.ParseResult" OR
    NOT _api_out MATCHES "Viper.Data.Yaml.ParseResult" OR
    NOT _api_out MATCHES "Viper.Data.Serialize.ParseResult" OR
    NOT _api_out MATCHES "Viper.Data.Serialize.AutoParseResult")
    message(FATAL_ERROR "--dump-runtime-api missing Result-returning data format parse rows")
endif ()
if (NOT _api_out MATCHES "Viper.Crypto.Tls.ConnectResult" OR
    NOT _api_out MATCHES "Viper.Crypto.Tls.ConnectForResult" OR
    NOT _api_out MATCHES "Viper.Crypto.Tls.ConnectOptionsResult" OR
    NOT _api_out MATCHES "Viper.System.Pty.OpenResult" OR
    NOT _api_out MATCHES "Viper.Zia.SemanticJob.Error" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneAsset.LoadResult" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneAsset.LoadAssetResult" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneAsset.LoadAnimationResult" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneAsset.LoadAnimationAssetResult" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneAsset.LoadNodeAnimationResult" OR
    NOT _api_out MATCHES "Viper.Graphics3D.SceneAsset.LoadNodeAnimationAssetResult" OR
    NOT _api_out MATCHES "Viper.Game2D.SceneDocument.LoadResult" OR
    NOT _api_out MATCHES "Viper.Game2D.SceneDocument.LoadJsonResult")
    message(FATAL_ERROR "--dump-runtime-api missing Result-returning connect/open/load rows")
endif ()
if (NOT _api_out MATCHES "Viper.Crypto.Cipher.DecryptResult" OR
    NOT _api_out MATCHES "Viper.Crypto.Cipher.TryDecryptWithKeyAad" OR
    NOT _api_out MATCHES "Viper.Crypto.Aes.DecryptAuthResult" OR
    NOT _api_out MATCHES "Viper.Crypto.Aes.TryDecryptStr" OR
    NOT _api_out MATCHES "Viper.Network.HttpReq.AllowInsecureCertificatesForTesting" OR
    NOT _api_out MATCHES "Viper.Crypto.Legacy.Hash.Md5" OR
    NOT _api_out MATCHES "Viper.Crypto.Legacy.Aes.DecryptCbcResult")
    message(FATAL_ERROR "--dump-runtime-api missing modern crypto failure/legacy entries")
endif ()
if (NOT _api_out MATCHES "Viper.Math.Bits.CountLeadingZeros" OR
    NOT _api_out MATCHES "Viper.Math.Bits.RotateLeft" OR
    NOT _api_out MATCHES "Viper.Math.Bits.ShiftRightLogical")
    message(FATAL_ERROR "--dump-runtime-api missing canonical math bit entries")
endif ()
if (NOT _api_out MATCHES "Viper.Terminal.TryReadLine" OR
    NOT _api_out MATCHES "Viper.Terminal.ReadLineResult")
    message(FATAL_ERROR "--dump-runtime-api missing modern terminal input entries")
endif ()
if (NOT _api_out MATCHES "Viper.Game.Pathfinder.FindPath" OR
    NOT _api_out MATCHES "Viper.Game.PathResult.get_Found" OR
    NOT _api_out MATCHES "Viper.Game.PathResult.get_StepCount" OR
    NOT _api_out MATCHES "Viper.Game.Quadtree.QueryRect" OR
    NOT _api_out MATCHES "Viper.Game.QueryResult.GetId" OR
    NOT _api_out MATCHES "Viper.Game.Quadtree.QueryPairs" OR
    NOT _api_out MATCHES "Viper.Game.QuadtreePairResult.First" OR
    NOT _api_out MATCHES "Viper.Game.UI.HudTable.HandleClick" OR
    NOT _api_out MATCHES "Viper.Game.UI.HudTableClickResult.get_IsHeader" OR
    NOT _api_out MATCHES "Viper.Game.AnimStateMachine.PollEvents" OR
    NOT _api_out MATCHES "Viper.Game.AnimationEventBatch.GetId")
    message(FATAL_ERROR "--dump-runtime-api missing game result snapshot entries")
endif ()
if (_api_out MATCHES "str\\?")
    message(FATAL_ERROR "--dump-runtime-api should not expose undocumented nullable suffix signatures")
endif ()
if (NOT _api_out MATCHES "\"fallibility\":\"option\"" OR
    NOT _api_out MATCHES "\"fallibility\":\"result\"")
    message(FATAL_ERROR "--dump-runtime-api missing Option/Result fallibility metadata")
endif ()

# ===== --dump-opcodes =====
execute_process(
        COMMAND "${VIPER_EXE}" --dump-opcodes
        RESULT_VARIABLE _ops_rc
        OUTPUT_VARIABLE _ops_out
        ERROR_VARIABLE _ops_err)
if (NOT _ops_rc EQUAL 0)
    message(FATAL_ERROR "--dump-opcodes should exit 0, got ${_ops_rc}")
endif ()
if (NOT _ops_out MATCHES "\"mnemonic\":\"add\"" OR NOT _ops_out MATCHES "\"opcodes\":")
    message(FATAL_ERROR "--dump-opcodes output malformed:\n${_ops_out}")
endif ()

message(STATUS "Agent CLI tests passed")
