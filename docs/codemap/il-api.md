# CODEMAP: IL API

- **src/il/api/expected_api.cpp**

  Provides the v2 expected-based façade for the IL API, exposing thin wrappers that translate legacy boolean interfaces into `il::support::Expected` results. `parse_text_expected` forwards to the IL parser while preserving module ownership, and `verify_module_expected` defers to the verifier so callers can chain diagnostics without manual capture. The implementation intentionally contains no additional logic, ensuring the v1 and v2 entry points stay behaviorally identical apart from error propagation style. Dependencies include `il/api/expected_api.hpp`, `il/core/Module.hpp`, `il/io/Parser.hpp`, and `viper/il/Verify.hpp`.
