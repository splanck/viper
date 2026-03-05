Spec sources:

- Authoritative: `x86_64_encodings.json`, `aarch64_encodings.json`
- Reference: `x86_64_encodings.yaml` (human-friendly view)

CMake generators consume the JSON files. If you update YAML, also reflect
the change in the JSON (or remove YAML if it drifts). A converter script may
be added later; for now treat JSON as SSOT.
