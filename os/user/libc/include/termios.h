#ifndef _TERMIOS_H
#define _TERMIOS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Terminal control characters */
#define NCCS 32

/* Control character indices */
#define VINTR    0   /* Interrupt character */
#define VQUIT    1   /* Quit character */
#define VERASE   2   /* Erase character */
#define VKILL    3   /* Kill line character */
#define VEOF     4   /* End-of-file character */
#define VTIME    5   /* Timeout in deciseconds */
#define VMIN     6   /* Minimum characters for read */
#define VSTART   8   /* Start character (XON) */
#define VSTOP    9   /* Stop character (XOFF) */
#define VSUSP    10  /* Suspend character */

/* Input mode flags (c_iflag) */
#define IGNBRK  0x00001  /* Ignore break condition */
#define BRKINT  0x00002  /* Signal interrupt on break */
#define IGNPAR  0x00004  /* Ignore parity errors */
#define PARMRK  0x00008  /* Mark parity errors */
#define INPCK   0x00010  /* Enable input parity check */
#define ISTRIP  0x00020  /* Strip 8th bit */
#define INLCR   0x00040  /* Map NL to CR */
#define IGNCR   0x00080  /* Ignore CR */
#define ICRNL   0x00100  /* Map CR to NL */
#define IXON    0x00400  /* Enable XON/XOFF output control */
#define IXOFF   0x01000  /* Enable XON/XOFF input control */
#define IXANY   0x00800  /* Any character restarts output */

/* Output mode flags (c_oflag) */
#define OPOST   0x00001  /* Post-process output */
#define ONLCR   0x00004  /* Map NL to CR-NL */
#define OCRNL   0x00008  /* Map CR to NL */
#define ONOCR   0x00010  /* No CR at column 0 */
#define ONLRET  0x00020  /* NL performs CR function */

/* Control mode flags (c_cflag) */
#define CSIZE   0x00030  /* Character size mask */
#define CS5     0x00000  /* 5 bits */
#define CS6     0x00010  /* 6 bits */
#define CS7     0x00020  /* 7 bits */
#define CS8     0x00030  /* 8 bits */
#define CSTOPB  0x00040  /* 2 stop bits */
#define CREAD   0x00080  /* Enable receiver */
#define PARENB  0x00100  /* Enable parity */
#define PARODD  0x00200  /* Odd parity */
#define HUPCL   0x00400  /* Hang up on last close */
#define CLOCAL  0x00800  /* Ignore modem control lines */

/* Local mode flags (c_lflag) */
#define ISIG    0x00001  /* Enable signals */
#define ICANON  0x00002  /* Canonical mode (line editing) */
#define ECHO    0x00008  /* Enable echo */
#define ECHOE   0x00010  /* Echo erase character */
#define ECHOK   0x00020  /* Echo kill character */
#define ECHONL  0x00040  /* Echo NL even if ECHO off */
#define NOFLSH  0x00080  /* Disable flush after interrupt */
#define TOSTOP  0x00100  /* Stop background jobs */
#define IEXTEN  0x08000  /* Enable extended functions */

/* Baud rate values */
#define B0      0
#define B50     1
#define B75     2
#define B110    3
#define B134    4
#define B150    5
#define B200    6
#define B300    7
#define B600    8
#define B1200   9
#define B1800   10
#define B2400   11
#define B4800   12
#define B9600   13
#define B19200  14
#define B38400  15
#define B57600  16
#define B115200 17

/* tcsetattr optional_actions */
#define TCSANOW   0  /* Change immediately */
#define TCSADRAIN 1  /* Change after all output transmitted */
#define TCSAFLUSH 2  /* Flush input and change */

/* tcflush queue_selector */
#define TCIFLUSH  0  /* Flush input queue */
#define TCOFLUSH  1  /* Flush output queue */
#define TCIOFLUSH 2  /* Flush both queues */

/* tcflow action */
#define TCOOFF 0  /* Suspend output */
#define TCOON  1  /* Resume output */
#define TCIOFF 2  /* Transmit STOP character */
#define TCION  3  /* Transmit START character */

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

/* Terminal attributes structure */
struct termios {
    tcflag_t c_iflag;    /* Input modes */
    tcflag_t c_oflag;    /* Output modes */
    tcflag_t c_cflag;    /* Control modes */
    tcflag_t c_lflag;    /* Local modes */
    cc_t c_cc[NCCS];     /* Control characters */
    speed_t c_ispeed;    /* Input baud rate */
    speed_t c_ospeed;    /* Output baud rate */
};

/* Terminal control functions */
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int tcsendbreak(int fd, int duration);
int tcdrain(int fd);
int tcflush(int fd, int queue_selector);
int tcflow(int fd, int action);

/* Baud rate functions */
speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int cfsetispeed(struct termios *termios_p, speed_t speed);
int cfsetospeed(struct termios *termios_p, speed_t speed);

/* Raw mode helper */
void cfmakeraw(struct termios *termios_p);

/* Check if fd is a terminal */
int isatty(int fd);

/* Get terminal name */
char *ttyname(int fd);

#ifdef __cplusplus
}
#endif

#endif /* _TERMIOS_H */
