/**
 * @file tls_smoke.c
 * @brief Smoke test for user-space TLS library.
 *
 * Tests basic TLS functionality by attempting a connection.
 */

#include <stdio.h>
#include <string.h>
#include <tls.h>

int main(void) {
    printf("TLS smoke test starting...\n");

    /* Test 1: Config initialization */
    tls_config_t config;
    tls_config_init(&config);
    if (config.verify_cert != 1) {
        printf("FAIL: config init failed\n");
        return 1;
    }
    printf("PASS: TLS config initialization\n");

    /* Test 2: Session creation (no actual connection) */
    /* We can't actually connect without a TLS server, but we can
       test the API doesn't crash with invalid fd */
    config.hostname = "example.com";
    config.verify_cert = 0;

    /* Note: tls_new with invalid fd won't fail immediately,
       failure happens at handshake */
    tls_session_t *session = tls_new(-1, &config);
    if (!session) {
        printf("FAIL: tls_new returned NULL\n");
        return 1;
    }
    printf("PASS: TLS session creation\n");

    /* Test 3: Get info on unconnected session */
    tls_info_t info;
    int rc = tls_get_info(session, &info);
    if (rc != 0) {
        printf("FAIL: tls_get_info failed\n");
        tls_close(session);
        return 1;
    }
    if (info.connected != 0) {
        printf("FAIL: unconnected session reports connected\n");
        tls_close(session);
        return 1;
    }
    printf("PASS: TLS get_info on unconnected session\n");

    /* Test 4: Get error message */
    const char *err = tls_get_error(session);
    if (!err) {
        printf("FAIL: tls_get_error returned NULL\n");
        tls_close(session);
        return 1;
    }
    printf("PASS: TLS get_error\n");

    /* Clean up */
    tls_close(session);

    printf("\n=== TLS smoke test PASSED ===\n");
    return 0;
}
