# Plan 03 — Modern Compressed-Texture Pipeline (KTX2 Supercompression, BCn Coverage, Fuzzing, Recoverable Errors)

> **Status (2026-07-03):** IMPLEMENTED except Basis Universal. Landed: trap→recoverable
> conversion (all malformed-input paths report via rt_asset_error; tests updated),
> BC1/BC4/BC5 decoders + full native-upload chain (incl. the previously missing BC3
> native path) with hand-packed block tests, the from-scratch Zstandard decoder
> (`src/runtime/io/rt_zstd.{h,c}`, 9/9 unit tests + 60/60 byte-exact stress round-trips
> against the reference encoder at levels 1–19), KTX2 supercompression schemes 2
> (Zstd) and 3 (ZLIB) end-to-end (242/242 canvas3d tests), and fuzz harnesses +
> corpora for KTX2, Zstd, and the PNG/JPEG/GIF decoders.
> **Deferred:** UASTC and ETC1S/BasisLZ transcode (scheme 1 + vkFormat 0/DFD files
> fail cleanly with a recoverable UNSUPPORTED diagnostic naming the scheme). They
> need a 19-mode UASTC block decoder and sgd codebook parsing — a bounded follow-up
> now that supercompression plumbing, native-upload wiring, and fuzz infrastructure
> all exist.

## 1. Objective & scope

Make Viper consume the output of the standard glTF texture toolchain. Today a `.ktx2` produced by `toktx` / `gltf-transform` with default settings (ETC1S/UASTC + Zstd supercompression) **traps the process**. BC1 (the most common opaque DXT format), BC4/BC5 (single/two-channel — normal maps and ORM packing), and BC6H (HDR) are undecodable. The from-scratch image decoders — the most attacker-controlled byte parsers in the runtime — have no fuzz coverage.

**In scope:** Zstd decode (from scratch); Basis Universal transcode — UASTC first, ETC1S second (from scratch); BC1/BC4/BC5 decoders + native GPU upload (and the missing BC3 native upload); BC6H decode (stretch, pairs with plan 01 HDR envs); KTX2 trap→recoverable-error conversion + non-trapping memory entry; fuzz harnesses for KTX2 + PNG/JPEG/GIF.
**Out of scope:** texture *encoding*, KTX1, Draco (see plan 09's deferral), ASTC HDR profile.

**Zero external dependencies — absolute.** No zstd lib, no Basis transcoder lib, no libktx, no squish/bcdec, no stb. Every decoder in this plan is written from scratch in C against the public format specifications (RFC 8878 for Zstd; the Khronos KTX2/BasisU data-format specs; the D3D/BCn block specs), exactly as the existing from-scratch DEFLATE (`src/runtime/io/rt_compress.c`), PNG/JPEG decoders, and BC7/ETC2/ASTC block decoders were. Decode-only, no dictionary support.

## 2. Current state (verified anchors)

- Parse flow: `textureasset3d_parse_ktx2` (`assets/rt_textureasset3d_ktx2.inc:15`); header fields read at fixed offsets (`:42-49`), **supercompression trap** at `:60-63` (`if (supercompression_scheme != 0) rt_trap(...)`); scheme values per KTX2 spec: 1 = BasisLZ (ETC1S), 2 = Zstd, 3 = ZLIB. The Data Format Descriptor / key-value / supercompressionGlobalData byte ranges (header bytes 48–79) are **never read** — format identity comes solely from `vkFormat`. UASTC files carry `vkFormat = 0` + DFD channel info, so **UASTC requires DFD parsing** (BasisLZ additionally requires sgd parsing for its global codebooks).
- Mip-level invariant that supercompression breaks: per-level `length == expected_bytes` and `uncompressed_length ∈ {0, expected}` enforcement (`_ktx2.inc:106-126`, with an explicit code comment saying the loader handles only non-supercompressed levels).
- Format table: `textureasset3d_format_from_vk` (`_core.inc:181-210`) — string-keyed `textureasset3d_format_info {name, compressed, block_w, block_h, block_bytes}`; rows: `"rgba8"`, `"bc3"`, `"bc7"`, `"etc2"`, `"astc"` (28-entry dims table). VkFormat macros at `rt_textureasset3d.c:62-71`. Native-id enum `rt_textureasset3d.h:25-29`: NONE=0, BC3=1, BC7=2, ASTC=3, ETC2=4; mapped in `rt_textureasset3d_get_native_format_id` (`_ktx2.inc:418-432`).
- Decoders (`_codecs.inc`): `rt_textureasset3d_decode_bc3_block` (`:37`, void), `_bc7_block` (`:506`, int, bit reader `bc7_get_bits_at:355`, mode table `bc7_mode_info BC7_MODES[8]:191`), `_etc2_rgba8_block` (`:757`), `_astc_ldr_block` (`:826`); generic mip driver `textureasset3d_decode_compressed_fallback/_mips` (`:923/:996`) with adapter fn-ptr type (`:854`). Both RGBA fallbacks (`mip_pixels`) and native payloads (`mip_payloads`) retained (`_core.inc:30-53`, copy `:565`).
- Native upload negotiation: `vgfx3d_textureasset_native_supported` (`backend/vgfx3d_backend_utils.c:97-110`) maps native-id → cap bit; cap bits `render/rt_canvas3d.h:165-190` (BC7=0x10000, ASTC=0x20000, ETC2=0x40000; **highest used 0x2000000, next free 0x4000000**). **BC3 has a native id but no cap bit and no backend upload — it always falls back to RGBA.** Backend mappers: D3D11 `d3d11_get_native_texture_caps/_native_texture_format` (`_d3d11_texture.inc:415/426`); GL `gl_get_native_texture_caps/gl_native_texture_internal_format` (`_opengl_texture.inc:364/377`, GL enums `_opengl.c:145-160`); Metal `metal_get_native_texture_caps/_native_texture_pixel_format` (`metal.m:3235/3253`).
- Inflate precedent to model Zstd on: `rt_compress_inflate_raw(data, len, max_output, &out, &out_len)` (`src/runtime/io/rt_compress.h:71`; FBX usage `rt_fbx_loader_parse.inc:609-653`); LSB-first streaming `bit_reader_t` (`rt_compress.c:181-260`: `br_init/br_fill/br_read/br_peek/br_consume/br_align`); Huffman direct-mapped table types (`rt_compress_internal.h:25-40`).
- Error model the KTX2 path should adopt: `rt_asset_error_{set,setf,add_warningf,begin_load,end_load_success,end_load_failure}` + codes NONE/NOT_FOUND/UNREADABLE/BAD_MAGIC/CORRUPT/UNSUPPORTED/TOO_LARGE (`assets/rt_asset_error.h:36-74`); Zia surface `Assets3D.GetLastLoadError*`; checker fallback `textureasset3d_make_decode_failure_checker(_mips)` (`_core.inc:510/526`). Today the KTX2 path has ~27 `rt_trap` sites (enumerated: `_ktx2.inc` :38,:53,:57,:61,:65,:70,:75,:81,:88,:103,:112,:123,:177,:191,:202,:233,:247,:252,:267,:272; `_core.inc` :339,:490,:513; `_codecs.inc` :113,:629,:963) and only one `add_warningf`.
- Fuzzing: 20 harnesses in `src/tests/fuzz/` (glTF/FBX/OBJ-STL among them), `LLVMFuzzerTestOneInput` + `viper_fuzz3d` helpers (`fuzz_3d_helpers.hpp`, 256 KB bound), CMake `viper_add_fuzzer`/`viper_add_3d_loader_fuzzer` gated on `VIPER_ENABLE_FUZZ` (`fuzz/CMakeLists.txt:6-82`), corpus `corpus/<name>/`, replay ctest label `fuzz`. **No KTX2 or image-decoder harness exists**, and the KTX2 loader trapping on malformed input makes it unfuzzable as-is.
- Image decoder fuzz targets (buffer APIs, `src/runtime/io/rt_asset_decode.c`): PNG `rt_png_decode_buffer_rgba32` (`:71`), JPEG `rt_jpeg_decode_buffer` (`:70`), GIF `rt_gif_decode_memory_first_rgba32` (`:76`). BMP is tempfile-only (`:519`) — optional.
- Test fixtures: `write_test_ktx2_custom_header(..., supercompression_scheme, ...)` (`src/tests/unit/test_rt_canvas3d.cpp:277`); hand-packed block test precedent (`:1737` ETC2/ASTC fixtures; BC7 mode tests nearby).

## 3. Design

### 3.1 Zstd decoder — `src/runtime/io/rt_zstd.{h,c}` (from scratch, RFC 8878, decode-only)

API mirrors the inflate shape:
```c
int rt_zstd_decompress_raw(const uint8_t *data, size_t len, size_t max_output,
                           uint8_t **out_data, size_t *out_len);  /* 1/0; caller free()s */
```
Components: frame header (magic 0xFD2FB528, window/dictionary checks — **reject dictionary frames**, KTX2 never uses them), block loop (raw/RLE/compressed), literals section (raw/RLE/Huffman — 1- and 4-stream), FSE table decoding (predefined + compressed distributions for literals-lengths/match-lengths/offsets), sequence execution with the three repeated-offset registers, optional xxhash64 checksum (implement — 30 lines — and validate when present). Reuse the streaming LSB-first `bit_reader_t` for the forward-read sections; Zstd's FSE/sequence bitstream is read *backward* — add a small `zstd_reverse_bit_reader` in `rt_zstd.c` (self-contained). Full Viper file header, no external references.

### 3.2 Basis Universal transcode (from scratch)

- **UASTC (priority 1):** UASTC LDR 4×4 is a constrained ASTC subset with 19 modes. Implement `rt_textureasset3d_decode_uastc_block(const uint8_t *block16, uint8_t *out_rgba)` in `_codecs.inc` following the BC7 decoder's structure (mode table + `bc7_get_bits_at`-style random-access reader). RGBA-decode only in v1: UASTC→RGBA8 fallback always; native re-encode to BC7 deferred (quality-lossless UASTC→BC7 requires the full mode-mapping tables — a later increment; RGBA path unblocks content *now*). UASTC identification: `vkFormat == 0` + DFD colorModel 166 (per Khronos DFD spec).
- **ETC1S/BasisLZ (priority 2):** requires sgd parsing (endpoint/selector codebooks, Huffman tables per the BasisU spec) then per-slice transcode to ETC1→RGBA8 (reuse the ETC2 decoder's ETC1 base path). Gate behind its own commit; UASTC ships first.
- New KTX2 flow: parse DFD (minimal: colorModel/transferFunction/channel count — enough for UASTC/sRGB identification); if `supercompression_scheme == 2`, Zstd-decompress each level payload to `uncompressed_length` then feed the existing per-format mip path; if `== 1`, parse sgd + ETC1S transcode; `== 3` (ZLIB) route to `rt_compress_inflate_raw` (nearly free). The `length == expected` check moves to *post-decompression*.

### 3.3 BCn coverage + native upload end-to-end

New decoders in `_codecs.inc` (signatures per `rt_textureasset3d.h:35-54` conventions):
- `rt_textureasset3d_decode_bc1_block` (8-byte block; 1-bit alpha mode when color0<=color1), block_bytes=8 — note the format table's first 8-byte row (existing rows are all 16).
- `rt_textureasset3d_decode_bc4_block` (8 bytes, single channel → R, G=B=R, A=255), `_bc5_block` (16 bytes, RG → normal-map convention R=X,G=Y,B=255,A=255; document Z-reconstruct in shader is NOT done — matches how BC5 normal data is authored for this engine).
- `rt_textureasset3d_decode_bc6h_block` (stretch): 14-mode HDR half-float; decode to RGBA8 via tonemap-free clamp for the fallback, and to float16 payload for plan 01's HDR env use. If cut, keep the format-table row returning graceful UNSUPPORTED (recoverable error, not trap).
- End-to-end per format: VkFormat macros (BC1_RGB/RGBA UNORM+SRGB 131-136 range, BC4 139/140, BC5 141/142, BC6H 143/144) → `format_from_vk` rows → native-id constants (BC1=5, BC4=6, BC5=7, BC6H=8) → cap bits (BC1=0x4000000, BC4=0x8000000, BC5=0x10000000, BC6H=0x20000000, **plus a new BC3 bit** since it's missing) → `vgfx3d_textureasset_native_supported` branches → backend mappers (D3D11 `DXGI_FORMAT_BC1/BC4/BC5/BC6H_*`; GL `GL_COMPRESSED_RGBA_S3TC_DXT1_EXT`/`GL_COMPRESSED_RED_RGTC1`/`GL_COMPRESSED_RG_RGTC2`/`GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT` macros added at `_opengl.c:145-160`; Metal `MTLPixelFormatBC1_RGBA`/`BC4_RUnorm`/`BC5_RGUnorm`/`BC6H_RGBUfloat`) with caps probes. Transcoded/decompressed levels upload their post-transform payloads (native retention only when the GPU format matches).

### 3.4 Trap→recoverable + fuzz enablement

- Convert all **malformed-input** traps in the KTX2 path to `rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT|UNSUPPORTED, ...) + return NULL` (the 20 `_ktx2.inc` sites); keep **allocation-failure** traps (`_core.inc:490,513`, `_codecs.inc:113,629,963`, `_ktx2.inc:88,177,191,202`) — OOM stays fatal, consistent with runtime policy. Wrap public entries in `rt_asset_error_begin_load/end_load_*` like the glTF loader.
- `rt_textureasset3d_load_ktx2_memory` (`_ktx2.inc:230`) becomes the non-trapping fuzz target once conversion lands.
- New harnesses: `fuzz_ktx2_loader.cpp` (calls `load_ktx2_memory`, frees result), `fuzz_image_decoders.cpp` (first byte selects PNG/JPEG/GIF target; frees pixel buffers). Register via `viper_add_3d_loader_fuzzer` / `viper_add_fuzzer`; seed corpora: minimal valid KTX2 per format (generated by the existing `write_test_ktx2*` fixture writers), tiny PNG/JPEG/GIF from existing test assets.

## 4. Implementation steps (each commit-sized, fail-before/pass-after)

1. Trap→recoverable conversion + `begin/end_load` scoping + tests (malformed header → NULL + `GetLastLoadError` populated; existing valid-file tests unchanged).
2. Fuzz harnesses + corpora + `VIPER_ENABLE_FUZZ` smoke run (fix anything the first hour of fuzzing shakes out of the *existing* decoders — likely).
3. BC1/BC4/BC5 decoders + format rows + hand-packed block unit tests.
4. Native upload end-to-end for BC1/BC3/BC4/BC5 (cap bits, negotiation, 3 backend mappers) + `native resident mips feed backend upload helpers`-style test extension.
5. `rt_zstd.c` decoder + exhaustive unit tests (RFC vectors: raw/RLE/compressed blocks, 1/4-stream Huffman literals, predefined + FSE-coded tables, repeat offsets, checksum) + fuzz harness `fuzz_zstd.cpp`.
6. KTX2 scheme-2 (Zstd) + scheme-3 (ZLIB) level decompression + DFD minimal parse + `write_test_ktx2_custom_header` round-trip tests + a real `toktx --zcmp` fixture checked into test data.
7. UASTC block decoder + KTX2 UASTC identification + tests (hand-packed blocks for the common modes + a real `toktx --uastc --zcmp` fixture).
8. ETC1S/BasisLZ (sgd parse + transcode) + tests with a real `toktx --bcmp`-era ETC1S fixture.
9. (Stretch) BC6H decode + float16 payload retention for plan 01.

## 5. Public API changes

None mandatory — existing `TextureAsset3D.LoadKTX2*` entry points gain format coverage transparently. Optional: `TextureAsset3D.get_FormatName` already exposes the format string; add `"bc1"/"bc4"/"bc5"/"uastc"` values to docs (`docs/viperlib/graphics/rendering3d.md` texture section). New files `src/runtime/io/rt_zstd.{h,c}` → `source_health_baseline.tsv` bump; runtime C ABI unchanged (internal helpers only) → no ADR unless `rt_zstd.h` is exported into the public runtime API surface (keep it internal).

## 6. Tests

- Given a KTX2 with scheme=2 and Zstd-compressed levels, When loaded, Then mips decode identical to the uncompressed twin (byte-compare `mip_pixels`).
- Given scheme=1/2/3 malformed payloads (truncated frame, bad FSE table, wrong checksum), When loaded, Then NULL + CORRUPT error, no trap, no leak (ASan lane).
- Hand-packed block tests per new decoder (BC1 opaque + punch-through alpha; BC4 ramp; BC5 two-channel; UASTC solid-color + common modes).
- Fuzz replay tests (`-runs=1`) green in the `fuzz` ctest label.
- Native negotiation: asset with BC5 + backend advertising the cap → native payload chosen; without → RGBA fallback (unit, using the utils helper directly).

## 7. Verification gates

Full build + full ctest + `-L slow`; `ctest -L fuzz` (with `VIPER_ENABLE_FUZZ=ON` configured locally at least once); a multi-hour background fuzz session on `fuzz_ktx2_loader` + `fuzz_zstd` before declaring done; real-toolchain round-trip (`toktx --zcmp --uastc` output loads and renders in `game3d_starter`). Metal + SW native-upload verification local; GL/D3D11 mapper code reviewed + waivered for Win/Linux.

## 8. Risks & constraints

- **Zero external dependencies (restated as a hard gate):** all of Zstd/UASTC/ETC1S/BCn are spec-implemented from scratch; if any piece proves unimplementable-with-confidence in budget (BC7-partition-table lesson: don't ship unverifiable tables), it degrades to a *graceful recoverable* UNSUPPORTED, never a trap, and never a vendored library.
- Zstd is the largest single item (~1.5–2K LOC with tests); FSE correctness is subtle — lean on RFC test vectors and fuzz early (step 5 before step 6 depends on it).
- ETC1S sgd format is the least-documented corner (BasisU spec + KTX2 spec appendix); schedule it last, ship UASTC value first.
- sRGB correctness: preserve the `_SRGB` variant handling the current loader has (same format name, sRGB-ness carried) across all new formats.
