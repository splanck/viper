#include "../include/termios.h"
#include "../include/string.h"

/*
 * Minimal termios implementation for ViperOS
 *
 * Since ViperOS doesn't have a full TTY subsystem, this provides
 * a basic implementation that tracks terminal settings for the
 * standard file descriptors (stdin/stdout/stderr).
 */

/* Default terminal settings (cooked mode with echo) */
static struct termios default_termios = {
    .c_iflag = ICRNL | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CS8 | CREAD | CLOCAL,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN,
    .c_cc =
        {
            [VINTR] = 0x03,  /* Ctrl+C */
            [VQUIT] = 0x1C,  /* Ctrl+\ */
            [VERASE] = 0x7F, /* Backspace */
            [VKILL] = 0x15,  /* Ctrl+U */
            [VEOF] = 0x04,   /* Ctrl+D */
            [VTIME] = 0,
            [VMIN] = 1,
            [VSTART] = 0x11, /* Ctrl+Q */
            [VSTOP] = 0x13,  /* Ctrl+S */
            [VSUSP] = 0x1A,  /* Ctrl+Z */
        },
    .c_ispeed = B9600,
    .c_ospeed = B9600,
};

/* Current terminal settings for stdin (fd 0) */
static struct termios current_termios;
static int termios_initialized = 0;

static void init_termios(void)
{
    if (!termios_initialized)
    {
        memcpy(&current_termios, &default_termios, sizeof(struct termios));
        termios_initialized = 1;
    }
}

int tcgetattr(int fd, struct termios *termios_p)
{
    if (!termios_p)
        return -1;

    /* Only support stdin for now */
    if (fd < 0 || fd > 2)
        return -1;

    init_termios();
    memcpy(termios_p, &current_termios, sizeof(struct termios));
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
    (void)optional_actions; /* We apply immediately regardless */

    if (!termios_p)
        return -1;

    /* Only support stdin for now */
    if (fd < 0 || fd > 2)
        return -1;

    init_termios();
    memcpy(&current_termios, termios_p, sizeof(struct termios));
    return 0;
}

int tcsendbreak(int fd, int duration)
{
    (void)fd;
    (void)duration;
    /* No-op: break not supported */
    return 0;
}

int tcdrain(int fd)
{
    (void)fd;
    /* No buffering, so nothing to drain */
    return 0;
}

int tcflush(int fd, int queue_selector)
{
    (void)fd;
    (void)queue_selector;
    /* No kernel buffers to flush */
    return 0;
}

int tcflow(int fd, int action)
{
    (void)fd;
    (void)action;
    /* Flow control not supported */
    return 0;
}

speed_t cfgetispeed(const struct termios *termios_p)
{
    if (!termios_p)
        return B0;
    return termios_p->c_ispeed;
}

speed_t cfgetospeed(const struct termios *termios_p)
{
    if (!termios_p)
        return B0;
    return termios_p->c_ospeed;
}

int cfsetispeed(struct termios *termios_p, speed_t speed)
{
    if (!termios_p)
        return -1;
    termios_p->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *termios_p, speed_t speed)
{
    if (!termios_p)
        return -1;
    termios_p->c_ospeed = speed;
    return 0;
}

void cfmakeraw(struct termios *termios_p)
{
    if (!termios_p)
        return;

    /* Turn off all processing */
    termios_p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termios_p->c_cflag &= ~(CSIZE | PARENB);
    termios_p->c_cflag |= CS8;

    /* Set read to return immediately with 1 character minimum */
    termios_p->c_cc[VMIN] = 1;
    termios_p->c_cc[VTIME] = 0;
}

int isatty(int fd)
{
    /* stdin, stdout, stderr are TTYs */
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

/* Static buffer for ttyname */
static char ttyname_buf[16];

char *ttyname(int fd)
{
    if (fd < 0 || fd > 2)
        return (char *)0;

    /* Return a generic TTY name */
    memcpy(ttyname_buf, "/dev/tty", 9);
    return ttyname_buf;
}
