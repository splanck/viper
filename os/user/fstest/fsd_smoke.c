#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Raw kernel syscall wrappers (bypass libc fsd routing). */
extern long __syscall1(long num, long arg0);
extern long __syscall2(long num, long arg0, long arg1);

#define SYS_OPEN 0x40
#define SYS_CLOSE 0x41

static void print_result(const char *label, long rc)
{
    printf("[fsd_smoke] %s: %ld\n", label, rc);
}

int main(void)
{
    const char *path = "/t/libc_fsd_smoke.txt";
    const char *payload = "libc->fsd smoke test\n";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        print_result("open (libc)", fd);
        return 1;
    }

    ssize_t w = write(fd, payload, strlen(payload));
    if (w < 0)
    {
        print_result("write (libc)", w);
        (void)close(fd);
        return 1;
    }
    (void)close(fd);

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        print_result("open for read (libc)", fd);
        return 1;
    }

    char buf[64];
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    (void)close(fd);
    if (r < 0)
    {
        print_result("read (libc)", r);
        return 1;
    }
    buf[(r >= 0 && r < (ssize_t)sizeof(buf)) ? (size_t)r : (sizeof(buf) - 1)] = '\0';

    if (strcmp(buf, payload) != 0)
    {
        printf("[fsd_smoke] payload mismatch: got=\"%s\"\n", buf);
        return 1;
    }

    /* Verify kernel VFS does NOT see the file (should be on fsd's disk). */
    long kfd = __syscall2(SYS_OPEN, (long)path, O_RDONLY);
    if (kfd >= 0)
    {
        (void)__syscall1(SYS_CLOSE, kfd);
        printf("[fsd_smoke] FAIL: kernel open unexpectedly succeeded\n");
        return 1;
    }

    printf("[fsd_smoke] OK: libc routed to fsd (kernel can't see file)\n");
    return 0;
}
