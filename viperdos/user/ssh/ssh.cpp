/**
 * @file ssh.c
 * @brief SSH client for ViperDOS.
 *
 * Simple SSH client that connects to a server and provides an interactive
 * shell or executes a command.
 *
 * Usage: ssh [-p port] [-i identity] user@host [command]
 */

#include <errno.h>
#include <poll.h>
#include <ssh.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* Terminal state */
static struct termios orig_termios;
static int raw_mode = 0;

static void disable_raw_mode(void) {
    if (raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode = 0;
    }
}

static void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
        return;
    }

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        return;
    }

    raw_mode = 1;
    atexit(disable_raw_mode);
}

static int hostkey_callback(ssh_session_t *session,
                            const char *hostname,
                            const uint8_t *key,
                            size_t key_len,
                            ssh_keytype_t keytype,
                            void *userdata) {
    (void)session;
    (void)key;
    (void)key_len;
    (void)userdata;

    const char *type_str = "unknown";
    if (keytype == SSH_KEYTYPE_ED25519)
        type_str = "ED25519";
    else if (keytype == SSH_KEYTYPE_RSA)
        type_str = "RSA";

    printf("Host '%s' presents %s key.\n", hostname, type_str);
    printf("Auto-accepting host key for testing.\n");

    /* TODO: Implement proper host key verification with stdin reading */
    return 0; /* Accept */
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-p port] [-i identity] [-l user] host [command]\n", prog);
    fprintf(stderr, "       %s [-p port] [-i identity] user@host [command]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -p port      Connect to specified port (default 22)\n");
    fprintf(stderr, "  -i identity  Use identity file for public key authentication\n");
    fprintf(stderr, "  -l user      Login as specified user\n");
    fprintf(stderr, "  -v, -vv      Verbose mode (use -vv for packet-level tracing)\n");
}

int main(int argc, char *argv[]) {
    const char *hostname = NULL;
    const char *username = NULL;
    const char *identity = NULL;
    const char *command = NULL;
    uint16_t port = 22;
    int verbose = 0;
    int opt;

    /* Parse options */
    while ((opt = getopt(argc, argv, "p:i:l:vh")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'i':
                identity = optarg;
                break;
            case 'l':
                username = optarg;
                break;
            case 'v':
                verbose++;
                break;
            case 'h':
            default:
                usage(argv[0]);
                return 1;
        }
    }

    /* Get hostname (and possibly user@host) */
    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    char *hostarg = argv[optind++];

    /* Parse user@host */
    char *at = strchr(hostarg, '@');
    if (at) {
        *at = '\0';
        username = hostarg;
        hostname = at + 1;
    } else {
        hostname = hostarg;
    }

    /* Get command if specified */
    if (optind < argc) {
        /* Build command from remaining args */
        static char cmdbuf[4096];
        cmdbuf[0] = '\0';
        for (int i = optind; i < argc; i++) {
            if (i > optind)
                strcat(cmdbuf, " ");
            strcat(cmdbuf, argv[i]);
        }
        command = cmdbuf;
    }

    /* Get username if not specified */
    if (!username) {
        username = getenv("USER");
        if (!username)
            username = "root";
    }

    if (verbose) {
        printf("Connecting to %s@%s:%d\n", username, hostname, port);
    }

    /* Create SSH session */
    ssh_session_t *session = ssh_new();
    if (!session) {
        fprintf(stderr, "Failed to create SSH session\n");
        return 1;
    }

    (void)ssh_set_verbose(session, verbose);
    ssh_set_host(session, hostname);
    ssh_set_port(session, port);
    ssh_set_user(session, username);
    ssh_set_hostkey_callback(session, hostkey_callback, NULL);

    /* Connect */
    int rc = ssh_connect(session);
    if (rc != SSH_OK) {
        fprintf(stderr, "Connection failed: %s\n", ssh_get_error(session));
        ssh_free(session);
        return 1;
    }

    if (verbose) {
        printf("Connected. Authenticating...\n");
    }

    /* Try public key authentication first */
    int authenticated = 0;

    if (identity) {
        ssh_key_t *key = ssh_key_load(identity, NULL);
        if (key) {
            rc = ssh_auth_publickey(session, key);
            if (rc == SSH_OK) {
                authenticated = 1;
                if (verbose)
                    printf("Public key authentication successful\n");
            }
            ssh_key_free(key);
        } else {
            fprintf(stderr, "Warning: Could not load identity file %s\n", identity);
        }
    }

    /* Try default identity files */
    if (!authenticated) {
        const char *home = getenv("HOME");
        if (home) {
            static char keypath[256];

            /* Try Ed25519 key */
            snprintf(keypath, sizeof(keypath), "%s/.ssh/id_ed25519", home);
            ssh_key_t *key = ssh_key_load(keypath, NULL);
            if (key) {
                rc = ssh_auth_publickey(session, key);
                if (rc == SSH_OK) {
                    authenticated = 1;
                    if (verbose)
                        printf("Ed25519 key authentication successful\n");
                }
                ssh_key_free(key);
            }

            /* Try RSA key */
            if (!authenticated) {
                snprintf(keypath, sizeof(keypath), "%s/.ssh/id_rsa", home);
                key = ssh_key_load(keypath, NULL);
                if (key) {
                    rc = ssh_auth_publickey(session, key);
                    if (rc == SSH_OK) {
                        authenticated = 1;
                        if (verbose)
                            printf("RSA key authentication successful\n");
                    }
                    ssh_key_free(key);
                }
            }
        }
    }

    /* Fall back to password authentication */
    if (!authenticated) {
        char password[256];
        password[0] = '\0';
        printf("%s@%s's password: ", username, hostname);
        fflush(stdout);

        /* Disable echo for password input */
        struct termios old_term, new_term;
        int have_old_term = (tcgetattr(STDIN_FILENO, &old_term) == 0);
        if (have_old_term) {
            new_term = old_term;
            new_term.c_lflag &= ~ECHO;
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        }

        if (!fgets(password, sizeof(password), stdin)) {
            fprintf(stderr, "\nFailed to read password from stdin\n");
            ssh_disconnect(session);
            ssh_free(session);
            return 1;
        }

        /* Remove newline / carriage return */
        size_t len = strlen(password);
        while (len > 0 && (password[len - 1] == '\n' || password[len - 1] == '\r')) {
            password[len - 1] = '\0';
            len--;
        }

        if (have_old_term) {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        }
        printf("\n");

        if (verbose) {
            printf("[ssh] Read password: len=%zu\n", strlen(password));
        }

        rc = ssh_auth_password(session, password);
        if (rc == SSH_OK) {
            authenticated = 1;
        } else {
            fprintf(stderr, "Authentication failed\n");
            ssh_disconnect(session);
            ssh_free(session);
            return 1;
        }
    }

    /* Open channel */
    ssh_channel_t *channel = ssh_channel_new(session);
    if (!channel) {
        fprintf(stderr, "Failed to create channel\n");
        ssh_disconnect(session);
        ssh_free(session);
        return 1;
    }

    rc = ssh_channel_open_session(channel);
    if (rc != SSH_OK) {
        fprintf(stderr, "Failed to open session: %s\n", ssh_get_error(session));
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        return 1;
    }

    if (command) {
        /* Execute command */
        rc = ssh_channel_request_exec(channel, command);
        if (rc != SSH_OK) {
            fprintf(stderr, "Failed to execute command: %s\n", ssh_get_error(session));
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            ssh_disconnect(session);
            ssh_free(session);
            return 1;
        }

        /* Read output */
        char buf[4096];
        int is_stderr;
        ssize_t nread;

        while ((nread = ssh_channel_read(channel, buf, sizeof(buf), &is_stderr)) > 0) {
            if (is_stderr) {
                fwrite(buf, 1, nread, stderr);
            } else {
                fwrite(buf, 1, nread, stdout);
            }
        }

        int exit_status = ssh_channel_get_exit_status(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        return exit_status;
    }

    /* Interactive shell */
    rc = ssh_channel_request_pty(channel, "xterm", 80, 24);
    if (rc != SSH_OK) {
        fprintf(stderr, "Failed to request PTY: %s\n", ssh_get_error(session));
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        return 1;
    }

    rc = ssh_channel_request_shell(channel);
    if (rc != SSH_OK) {
        fprintf(stderr, "Failed to start shell: %s\n", ssh_get_error(session));
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        return 1;
    }

    /* Enable raw mode for interactive session */
    enable_raw_mode();

    /* Main loop */
    char buf[64 * 1024];
    int sockfd = ssh_get_socket_fd(session);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to get session socket fd\n");
        goto done;
    }

    while (ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel)) {
        struct pollfd pfds[2];
        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfds[1].fd = sockfd;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;

        /* Use a timeout to ensure we regularly check both stdin and socket,
         * even if one is more active than the other. */
        int pr = poll(pfds, 2, 100);

        if (pr < 0) {
            continue;
        }

        if (pfds[0].revents & POLLIN) {
            ssize_t nread = read(STDIN_FILENO, buf, sizeof(buf));
            if (nread > 0) {
                ssh_channel_write(channel, buf, nread);
            }
        }

        if (pfds[1].revents & (POLLIN | POLLERR | POLLHUP)) {
            /* Read one chunk of data per poll iteration.
             * ssh_channel_read may block inside recv(), so we avoid
             * looping to ensure stdin gets checked regularly. */
            int is_stderr;
            ssize_t nread = ssh_channel_read(channel, buf, sizeof(buf), &is_stderr);
            if (nread > 0) {
                if (is_stderr) {
                    write(STDERR_FILENO, buf, nread);
                } else {
                    write(STDOUT_FILENO, buf, nread);
                }
            } else if (nread != SSH_AGAIN) {
                /* EOF or error */
                goto done;
            }
        }
    }

done:
    disable_raw_mode();

    int exit_status = ssh_channel_get_exit_status(channel);
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_disconnect(session);
    ssh_free(session);

    return exit_status >= 0 ? exit_status : 0;
}
