/**
 * @file ssh_auth.c
 * @brief SSH authentication implementation (RFC 4252).
 */

#include "include/ssh.h"
#include "ssh_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Authentication
 *===========================================================================*/

int ssh_get_auth_methods(ssh_session_t *session) {
    /* Try "none" auth to get list of available methods */
    int rc = ssh_auth_none(session);
    if (rc == SSH_OK) {
        return 0; /* No authentication needed */
    }

    /* For now, assume password and publickey are available */
    return SSH_AUTH_PASSWORD | SSH_AUTH_PUBLICKEY;
}

int ssh_auth_none(ssh_session_t *session) {
    if (!session || !session->username)
        return SSH_ERROR;

    uint8_t payload[512];
    size_t pos = 0;

    /* username */
    size_t user_len = strlen(session->username);
    ssh_buf_write_u32(payload + pos, user_len);
    memcpy(payload + pos + 4, session->username, user_len);
    pos += 4 + user_len;

    /* service name */
    const char *service = "ssh-connection";
    size_t service_len = strlen(service);
    ssh_buf_write_u32(payload + pos, service_len);
    memcpy(payload + pos + 4, service, service_len);
    pos += 4 + service_len;

    /* method name */
    const char *method = "none";
    size_t method_len = strlen(method);
    ssh_buf_write_u32(payload + pos, method_len);
    memcpy(payload + pos + 4, method, method_len);
    pos += 4 + method_len;

    int rc = ssh_packet_send(session, SSH_MSG_USERAUTH_REQUEST, payload, pos);
    if (rc < 0)
        return rc;

    /* Wait for response */
    uint8_t response[1024];
    size_t response_len;
    uint8_t msg_type;

    while (1) {
        rc = ssh_packet_recv(session, &msg_type, response, &response_len);
        if (rc == SSH_AGAIN) {
            extern long __syscall1(long, long);
            __syscall1(0x31 /* SYS_YIELD */, 0);
            continue;
        }
        if (rc < 0)
            return rc;
        break;
    }

    if (msg_type == SSH_MSG_USERAUTH_SUCCESS) {
        session->state = SSH_STATE_AUTHENTICATED;
        return SSH_OK;
    }

    return SSH_AUTH_DENIED;
}

int ssh_auth_password(ssh_session_t *session, const char *password) {
    if (!session || !session->username || !password)
        return SSH_ERROR;

    if (session->verbose >= 1) {
        printf(
            "[ssh] Password auth: user='%s' pass_len=%zu\n", session->username, strlen(password));
    }

    uint8_t payload[1024];
    size_t pos = 0;

    /* username */
    size_t user_len = strlen(session->username);
    ssh_buf_write_u32(payload + pos, user_len);
    memcpy(payload + pos + 4, session->username, user_len);
    pos += 4 + user_len;

    /* service name */
    const char *service = "ssh-connection";
    size_t service_len = strlen(service);
    ssh_buf_write_u32(payload + pos, service_len);
    memcpy(payload + pos + 4, service, service_len);
    pos += 4 + service_len;

    /* method name */
    const char *method = "password";
    size_t method_len = strlen(method);
    ssh_buf_write_u32(payload + pos, method_len);
    memcpy(payload + pos + 4, method, method_len);
    pos += 4 + method_len;

    /* boolean FALSE (no password change) */
    payload[pos++] = 0;

    /* password */
    size_t pass_len = strlen(password);
    ssh_buf_write_u32(payload + pos, pass_len);
    memcpy(payload + pos + 4, password, pass_len);
    pos += 4 + pass_len;

    int rc = ssh_packet_send(session, SSH_MSG_USERAUTH_REQUEST, payload, pos);
    if (rc < 0)
        return rc;

    /* Wait for response */
    uint8_t response[1024];
    size_t response_len;
    uint8_t msg_type;

    fprintf(stderr, "[ssh-auth] Waiting for auth response...\n");
    int wait_count = 0;
    while (1) {
        rc = ssh_packet_recv(session, &msg_type, response, &response_len);
        if (rc == SSH_AGAIN) {
            /* No data yet, yield and retry */
            extern long __syscall1(long, long);
            __syscall1(0x31 /* SYS_YIELD */, 0);
            wait_count++;
            if (wait_count % 10000 == 0) {
                fprintf(stderr, "[ssh-auth] Still waiting... (count=%d)\n", wait_count);
            }
            continue;
        }
        if (rc < 0) {
            fprintf(stderr, "[ssh-auth] ssh_packet_recv returned error %d\n", rc);
            return rc;
        }

        fprintf(stderr, "[ssh-auth] Received msg_type=%d, len=%zu\n", msg_type, response_len);

        if (msg_type == SSH_MSG_USERAUTH_SUCCESS) {
            session->state = SSH_STATE_AUTHENTICATED;
            return SSH_OK;
        }

        if (msg_type == SSH_MSG_USERAUTH_FAILURE) {
            /* Parse failure message to show available methods */
            if (response_len >= 4 && session->verbose >= 1) {
                uint32_t methods_len = ssh_buf_read_u32(response);
                if (methods_len < response_len) {
                    char methods[256];
                    size_t copy_len = methods_len < 255 ? methods_len : 255;
                    memcpy(methods, response + 4, copy_len);
                    methods[copy_len] = '\0';
                    printf("[ssh] Server auth methods: %s\n", methods);
                }
            }
            return SSH_AUTH_DENIED;
        }

        if (msg_type == SSH_MSG_USERAUTH_BANNER) {
            /* Just skip banner messages */
            continue;
        }

        /* Unexpected message */
        return SSH_PROTOCOL_ERROR;
    }
}

int ssh_auth_try_publickey(ssh_session_t *session, ssh_key_t *key) {
    if (!session || !session->username || !key)
        return SSH_ERROR;

    uint8_t payload[2048];
    size_t pos = 0;

    /* username */
    size_t user_len = strlen(session->username);
    ssh_buf_write_u32(payload + pos, user_len);
    memcpy(payload + pos + 4, session->username, user_len);
    pos += 4 + user_len;

    /* service name */
    const char *service = "ssh-connection";
    size_t service_len = strlen(service);
    ssh_buf_write_u32(payload + pos, service_len);
    memcpy(payload + pos + 4, service, service_len);
    pos += 4 + service_len;

    /* method name */
    const char *method = "publickey";
    size_t method_len = strlen(method);
    ssh_buf_write_u32(payload + pos, method_len);
    memcpy(payload + pos + 4, method, method_len);
    pos += 4 + method_len;

    /* boolean FALSE (just checking, not signing) */
    payload[pos++] = 0;

    /* public key algorithm name */
    const char *alg_name;
    if (key->type == SSH_KEYTYPE_ED25519) {
        alg_name = "ssh-ed25519";
    } else if (key->type == SSH_KEYTYPE_RSA) {
        alg_name = "ssh-rsa";
    } else {
        return SSH_ERROR;
    }
    size_t alg_len = strlen(alg_name);
    ssh_buf_write_u32(payload + pos, alg_len);
    memcpy(payload + pos + 4, alg_name, alg_len);
    pos += 4 + alg_len;

    /* public key blob */
    uint8_t pubkey_blob[1024];
    size_t pubkey_len = sizeof(pubkey_blob);
    int rc = ssh_key_get_public_blob(key, pubkey_blob, &pubkey_len);
    if (rc < 0)
        return rc;

    ssh_buf_write_u32(payload + pos, pubkey_len);
    memcpy(payload + pos + 4, pubkey_blob, pubkey_len);
    pos += 4 + pubkey_len;

    rc = ssh_packet_send(session, SSH_MSG_USERAUTH_REQUEST, payload, pos);
    if (rc < 0)
        return rc;

    /* Wait for response */
    uint8_t response[1024];
    size_t response_len;
    uint8_t msg_type;

    while (1) {
        rc = ssh_packet_recv(session, &msg_type, response, &response_len);
        if (rc == SSH_AGAIN) {
            extern long __syscall1(long, long);
            __syscall1(0x31 /* SYS_YIELD */, 0);
            continue;
        }
        if (rc < 0)
            return rc;
        break;
    }

    if (msg_type == SSH_MSG_USERAUTH_PK_OK) {
        return SSH_OK; /* Key is acceptable */
    }

    if (msg_type == SSH_MSG_USERAUTH_FAILURE) {
        return SSH_AUTH_DENIED;
    }

    return SSH_PROTOCOL_ERROR;
}

int ssh_auth_publickey(ssh_session_t *session, ssh_key_t *key) {
    if (!session || !session->username || !key || !key->has_private) {
        return SSH_ERROR;
    }

    uint8_t payload[4096];
    size_t pos = 0;

    /* username */
    size_t user_len = strlen(session->username);
    ssh_buf_write_u32(payload + pos, user_len);
    memcpy(payload + pos + 4, session->username, user_len);
    pos += 4 + user_len;

    /* service name */
    const char *service = "ssh-connection";
    size_t service_len = strlen(service);
    ssh_buf_write_u32(payload + pos, service_len);
    memcpy(payload + pos + 4, service, service_len);
    pos += 4 + service_len;

    /* method name */
    const char *method = "publickey";
    size_t method_len = strlen(method);
    ssh_buf_write_u32(payload + pos, method_len);
    memcpy(payload + pos + 4, method, method_len);
    pos += 4 + method_len;

    /* boolean TRUE (signing) */
    payload[pos++] = 1;

    /* public key algorithm name */
    const char *alg_name;
    if (key->type == SSH_KEYTYPE_ED25519) {
        alg_name = "ssh-ed25519";
    } else if (key->type == SSH_KEYTYPE_RSA) {
        alg_name = "ssh-rsa";
    } else {
        return SSH_ERROR;
    }
    size_t alg_len = strlen(alg_name);
    ssh_buf_write_u32(payload + pos, alg_len);
    memcpy(payload + pos + 4, alg_name, alg_len);
    pos += 4 + alg_len;

    /* public key blob */
    uint8_t pubkey_blob[1024];
    size_t pubkey_len = sizeof(pubkey_blob);
    int rc = ssh_key_get_public_blob(key, pubkey_blob, &pubkey_len);
    if (rc < 0)
        return rc;

    ssh_buf_write_u32(payload + pos, pubkey_len);
    memcpy(payload + pos + 4, pubkey_blob, pubkey_len);
    pos += 4 + pubkey_len;

    /* Build data to sign:
     * string session_id
     * byte SSH_MSG_USERAUTH_REQUEST
     * string username
     * string service
     * string "publickey"
     * boolean TRUE
     * string algorithm
     * string pubkey_blob
     */
    uint8_t sign_data[4096];
    size_t sign_pos = 0;

    /* session_id */
    ssh_buf_write_u32(sign_data + sign_pos, session->keys.session_id_len);
    memcpy(sign_data + sign_pos + 4, session->keys.session_id, session->keys.session_id_len);
    sign_pos += 4 + session->keys.session_id_len;

    /* message type */
    sign_data[sign_pos++] = SSH_MSG_USERAUTH_REQUEST;

    /* Copy rest of payload (already built) */
    memcpy(sign_data + sign_pos, payload, pos);
    sign_pos += pos;

    /* Sign the data */
    uint8_t signature[512];
    size_t sig_len = 0;

    if (key->type == SSH_KEYTYPE_ED25519) {
        ssh_ed25519_sign(key->key.ed25519.secret_key, sign_data, sign_pos, signature);
        sig_len = 64;
    } else if (key->type == SSH_KEYTYPE_RSA) {
        if (!ssh_rsa_sign(key, sign_data, sign_pos, signature, &sig_len)) {
            return SSH_ERROR;
        }
    }

    /* Build signature blob: string algorithm || string signature */
    uint8_t sig_blob[1024];
    size_t sig_blob_len = 0;

    ssh_buf_write_u32(sig_blob + sig_blob_len, alg_len);
    memcpy(sig_blob + sig_blob_len + 4, alg_name, alg_len);
    sig_blob_len += 4 + alg_len;

    ssh_buf_write_u32(sig_blob + sig_blob_len, sig_len);
    memcpy(sig_blob + sig_blob_len + 4, signature, sig_len);
    sig_blob_len += 4 + sig_len;

    /* Append signature blob to payload */
    ssh_buf_write_u32(payload + pos, sig_blob_len);
    memcpy(payload + pos + 4, sig_blob, sig_blob_len);
    pos += 4 + sig_blob_len;

    rc = ssh_packet_send(session, SSH_MSG_USERAUTH_REQUEST, payload, pos);
    if (rc < 0)
        return rc;

    /* Wait for response */
    uint8_t response[1024];
    size_t response_len;
    uint8_t msg_type;

    while (1) {
        rc = ssh_packet_recv(session, &msg_type, response, &response_len);
        if (rc == SSH_AGAIN) {
            extern long __syscall1(long, long);
            __syscall1(0x31 /* SYS_YIELD */, 0);
            continue;
        }
        if (rc < 0)
            return rc;

        if (msg_type == SSH_MSG_USERAUTH_SUCCESS) {
            session->state = SSH_STATE_AUTHENTICATED;
            return SSH_OK;
        }

        if (msg_type == SSH_MSG_USERAUTH_FAILURE) {
            return SSH_AUTH_DENIED;
        }

        if (msg_type == SSH_MSG_USERAUTH_BANNER) {
            continue;
        }

        return SSH_PROTOCOL_ERROR;
    }
}

/*=============================================================================
 * Key Management
 *===========================================================================*/

ssh_key_t *ssh_key_load(const char *filename, const char *passphrase) {
    (void)passphrase;

    /* Open and read file */
    FILE *f = fopen(filename, "r");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 16384) {
        fclose(f);
        return NULL;
    }

    /* Allocate one extra byte so text formats can be safely NUL-terminated. */
    uint8_t *data = malloc(size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t nread = fread(data, 1, size, f);
    fclose(f);

    if (nread != (size_t)size) {
        free(data);
        return NULL;
    }

    data[size] = '\0';

    ssh_key_t *key = ssh_key_load_mem(data, nread, passphrase);
    free(data);
    return key;
}

/* Base64 decode helper */
static int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max) {
    size_t out_len = 0;
    uint32_t buf = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len && out_len < out_max; i++) {
        int v = base64_decode_char(in[i]);
        if (v < 0) {
            if (in[i] == '=' || in[i] == '\n' || in[i] == '\r')
                continue;
            break;
        }
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[out_len++] = (buf >> bits) & 0xFF;
        }
    }
    return out_len;
}

ssh_key_t *ssh_key_load_mem(const uint8_t *data, size_t len, const char *passphrase) {
    (void)passphrase;

    ssh_key_t *key = calloc(1, sizeof(ssh_key_t));
    if (!key)
        return NULL;

    /* Check for OpenSSH private key format */
    const char *openssh_header = "-----BEGIN OPENSSH PRIVATE KEY-----";
    const char *openssh_footer = "-----END OPENSSH PRIVATE KEY-----";

    if (len > strlen(openssh_header) && memcmp(data, openssh_header, strlen(openssh_header)) == 0) {
        /* Parse OpenSSH format */
        const char *start = (const char *)data + strlen(openssh_header);
        const char *end = strstr(start, openssh_footer);
        if (!end) {
            free(key);
            return NULL;
        }

        /* Base64 decode */
        uint8_t decoded[4096];
        size_t decoded_len = base64_decode(start, end - start, decoded, sizeof(decoded));

        /* Parse openssh-key-v1 format */
        if (decoded_len < 15 || memcmp(decoded, "openssh-key-v1", 14) != 0) {
            free(key);
            return NULL;
        }

        size_t pos = 15; /* After null terminator */

        /* cipher name */
        if (pos + 4 > decoded_len) {
            free(key);
            return NULL;
        }
        uint32_t cipher_len = ssh_buf_read_u32(decoded + pos);
        pos += 4 + cipher_len;

        /* kdf name */
        if (pos + 4 > decoded_len) {
            free(key);
            return NULL;
        }
        uint32_t kdf_len = ssh_buf_read_u32(decoded + pos);
        pos += 4 + kdf_len;

        /* kdf options */
        if (pos + 4 > decoded_len) {
            free(key);
            return NULL;
        }
        uint32_t kdf_opts_len = ssh_buf_read_u32(decoded + pos);
        pos += 4 + kdf_opts_len;

        /* number of keys */
        if (pos + 4 > decoded_len) {
            free(key);
            return NULL;
        }
        uint32_t num_keys = ssh_buf_read_u32(decoded + pos);
        pos += 4;

        if (num_keys != 1) {
            free(key);
            return NULL;
        }

        /* public key blob (skip) */
        if (pos + 4 > decoded_len) {
            free(key);
            return NULL;
        }
        uint32_t pubkey_len = ssh_buf_read_u32(decoded + pos);
        pos += 4 + pubkey_len;

        /* private key blob */
        if (pos + 4 > decoded_len) {
            free(key);
            return NULL;
        }
        uint32_t privkey_len = ssh_buf_read_u32(decoded + pos);
        pos += 4;

        if (pos + privkey_len > decoded_len) {
            free(key);
            return NULL;
        }
        uint8_t *privkey = decoded + pos;

        /* Parse private key blob */
        /* checkint (2x uint32) */
        if (privkey_len < 8) {
            free(key);
            return NULL;
        }
        uint32_t check1 = ssh_buf_read_u32(privkey);
        uint32_t check2 = ssh_buf_read_u32(privkey + 4);
        if (check1 != check2) {
            free(key);
            return NULL; /* Incorrect passphrase or corrupted */
        }

        size_t priv_pos = 8;

        /* key type */
        if (priv_pos + 4 > privkey_len) {
            free(key);
            return NULL;
        }
        uint32_t type_len = ssh_buf_read_u32(privkey + priv_pos);
        priv_pos += 4;

        if (priv_pos + type_len > privkey_len) {
            free(key);
            return NULL;
        }

        if (type_len == 11 && memcmp(privkey + priv_pos, "ssh-ed25519", 11) == 0) {
            key->type = SSH_KEYTYPE_ED25519;
            priv_pos += type_len;

            /* public key (32 bytes, with length prefix) */
            if (priv_pos + 4 > privkey_len) {
                free(key);
                return NULL;
            }
            uint32_t pub_len = ssh_buf_read_u32(privkey + priv_pos);
            priv_pos += 4;
            if (pub_len != 32 || priv_pos + 32 > privkey_len) {
                free(key);
                return NULL;
            }
            memcpy(key->key.ed25519.public_key, privkey + priv_pos, 32);
            priv_pos += 32;

            /* secret key (64 bytes: seed || public) */
            if (priv_pos + 4 > privkey_len) {
                free(key);
                return NULL;
            }
            uint32_t sec_len = ssh_buf_read_u32(privkey + priv_pos);
            priv_pos += 4;
            if (sec_len != 64 || priv_pos + 64 > privkey_len) {
                free(key);
                return NULL;
            }
            memcpy(key->key.ed25519.secret_key, privkey + priv_pos, 64);

            key->has_private = true;
        } else if (type_len == 7 && memcmp(privkey + priv_pos, "ssh-rsa", 7) == 0) {
            key->type = SSH_KEYTYPE_RSA;
            priv_pos += type_len;

            /* n (modulus) */
            if (priv_pos + 4 > privkey_len) {
                free(key);
                return NULL;
            }
            uint32_t n_len = ssh_buf_read_u32(privkey + priv_pos);
            priv_pos += 4;
            if (n_len > 512 || priv_pos + n_len > privkey_len) {
                free(key);
                return NULL;
            }
            /* Skip leading zero */
            uint8_t *n_data = privkey + priv_pos;
            if (n_len > 0 && n_data[0] == 0) {
                n_data++;
                n_len--;
            }
            memcpy(key->key.rsa.modulus, n_data, n_len);
            key->key.rsa.modulus_len = n_len;
            priv_pos += ssh_buf_read_u32(privkey + priv_pos - 4); /* original length */

            /* e (public exponent) */
            if (priv_pos + 4 > privkey_len) {
                free(key);
                return NULL;
            }
            uint32_t e_len = ssh_buf_read_u32(privkey + priv_pos);
            priv_pos += 4;
            if (e_len > 8 || priv_pos + e_len > privkey_len) {
                free(key);
                return NULL;
            }
            memcpy(key->key.rsa.public_exp, privkey + priv_pos, e_len);
            key->key.rsa.public_exp_len = e_len;
            priv_pos += e_len;

            /* d (private exponent) */
            if (priv_pos + 4 > privkey_len) {
                free(key);
                return NULL;
            }
            uint32_t d_len = ssh_buf_read_u32(privkey + priv_pos);
            priv_pos += 4;
            if (d_len > 512 || priv_pos + d_len > privkey_len) {
                free(key);
                return NULL;
            }
            uint8_t *d_data = privkey + priv_pos;
            if (d_len > 0 && d_data[0] == 0) {
                d_data++;
                d_len--;
            }
            memcpy(key->key.rsa.private_exp, d_data, d_len);
            key->key.rsa.private_exp_len = d_len;

            key->has_private = true;
        } else {
            free(key);
            return NULL;
        }

        return key;
    }

    /* Unsupported format */
    free(key);
    return NULL;
}

void ssh_key_free(ssh_key_t *key) {
    if (!key)
        return;
    /* Clear sensitive data */
    memset(key, 0, sizeof(*key));
    free(key);
}

ssh_keytype_t ssh_key_type(ssh_key_t *key) {
    return key ? key->type : SSH_KEYTYPE_UNKNOWN;
}

int ssh_key_get_public_blob(ssh_key_t *key, uint8_t *blob, size_t *len) {
    if (!key || !blob || !len)
        return SSH_ERROR;

    size_t pos = 0;

    if (key->type == SSH_KEYTYPE_ED25519) {
        /* Format: string "ssh-ed25519" || string pubkey */
        const char *alg = "ssh-ed25519";
        size_t alg_len = strlen(alg);

        if (*len < 4 + alg_len + 4 + 32)
            return SSH_ERROR;

        ssh_buf_write_u32(blob + pos, alg_len);
        memcpy(blob + pos + 4, alg, alg_len);
        pos += 4 + alg_len;

        ssh_buf_write_u32(blob + pos, 32);
        memcpy(blob + pos + 4, key->key.ed25519.public_key, 32);
        pos += 4 + 32;

        *len = pos;
        return SSH_OK;
    } else if (key->type == SSH_KEYTYPE_RSA) {
        /* Format: string "ssh-rsa" || mpint e || mpint n */
        const char *alg = "ssh-rsa";
        size_t alg_len = strlen(alg);

        ssh_buf_write_u32(blob + pos, alg_len);
        memcpy(blob + pos + 4, alg, alg_len);
        pos += 4 + alg_len;

        /* e (public exponent) */
        ssh_buf_write_u32(blob + pos, key->key.rsa.public_exp_len);
        memcpy(blob + pos + 4, key->key.rsa.public_exp, key->key.rsa.public_exp_len);
        pos += 4 + key->key.rsa.public_exp_len;

        /* n (modulus) - may need leading zero */
        int need_zero = (key->key.rsa.modulus[0] & 0x80) ? 1 : 0;
        ssh_buf_write_u32(blob + pos, key->key.rsa.modulus_len + need_zero);
        if (need_zero)
            blob[pos + 4] = 0;
        memcpy(blob + pos + 4 + need_zero, key->key.rsa.modulus, key->key.rsa.modulus_len);
        pos += 4 + key->key.rsa.modulus_len + need_zero;

        *len = pos;
        return SSH_OK;
    }

    return SSH_ERROR;
}
