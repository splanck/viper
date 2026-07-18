//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/common/TlsServerFixtureData.hpp
// Purpose: Provide a deterministic localhost certificate/key pair for focused
//          in-tree TLS server tests without invoking external tooling.
// Key invariants:
//   - The certificate covers both `localhost` and IPv4 loopback.
//   - Fixture bytes are immutable and contain no product runtime dependency.
//   - Tests disable trust verification unless they explicitly install a CA.
// Ownership/Lifetime:
//   - Inline constants have static storage and are never dynamically owned.
// Links: src/runtime/network/rt_tls_server_internal.h,
//        src/runtime/network/rt_https_server.h
//
//===----------------------------------------------------------------------===//
#pragma once

namespace zanna::tests {

/// @brief PEM-encoded P-256 private key matching @ref kLocalhostEcCertificatePem.
/// @details The key is test-only, checked into source intentionally, and must
///          never be used for a deployed endpoint. Focused tests write these
///          bytes to a unique temporary file before constructing a TLS server.
inline constexpr char kLocalhostEcPrivateKeyPem[] = R"PEM(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg+Z1xhQRSU9+jKQhH
9R9DeB1DObDrQG6uuJYh2fGU/gOhRANCAATfYC4JF5vgz0f005FgdcIvzq+XWoK2
WkHv9ylmizkXwiiwONBMUiHLJp0aQ5prsy/qG1qvxIA+EemN8nsM73O/
-----END PRIVATE KEY-----
)PEM";

/// @brief PEM-encoded localhost certificate matching @ref kLocalhostEcPrivateKeyPem.
/// @details The self-signed P-256 certificate includes DNS `localhost` and IP
///          `127.0.0.1` subject-alternative names. Its long-lived validity is
///          deliberate so deterministic local integration tests do not depend
///          on certificate generation commands or wall-clock fixture renewal.
inline constexpr char kLocalhostEcCertificatePem[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIBmTCCAT+gAwIBAgIUMx/aHjSr1BLKVJWLkjEW8tVBwEwwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDQxOTAwNDY0OVoXDTM2MDQxNjAw
NDY0OVowFDESMBAGA1UEAwwJbG9jYWxob3N0MFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAE32AuCReb4M9H9NORYHXCL86vl1qCtlpB7/cpZos5F8IosDjQTFIhyyad
GkOaa7Mv6htar8SAPhHpjfJ7DO9zv6NvMG0wHQYDVR0OBBYEFH8rprP1CxiHqLBg
7tp3in6Op8rZMB8GA1UdIwQYMBaAFH8rprP1CxiHqLBg7tp3in6Op8rZMA8GA1Ud
EwEB/wQFMAMBAf8wGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMAoGCCqGSM49
BAMCA0gAMEUCICfrWIQjaBKJOeHsEFydx3kmB3xZA27GVaokzpkBKShNAiEApv2B
ptOACq7G5MbeXCED94+Klf9Txx0gZ+qg8GckbdA=
-----END CERTIFICATE-----
)PEM";

} // namespace zanna::tests
