cmake_minimum_required(VERSION 3.20)

if (NOT DEFINED VIPER_SOURCE_DIR)
    message(FATAL_ERROR "VIPER_SOURCE_DIR must be provided to TlsSignaturePolicyTest.cmake")
endif ()

set(_tls_client "${VIPER_SOURCE_DIR}/src/runtime/network/rt_tls.c")
set(_tls_verify "${VIPER_SOURCE_DIR}/src/runtime/network/rt_tls_verify.c")

file(READ "${_tls_client}" _tls_client_text)
file(READ "${_tls_verify}" _tls_verify_text)

if (_tls_client_text MATCHES "0x0503")
    message(FATAL_ERROR "ClientHello still advertises ecdsa_secp384r1_sha384 (0x0503)")
endif ()

if (_tls_verify_text MATCHES "case[ \t]+0x0503")
    message(FATAL_ERROR "CertificateVerify still treats ecdsa_secp384r1_sha384 as implemented")
endif ()

message(STATUS "TLS signature policy excludes unimplemented P-384 ECDSA")
