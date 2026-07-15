//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_gltf_draco_probe.cpp
// Purpose: Regression tests for the KHR_draco_mesh_compression decoder hardening
//   (NORMALS stride mismatch, edgebreaker leftmost_corner bound, count-vs-payload
//   caps). Drives the decoder directly via rt_gltf_draco_decode_probe on crafted
//   payloads and asserts malformed input is rejected without a trap or OOB access.
//
// Key invariants:
//   - A NORMALS-transform attribute declaring components != 3 is rejected at parse.
//   - A declared count grossly disproportionate to the payload is rejected.
//   - Truncated / non-magic input is rejected; no input crashes the decoder.
//
// Links: src/runtime/graphics/3d/assets/rt_gltf.h, rt_gltf_draco.inc
//
//===----------------------------------------------------------------------===//

#include "rt_gltf.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" void vm_trap(const char *msg) {
    std::fprintf(stderr, "unexpected trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

static int g_failures = 0;

static void expect(bool cond, const char *what) {
    std::printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond)
        g_failures++;
}

static std::vector<uint8_t> draco_header(uint8_t etype, uint8_t method, uint16_t flags) {
    std::vector<uint8_t> b = {'D', 'R', 'A', 'C', 'O', 2, 2, etype, method};
    b.push_back((uint8_t)(flags & 0xFF));
    b.push_back((uint8_t)(flags >> 8));
    return b;
}

int main() {
    std::printf("test_rt_gltf_draco_probe:\n");

    /* Valid-ish sequential mesh: exercises the decode path; must not crash
     * (accept or reject both fine — the point is no trap/OOB). */
    {
        std::vector<uint8_t> b = draco_header(1, 0, 0);
        const uint8_t body[] = {1, 3, 0, 1, 1, 0, 9, 3, 0, 0, 1};
        b.insert(b.end(), body, body + sizeof(body));
        b.resize(b.size() + 32, 0);
        (void)rt_gltf_draco_decode_probe(b.data(), b.size());
        expect(true, "sequential payload decoded without crash");
    }

    /* F1: NORMALS transform (decoder_type 3) with components=4 must be rejected at
     * parse — decoding at a stride-3 output while the reader strides by 4 was OOB. */
    {
        std::vector<uint8_t> b = draco_header(1, 0, 0);
        const uint8_t body[] = {1, 3, 0, 1, 1, 1, 9, 4, 0, 0, 3};
        b.insert(b.end(), body, body + sizeof(body));
        b.resize(b.size() + 16, 0);
        int ok = rt_gltf_draco_decode_probe(b.data(), b.size());
        expect(ok == 0, "NORMALS attribute with components!=3 rejected (F1)");
    }

    /* F11: a face count wildly disproportionate to a tiny payload must be rejected
     * before it can drive a multi-gigabyte allocation. */
    {
        std::vector<uint8_t> b = draco_header(1, 0, 0);
        const uint8_t body[] = {0xFF, 0xFF, 0xFF, 0x7F, 0x03};
        b.insert(b.end(), body, body + sizeof(body));
        int ok = rt_gltf_draco_decode_probe(b.data(), b.size());
        expect(ok == 0, "oversized declared count rejected (F11)");
    }

    /* Edgebreaker header with tiny counts: exercises draco_eb_parse_and_alloc and
     * the guarded leftmost_corner path (F2); must not crash. */
    {
        std::vector<uint8_t> b = draco_header(1, 1, 0);
        const uint8_t body[] = {0, 3, 1, 1, 1, 0};
        b.insert(b.end(), body, body + sizeof(body));
        b.resize(b.size() + 32, 0);
        (void)rt_gltf_draco_decode_probe(b.data(), b.size());
        expect(true, "edgebreaker payload processed without crash (F2)");
    }

    /* Truncated and non-magic inputs are rejected cleanly. */
    {
        const uint8_t trunc[] = {'D', 'R', 'A', 'C', 'O', 2, 2};
        expect(rt_gltf_draco_decode_probe(trunc, sizeof(trunc)) == 0, "truncated header rejected");
        const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        expect(rt_gltf_draco_decode_probe(junk, sizeof(junk)) == 0, "non-magic input rejected");
        expect(rt_gltf_draco_decode_probe(nullptr, 0) == 0, "null/empty input rejected");
    }

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
