cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED VIPER_SOURCE_DIR)
    message(FATAL_ERROR "VIPER_SOURCE_DIR must be provided to TlsSignaturePolicyTest.cmake")
endif ()

set(_tls_client "${VIPER_SOURCE_DIR}/src/runtime/network/rt_tls.c")
# Certificate verification was split by platform; scan all of its translation units.
set(_tls_verify_common "${VIPER_SOURCE_DIR}/src/runtime/network/rt_tls_verify_common.c")
set(_tls_verify_win "${VIPER_SOURCE_DIR}/src/runtime/network/rt_tls_verify_win.c")
set(_tls_verify_posix "${VIPER_SOURCE_DIR}/src/runtime/network/rt_tls_verify_posix.c")

file(READ "${_tls_client}" _tls_client_text)
file(READ "${_tls_verify_common}" _tls_verify_common_text)
file(READ "${_tls_verify_win}" _tls_verify_win_text)
file(READ "${_tls_verify_posix}" _tls_verify_posix_text)
set(_tls_verify_text "${_tls_verify_common_text}${_tls_verify_win_text}${_tls_verify_posix_text}")

if (_tls_client_text MATCHES "0x0503")
    message(FATAL_ERROR "ClientHello still advertises ecdsa_secp384r1_sha384 (0x0503)")
endif ()

if (_tls_verify_text MATCHES "case[ \t]+0x0503")
    message(FATAL_ERROR "CertificateVerify still treats ecdsa_secp384r1_sha384 as implemented")
endif ()

message(STATUS "TLS signature policy excludes unimplemented P-384 ECDSA")
