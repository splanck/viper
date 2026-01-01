/**
 * @file sftp.c
 * @brief SFTP client for ViperOS.
 *
 * Interactive SFTP client for file transfers over SSH.
 *
 * Usage: sftp [-P port] [-i identity] user@host
 */

#include <errno.h>
#include <fcntl.h>
#include <sftp.h>
#include <ssh.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static ssh_session_t *g_session = NULL;
static sftp_session_t *g_sftp = NULL;
static char g_cwd[1024] = "/";
static char g_lcwd[1024] = "/";

static void print_help(void)
{
    printf("Available commands:\n");
    printf("  cd path              Change remote directory\n");
    printf("  lcd path             Change local directory\n");
    printf("  pwd                  Print remote working directory\n");
    printf("  lpwd                 Print local working directory\n");
    printf("  ls [path]            List remote directory\n");
    printf("  lls [path]           List local directory\n");
    printf("  get remote [local]   Download file\n");
    printf("  put local [remote]   Upload file\n");
    printf("  mkdir path           Create remote directory\n");
    printf("  rmdir path           Remove remote directory\n");
    printf("  rm file              Remove remote file\n");
    printf("  rename old new       Rename remote file\n");
    printf("  chmod mode path      Change permissions\n");
    printf("  stat path            Show file information\n");
    printf("  help                 Show this help\n");
    printf("  quit                 Exit sftp\n");
}

static char *join_path(const char *base, const char *name)
{
    static char buf[2048];
    if (name[0] == '/')
    {
        strncpy(buf, name, sizeof(buf) - 1);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%s/%s", base, name);
    }
    return buf;
}

static const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void format_size(uint64_t size, char *buf, size_t len)
{
    if (size < 1024)
    {
        snprintf(buf, len, "%llu", (unsigned long long)size);
    }
    else if (size < 1024 * 1024)
    {
        snprintf(buf, len, "%.1fK", size / 1024.0);
    }
    else if (size < 1024 * 1024 * 1024)
    {
        snprintf(buf, len, "%.1fM", size / (1024.0 * 1024));
    }
    else
    {
        snprintf(buf, len, "%.1fG", size / (1024.0 * 1024 * 1024));
    }
}

static void format_mode(uint32_t mode, char *buf)
{
    buf[0] = (mode & 0040000) ? 'd' : (mode & 0120000) ? 'l' : '-';
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';
    buf[4] = (mode & 040) ? 'r' : '-';
    buf[5] = (mode & 020) ? 'w' : '-';
    buf[6] = (mode & 010) ? 'x' : '-';
    buf[7] = (mode & 04) ? 'r' : '-';
    buf[8] = (mode & 02) ? 'w' : '-';
    buf[9] = (mode & 01) ? 'x' : '-';
    buf[10] = '\0';
}

static int compare_entries(const void *a, const void *b)
{
    const sftp_attributes_t *ea = *(const sftp_attributes_t **)a;
    const sftp_attributes_t *eb = *(const sftp_attributes_t **)b;
    if (!ea->name)
        return eb->name ? 1 : 0;
    if (!eb->name)
        return -1;
    return strcmp(ea->name, eb->name);
}

static void cmd_ls(const char *path)
{
    const char *dir_path = path ? join_path(g_cwd, path) : g_cwd;
    sftp_dir_t *dir = sftp_opendir(g_sftp, dir_path);
    if (!dir)
    {
        printf("Cannot open directory: %s\n", dir_path);
        return;
    }

    /* Collect all entries for sorting */
    sftp_attributes_t **entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    sftp_attributes_t *attr;
    while ((attr = sftp_readdir(dir)) != NULL)
    {
        if (count >= capacity)
        {
            size_t new_cap = capacity ? capacity * 2 : 64;
            sftp_attributes_t **new_entries = realloc(entries, new_cap * sizeof(*entries));
            if (!new_entries)
            {
                sftp_attributes_free(attr);
                break;
            }
            entries = new_entries;
            capacity = new_cap;
        }
        entries[count++] = attr;
    }

    sftp_closedir(dir);

    /* Sort entries alphabetically */
    if (entries && count > 0)
    {
        qsort(entries, count, sizeof(*entries), compare_entries);
    }

    /* Display sorted entries */
    for (size_t i = 0; i < count; i++)
    {
        attr = entries[i];
        char mode_str[12];
        char size_str[16];

        if (attr->flags & SFTP_ATTR_PERMISSIONS)
        {
            format_mode(attr->permissions, mode_str);
        }
        else
        {
            strcpy(mode_str, "----------");
        }

        if (attr->flags & SFTP_ATTR_SIZE)
        {
            format_size(attr->size, size_str, sizeof(size_str));
        }
        else
        {
            strcpy(size_str, "-");
        }

        printf("%s %8s %s\n", mode_str, size_str, attr->name ? attr->name : "?");
        sftp_attributes_free(attr);
    }

    free(entries);
}

static void cmd_cd(const char *path)
{
    if (!path)
    {
        printf("Usage: cd path\n");
        return;
    }

    char *newpath = sftp_realpath(g_sftp, join_path(g_cwd, path));
    if (newpath)
    {
        strncpy(g_cwd, newpath, sizeof(g_cwd) - 1);
        free(newpath);
    }
    else
    {
        printf("Cannot change to directory: %s\n", path);
    }
}

static void cmd_pwd(void)
{
    printf("%s\n", g_cwd);
}

static void cmd_lcd(const char *path)
{
    if (!path)
    {
        printf("Usage: lcd path\n");
        return;
    }

    if (chdir(path) == 0)
    {
        if (getcwd(g_lcwd, sizeof(g_lcwd)) == NULL)
        {
            printf("Warning: could not get current directory\n");
        }
    }
    else
    {
        printf("Cannot change to directory: %s\n", path);
    }
}

static void cmd_lpwd(void)
{
    if (getcwd(g_lcwd, sizeof(g_lcwd)))
    {
        printf("%s\n", g_lcwd);
    }
    else
    {
        printf("Cannot get current directory\n");
    }
}

static void cmd_get(const char *remote, const char *local)
{
    if (!remote)
    {
        printf("Usage: get remote [local]\n");
        return;
    }

    const char *remote_path = join_path(g_cwd, remote);
    const char *local_path = local ? local : basename_of(remote);

    sftp_file_t *rf = sftp_open(g_sftp, remote_path, SFTP_READ, 0);
    if (!rf)
    {
        printf("Cannot open remote file: %s\n", remote_path);
        return;
    }

    int lf = open(local_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (lf < 0)
    {
        printf("Cannot create local file: %s\n", local_path);
        sftp_close(rf);
        return;
    }

    char buf[32768];
    ssize_t nread;
    uint64_t total = 0;

    printf("Downloading %s...\n", remote_path);

    while ((nread = sftp_read(rf, buf, sizeof(buf))) > 0)
    {
        ssize_t nwritten = write(lf, buf, nread);
        if (nwritten != nread)
        {
            printf("Error writing to local file\n");
            break;
        }
        total += nread;
    }

    /* Sync to ensure data reaches filesystem */
    fsync(lf);
    close(lf);
    sftp_close(rf);

    char size_str[16];
    format_size(total, size_str, sizeof(size_str));
    printf("Downloaded %s bytes\n", size_str);
}

static void cmd_put(const char *local, const char *remote)
{
    if (!local)
    {
        printf("Usage: put local [remote]\n");
        return;
    }

    const char *remote_path =
        remote ? join_path(g_cwd, remote) : join_path(g_cwd, basename_of(local));

    int lf = open(local, O_RDONLY);
    if (lf < 0)
    {
        printf("Cannot open local file: %s\n", local);
        return;
    }

    sftp_file_t *rf = sftp_open(g_sftp, remote_path, SFTP_WRITE | SFTP_CREAT | SFTP_TRUNC, 0644);
    if (!rf)
    {
        printf("Cannot create remote file: %s\n", remote_path);
        close(lf);
        return;
    }

    char buf[32768];
    ssize_t nread;
    uint64_t total = 0;

    printf("Uploading to %s...\n", remote_path);

    while ((nread = read(lf, buf, sizeof(buf))) > 0)
    {
        ssize_t nwritten = sftp_write(rf, buf, nread);
        if (nwritten != nread)
        {
            printf("Error writing to remote file\n");
            break;
        }
        total += nread;
    }

    close(lf);
    sftp_close(rf);

    char size_str[16];
    format_size(total, size_str, sizeof(size_str));
    printf("Uploaded %s bytes\n", size_str);
}

static void cmd_mkdir(const char *path)
{
    if (!path)
    {
        printf("Usage: mkdir path\n");
        return;
    }

    int rc = sftp_mkdir(g_sftp, join_path(g_cwd, path), 0755);
    if (rc != SFTP_OK)
    {
        printf("Cannot create directory: %s\n", path);
    }
}

static void cmd_rmdir(const char *path)
{
    if (!path)
    {
        printf("Usage: rmdir path\n");
        return;
    }

    int rc = sftp_rmdir(g_sftp, join_path(g_cwd, path));
    if (rc != SFTP_OK)
    {
        printf("Cannot remove directory: %s\n", path);
    }
}

static void cmd_rm(const char *path)
{
    if (!path)
    {
        printf("Usage: rm file\n");
        return;
    }

    int rc = sftp_unlink(g_sftp, join_path(g_cwd, path));
    if (rc != SFTP_OK)
    {
        printf("Cannot remove file: %s\n", path);
    }
}

static void cmd_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
    {
        printf("Usage: rename old new\n");
        return;
    }

    int rc = sftp_rename(g_sftp, join_path(g_cwd, oldpath), join_path(g_cwd, newpath));
    if (rc != SFTP_OK)
    {
        printf("Cannot rename: %s -> %s\n", oldpath, newpath);
    }
}

static void cmd_chmod(const char *mode_str, const char *path)
{
    if (!mode_str || !path)
    {
        printf("Usage: chmod mode path\n");
        return;
    }

    mode_t mode = strtol(mode_str, NULL, 8);
    int rc = sftp_chmod(g_sftp, join_path(g_cwd, path), mode);
    if (rc != SFTP_OK)
    {
        printf("Cannot change permissions: %s\n", path);
    }
}

static void cmd_stat(const char *path)
{
    if (!path)
    {
        printf("Usage: stat path\n");
        return;
    }

    sftp_attributes_t *attr = sftp_stat(g_sftp, join_path(g_cwd, path));
    if (!attr)
    {
        printf("Cannot stat: %s\n", path);
        return;
    }

    printf("File: %s\n", path);

    if (attr->flags & SFTP_ATTR_SIZE)
    {
        printf("Size: %llu\n", (unsigned long long)attr->size);
    }

    if (attr->flags & SFTP_ATTR_PERMISSIONS)
    {
        char mode_str[12];
        format_mode(attr->permissions, mode_str);
        printf("Mode: %s (%04o)\n", mode_str, attr->permissions & 07777);
    }

    if (attr->flags & SFTP_ATTR_UIDGID)
    {
        printf("UID: %u  GID: %u\n", attr->uid, attr->gid);
    }

    sftp_attributes_free(attr);
}

static int hostkey_callback(ssh_session_t *session,
                            const char *hostname,
                            const uint8_t *key,
                            size_t key_len,
                            ssh_keytype_t keytype,
                            void *userdata)
{
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
    printf("Accept? (yes/no): ");
    fflush(stdout);

    char answer[10];
    if (fgets(answer, sizeof(answer), stdin))
    {
        if (strncmp(answer, "yes", 3) == 0)
        {
            return 0;
        }
    }

    return -1;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-P port] [-i identity] user@host\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -P port      Connect to specified port (default 22)\n");
    fprintf(stderr, "  -i identity  Use identity file for public key authentication\n");
}

int main(int argc, char *argv[])
{
    const char *hostname = NULL;
    const char *username = NULL;
    const char *identity = NULL;
    uint16_t port = 22;
    int opt;

    while ((opt = getopt(argc, argv, "P:i:h")) != -1)
    {
        switch (opt)
        {
            case 'P':
                port = atoi(optarg);
                break;
            case 'i':
                identity = optarg;
                break;
            case 'h':
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (optind >= argc)
    {
        usage(argv[0]);
        return 1;
    }

    char *hostarg = argv[optind];

    char *at = strchr(hostarg, '@');
    if (at)
    {
        *at = '\0';
        username = hostarg;
        hostname = at + 1;
    }
    else
    {
        hostname = hostarg;
        username = getenv("USER");
        if (!username)
            username = "root";
    }

    printf("Connecting to %s@%s:%d...\n", username, hostname, port);

    /* Create SSH session */
    g_session = ssh_new();
    if (!g_session)
    {
        fprintf(stderr, "Failed to create SSH session\n");
        return 1;
    }

    ssh_set_host(g_session, hostname);
    ssh_set_port(g_session, port);
    ssh_set_user(g_session, username);
    ssh_set_hostkey_callback(g_session, hostkey_callback, NULL);

    int rc = ssh_connect(g_session);
    if (rc != SSH_OK)
    {
        fprintf(stderr, "Connection failed: %s\n", ssh_get_error(g_session));
        ssh_free(g_session);
        return 1;
    }

    /* Authenticate */
    int authenticated = 0;

    if (identity)
    {
        ssh_key_t *key = ssh_key_load(identity, NULL);
        if (key)
        {
            if (ssh_auth_publickey(g_session, key) == SSH_OK)
            {
                authenticated = 1;
            }
            ssh_key_free(key);
        }
    }

    if (!authenticated)
    {
        char password[256];
        printf("%s@%s's password: ", username, hostname);
        fflush(stdout);

        struct termios old_term, new_term;
        int have_old_term = (tcgetattr(STDIN_FILENO, &old_term) == 0);
        if (have_old_term)
        {
            new_term = old_term;
            new_term.c_lflag &= ~ECHO;
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
        }

        if (fgets(password, sizeof(password), stdin))
        {
            size_t len = strlen(password);
            if (len > 0 && password[len - 1] == '\n')
            {
                password[len - 1] = '\0';
            }
        }

        if (have_old_term)
        {
            (void)tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
        }
        printf("\n");

        if (ssh_auth_password(g_session, password) == SSH_OK)
        {
            authenticated = 1;
        }
    }

    if (!authenticated)
    {
        fprintf(stderr, "Authentication failed\n");
        ssh_disconnect(g_session);
        ssh_free(g_session);
        return 1;
    }

    /* Create SFTP session */
    g_sftp = sftp_new(g_session);
    if (!g_sftp)
    {
        fprintf(stderr, "Failed to create SFTP session\n");
        ssh_disconnect(g_session);
        ssh_free(g_session);
        return 1;
    }

    rc = sftp_init(g_sftp);
    if (rc != SFTP_OK)
    {
        fprintf(stderr, "Failed to initialize SFTP\n");
        sftp_free(g_sftp);
        ssh_disconnect(g_session);
        ssh_free(g_session);
        return 1;
    }

    /* Get initial directory */
    char *cwd = sftp_getcwd(g_sftp);
    if (cwd)
    {
        strncpy(g_cwd, cwd, sizeof(g_cwd) - 1);
        free(cwd);
    }

    if (getcwd(g_lcwd, sizeof(g_lcwd)) == NULL)
    {
        strcpy(g_lcwd, "/");
    }

    printf("Connected to %s.\n", hostname);
    printf("Type 'help' for available commands.\n");

    /* Command loop */
    char line[1024];

    while (1)
    {
        printf("sftp> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin))
        {
            break;
        }

        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
        {
            line[len - 1] = '\0';
        }

        /* Skip empty lines */
        if (line[0] == '\0')
            continue;

        /* Parse command */
        char *cmd = strtok(line, " \t");
        char *arg1 = strtok(NULL, " \t");
        char *arg2 = strtok(NULL, " \t");

        if (!cmd)
            continue;

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "bye") == 0)
        {
            break;
        }
        else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0)
        {
            print_help();
        }
        else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0)
        {
            cmd_ls(arg1);
        }
        else if (strcmp(cmd, "cd") == 0)
        {
            cmd_cd(arg1);
        }
        else if (strcmp(cmd, "pwd") == 0)
        {
            cmd_pwd();
        }
        else if (strcmp(cmd, "lcd") == 0)
        {
            cmd_lcd(arg1);
        }
        else if (strcmp(cmd, "lpwd") == 0)
        {
            cmd_lpwd();
        }
        else if (strcmp(cmd, "get") == 0)
        {
            cmd_get(arg1, arg2);
        }
        else if (strcmp(cmd, "put") == 0)
        {
            cmd_put(arg1, arg2);
        }
        else if (strcmp(cmd, "mkdir") == 0)
        {
            cmd_mkdir(arg1);
        }
        else if (strcmp(cmd, "rmdir") == 0)
        {
            cmd_rmdir(arg1);
        }
        else if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "delete") == 0)
        {
            cmd_rm(arg1);
        }
        else if (strcmp(cmd, "rename") == 0 || strcmp(cmd, "mv") == 0)
        {
            cmd_rename(arg1, arg2);
        }
        else if (strcmp(cmd, "chmod") == 0)
        {
            cmd_chmod(arg1, arg2);
        }
        else if (strcmp(cmd, "stat") == 0)
        {
            cmd_stat(arg1);
        }
        else if (strcmp(cmd, "lls") == 0)
        {
            printf("(Local listing not implemented - use shell)\n");
        }
        else
        {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for available commands.\n");
        }
    }

    printf("Disconnecting...\n");

    sftp_free(g_sftp);
    ssh_disconnect(g_session);
    ssh_free(g_session);

    return 0;
}
