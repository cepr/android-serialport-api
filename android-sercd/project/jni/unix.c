/*
 * sercd UNIX support
 * Copyright 2008 Peter Åstrand <astrand@cendio.se> for Cendio AB
 * see file COPYING for license details
 */

#ifndef WIN32
#include "sercd.h"
#include "unix.h"

#include <termios.h>
#ifndef ANDROID
#include <termio.h>
#endif
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>           /* gettimeofday */
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#ifdef ANDROID
#include <android/log.h>
#endif
/* timeval macros */
#ifndef timerisset
#define timerisset(tvp)\
         ((tvp)->tv_sec || (tvp)->tv_usec)
#endif
#ifndef timercmp
#define timercmp(tvp, uvp, cmp)\
        ((tvp)->tv_sec cmp (uvp)->tv_sec ||\
        (tvp)->tv_sec == (uvp)->tv_sec &&\
        (tvp)->tv_usec cmp (uvp)->tv_usec)
#endif
#ifndef timerclear
#define timerclear(tvp)\
        ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#endif

extern Boolean BreakSignaled;

extern Boolean StdErrLogging;

extern int MaxLogLevel;

static struct timeval LastPoll = { 0, 0 };

/* Initial serial port settings */
static struct termios *InitialPortSettings;
static struct termios initialportsettings;

/* Locking constants */
#define LockOk 0
#define Locked 1
#define LockKo 2

/* File mode and file length for HDB (ASCII) stile lock file */
#define LockFileMode 0644
#define HDBHeaderLen 11

/* Convert termios speed to tncom speed */
static unsigned long int
Termios2TncomSpeed(struct termios *ti)
{
    speed_t ispeed, ospeed;

    ispeed = cfgetispeed(ti);
    ospeed = cfgetospeed(ti);

    if (ispeed != ospeed) {
        LogMsg(LOG_WARNING, "warning: different input and output speed, using output speed");
    }

    switch (ospeed) {
    case B50:
        return (50UL);
    case B75:
        return (75UL);
    case B110:
        return (110UL);
    case B134:
        return (134UL);
    case B150:
        return (150UL);
    case B200:
        return (200UL);
    case B300:
        return (300UL);
    case B600:
        return (600UL);
    case B1200:
        return (1200UL);
    case B1800:
        return (1800UL);
    case B2400:
        return (2400UL);
    case B4800:
        return (4800UL);
    case B9600:
        return (9600UL);
    case B19200:
        return (19200UL);
    case B38400:
        return (38400UL);
    case B57600:
        return (57600UL);
    case B115200:
        return (115200UL);
    case B230400:
        return (230400UL);
    case B460800:
        return (460800UL);
    default:
        return (0UL);
    }
}

/* Convert termios data size to tncom size */
static unsigned char
Termios2TncomDataSize(struct termios *ti)
{
    tcflag_t DataSize;
    DataSize = ti->c_cflag & CSIZE;

    switch (DataSize) {
    case CS5:
        return ((unsigned char) 5);
    case CS6:
        return ((unsigned char) 6);
    case CS7:
        return ((unsigned char) 7);
    case CS8:
        return ((unsigned char) 8);
    default:
        return ((unsigned char) 0);
    }
}

/* Convert termios parity to tncom parity */
static unsigned char
Termios2TncomParity(struct termios *ti)
{
    if ((ti->c_cflag & PARENB) == 0)
        return TNCOM_NOPARITY;

    if ((ti->c_cflag & PARENB) && (ti->c_cflag & PARODD))
        return TNCOM_ODDPARITY;

    return TNCOM_EVENPARITY;
}

/* Convert termios stop size to tncom size */
static unsigned char
Termios2TncomStopSize(struct termios *ti)
{
    if ((ti->c_cflag & CSTOPB) == 0)
        return TNCOM_ONESTOPBIT;
    else
        return TNCOM_TWOSTOPBITS;
}

/* Convert termios output flow control to tncom flow */
static unsigned char
Termios2TncomOutFlow(struct termios *ti)
{
    if (ti->c_iflag & IXON)
        return TNCOM_CMD_FLOW_XONXOFF;
    if (ti->c_cflag & CRTSCTS)
        return TNCOM_CMD_FLOW_HARDWARE;
    return TNCOM_CMD_FLOW_NONE;
}

/* Convert termios input flow control to tncom flow */
static unsigned char
Termios2TncomInFlow(struct termios *ti)
{
    if (ti->c_iflag & IXOFF)
        return TNCOM_CMD_INFLOW_XONXOFF;
    if (ti->c_cflag & CRTSCTS)
        return TNCOM_CMD_INFLOW_HARDWARE;
    return TNCOM_CMD_INFLOW_NONE;
}

static void
UnixLogPortSettings(struct termios *ti)
{
    unsigned long speed;
    unsigned char datasize;
    unsigned char parity;
    unsigned char stopsize;
    unsigned char outflow, inflow;

    speed = Termios2TncomSpeed(ti);
    datasize = Termios2TncomDataSize(ti);
    parity = Termios2TncomParity(ti);
    stopsize = Termios2TncomStopSize(ti);
    outflow = Termios2TncomOutFlow(ti);
    inflow = Termios2TncomInFlow(ti);

    LogPortSettings(speed, datasize, parity, stopsize, outflow, inflow);
}

/* Retrieves the port speed from PortFd */
unsigned long int
GetPortSpeed(PORTHANDLE PortFd)
{
    struct termios PortSettings;

    tcgetattr(PortFd, &PortSettings);
    return Termios2TncomSpeed(&PortSettings);
}

/* Retrieves the data size from PortFd */
unsigned char
GetPortDataSize(PORTHANDLE PortFd)
{
    struct termios PortSettings;

    tcgetattr(PortFd, &PortSettings);
    return Termios2TncomDataSize(&PortSettings);
}

/* Retrieves the parity settings from PortFd */
unsigned char
GetPortParity(PORTHANDLE PortFd)
{
    struct termios PortSettings;

    tcgetattr(PortFd, &PortSettings);
    return Termios2TncomParity(&PortSettings);
}

/* Retrieves the stop bits size from PortFd */
unsigned char
GetPortStopSize(PORTHANDLE PortFd)
{
    struct termios PortSettings;

    tcgetattr(PortFd, &PortSettings);
    return Termios2TncomStopSize(&PortSettings);
}

/* Retrieves the flow control status, including DTR and RTS status,
from PortFd */
unsigned char
GetPortFlowControl(PORTHANDLE PortFd, unsigned char Which)
{
    struct termios PortSettings;
    int MLines;

    /* Gets the basic informations from the port */
    tcgetattr(PortFd, &PortSettings);
    ioctl(PortFd, TIOCMGET, &MLines);

    /* Check wich kind of information is requested */
    switch (Which) {
        /* DTR Signal State */
    case TNCOM_CMD_DTR_REQ:
        /* See comment below. */
        if (MLines & TIOCM_DSR)
            return TNCOM_CMD_DTR_ON;
        else
            return TNCOM_CMD_DTR_OFF;
        break;

        /* RTS Signal State */
    case TNCOM_CMD_RTS_REQ:
        /* Note: RFC2217 mentions RTS but never CTS. Since RTS is an
           output signal, it doesn't make sense to return it's status,
           especially if this means we cannot return the CTS
           status. We believe the RFC is in error in this
           area. Therefore, we are returning CTS rather than RTS. */
        if (MLines & TIOCM_CTS)
            return TNCOM_CMD_RTS_ON;
        else
            return TNCOM_CMD_RTS_OFF;
        break;

        /* Com Port Flow Control Setting (inbound) */
    case TNCOM_CMD_INFLOW_REQ:
        return Termios2TncomInFlow(&PortSettings);
        break;

        /* Com Port Flow Control Setting (outbound/both) */
    case TNCOM_CMD_FLOW_REQ:
    default:
        return Termios2TncomOutFlow(&PortSettings);
        break;
    }
}

/* Return the status of the modem control lines (DCD, CTS, DSR, RNG) */
unsigned char
GetModemState(PORTHANDLE PortFd, unsigned char PMState)
{
    int MLines;
    unsigned char MState = (unsigned char) 0;

    ioctl(PortFd, TIOCMGET, &MLines);

    if ((MLines & TIOCM_CAR) != 0)
        MState += TNCOM_MODMASK_RLSD;
    if ((MLines & TIOCM_RNG) != 0)
        MState += TNCOM_MODMASK_RING;
    if ((MLines & TIOCM_DSR) != 0)
        MState += TNCOM_MODMASK_DSR;
    if ((MLines & TIOCM_CTS) != 0)
        MState += TNCOM_MODMASK_CTS;
    if ((MState & TNCOM_MODMASK_RLSD) != (PMState & TNCOM_MODMASK_RLSD))
        MState += TNCOM_MODMASK_RLSD_DELTA;
    if ((MState & TNCOM_MODMASK_RING) != (PMState & TNCOM_MODMASK_RING))
        MState += TNCOM_MODMASK_RING_TRAIL;
    if ((MState & TNCOM_MODMASK_DSR) != (PMState & TNCOM_MODMASK_DSR))
        MState += TNCOM_MODMASK_DSR_DELTA;
    if ((MState & TNCOM_MODMASK_CTS) != (PMState & TNCOM_MODMASK_CTS))
        MState += TNCOM_MODMASK_CTS_DELTA;

    return (MState);
}

/* Set the serial port data size */
void
SetPortDataSize(PORTHANDLE PortFd, unsigned char DataSize)
{
    struct termios PortSettings;
    tcflag_t PDataSize;

    switch (DataSize) {
    case 5:
        PDataSize = CS5;
        break;
    case 6:
        PDataSize = CS6;
        break;
    case 7:
        PDataSize = CS7;
        break;
    case 8:
        PDataSize = CS8;
        break;
    default:
        PDataSize = CS8;
        break;
    }

    tcgetattr(PortFd, &PortSettings);
    PortSettings.c_cflag &= ~CSIZE;
    PortSettings.c_cflag |= PDataSize & CSIZE;
    tcsetattr(PortFd, TCSADRAIN, &PortSettings);
    UnixLogPortSettings(&PortSettings);
}

/* Set the serial port parity */
void
SetPortParity(PORTHANDLE PortFd, unsigned char Parity)
{
    struct termios PortSettings;

    tcgetattr(PortFd, &PortSettings);

    switch (Parity) {
    case TNCOM_NOPARITY:
        PortSettings.c_cflag = PortSettings.c_cflag & ~PARENB;
        break;
    case TNCOM_ODDPARITY:
        PortSettings.c_cflag = PortSettings.c_cflag | PARENB | PARODD;
        break;
    case TNCOM_EVENPARITY:
        PortSettings.c_cflag = (PortSettings.c_cflag | PARENB) & ~PARODD;
        break;
        /* There's no support for MARK and SPACE parity so sets no parity */
    default:
        LogMsg(LOG_WARNING, "Requested unsupported parity, set to no parity.");
        PortSettings.c_cflag = PortSettings.c_cflag & ~PARENB;
        break;
    }

    tcsetattr(PortFd, TCSADRAIN, &PortSettings);
    UnixLogPortSettings(&PortSettings);
}

/* Set the serial port stop bits size */
void
SetPortStopSize(PORTHANDLE PortFd, unsigned char StopSize)
{
    struct termios PortSettings;

    tcgetattr(PortFd, &PortSettings);

    switch (StopSize) {
    case TNCOM_ONESTOPBIT:
        PortSettings.c_cflag = PortSettings.c_cflag & ~CSTOPB;
        break;
    case TNCOM_TWOSTOPBITS:
        PortSettings.c_cflag = PortSettings.c_cflag | CSTOPB;
        break;
    case TNCOM_ONE5STOPBITS:
        PortSettings.c_cflag = PortSettings.c_cflag & ~CSTOPB;
        LogMsg(LOG_WARNING, "Requested unsupported 1.5 bits stop size, set to 1 bit stop size.");
        break;
    default:
        PortSettings.c_cflag = PortSettings.c_cflag & ~CSTOPB;
        break;
    }

    tcsetattr(PortFd, TCSADRAIN, &PortSettings);
    UnixLogPortSettings(&PortSettings);
}

/* Set the port flow control and DTR and RTS status */
void
SetPortFlowControl(PORTHANDLE PortFd, unsigned char How)
{
    struct termios PortSettings;
    int MLines;

    /* Gets the base status from the port */
    tcgetattr(PortFd, &PortSettings);
    ioctl(PortFd, TIOCMGET, &MLines);

    /* Check which settings to change */
    switch (How) {
        /* No Flow Control (outbound/both) */
    case TNCOM_CMD_FLOW_NONE:
        PortSettings.c_iflag = PortSettings.c_iflag & ~IXON;
        PortSettings.c_iflag = PortSettings.c_iflag & ~IXOFF;
        PortSettings.c_cflag = PortSettings.c_cflag & ~CRTSCTS;
        break;
        /* XON/XOFF Flow Control (outbound/both) */
    case TNCOM_CMD_FLOW_XONXOFF:
        PortSettings.c_iflag = PortSettings.c_iflag | IXON;
        PortSettings.c_iflag = PortSettings.c_iflag | IXOFF;
        PortSettings.c_cflag = PortSettings.c_cflag & ~CRTSCTS;
        break;
        /* HARDWARE Flow Control (outbound/both) */
    case TNCOM_CMD_FLOW_HARDWARE:
        PortSettings.c_iflag = PortSettings.c_iflag & ~IXON;
        PortSettings.c_iflag = PortSettings.c_iflag & ~IXOFF;
        PortSettings.c_cflag = PortSettings.c_cflag | CRTSCTS;
        break;
        /* DTR Signal State ON */
    case TNCOM_CMD_DTR_ON:
        MLines = MLines | TIOCM_DTR;
        break;
        /* DTR Signal State OFF */
    case TNCOM_CMD_DTR_OFF:
        MLines = MLines & ~TIOCM_DTR;
        break;
        /* RTS Signal State ON */
    case TNCOM_CMD_RTS_ON:
        MLines = MLines | TIOCM_RTS;
        break;
        /* RTS Signal State OFF */
    case TNCOM_CMD_RTS_OFF:
        MLines = MLines & ~TIOCM_RTS;
        break;

        /* INBOUND FLOW CONTROL is ignored */
    case TNCOM_CMD_INFLOW_NONE:
    case TNCOM_CMD_INFLOW_XONXOFF:
    case TNCOM_CMD_INFLOW_HARDWARE:
        LogMsg(LOG_WARNING, "Inbound flow control ignored.");
        break;

    case TNCOM_CMD_FLOW_DCD:
        LogMsg(LOG_WARNING, "DCD Flow Control ignored.");
        break;

    case TNCOM_CMD_INFLOW_DTR:
        LogMsg(LOG_WARNING, "DTR Flow Control ignored.");
        break;

    case TNCOM_CMD_FLOW_DSR:
        LogMsg(LOG_WARNING, "DSR Flow Control ignored.");
        break;

    default:
        LogMsg(LOG_WARNING, "Requested invalid flow control.");
        break;
    }

    tcsetattr(PortFd, TCSADRAIN, &PortSettings);
    ioctl(PortFd, TIOCMSET, &MLines);
    UnixLogPortSettings(&PortSettings);
}

/* Set the serial port speed */
void
SetPortSpeed(PORTHANDLE PortFd, unsigned long BaudRate)
{
    struct termios PortSettings;
    speed_t Speed;

    switch (BaudRate) {
    case 50UL:
        Speed = B50;
        break;
    case 75UL:
        Speed = B75;
        break;
    case 110UL:
        Speed = B110;
        break;
    case 134UL:
        Speed = B134;
        break;
    case 150UL:
        Speed = B150;
        break;
    case 200UL:
        Speed = B200;
        break;
    case 300UL:
        Speed = B300;
        break;
    case 600UL:
        Speed = B600;
        break;
    case 1200UL:
        Speed = B1200;
        break;
    case 1800UL:
        Speed = B1800;
        break;
    case 2400UL:
        Speed = B2400;
        break;
    case 4800UL:
        Speed = B4800;
        break;
    case 9600UL:
        Speed = B9600;
        break;
    case 19200UL:
        Speed = B19200;
        break;
    case 38400UL:
        Speed = B38400;
        break;
    case 57600UL:
        Speed = B57600;
        break;
    case 115200UL:
        Speed = B115200;
        break;
    case 230400UL:
        Speed = B230400;
        break;
    case 460800UL:
        Speed = B460800;
        break;
    default:
        LogMsg(LOG_WARNING, "Unknwon baud rate requested, setting to 9600.");
        Speed = B9600;
        break;
    }

    tcgetattr(PortFd, &PortSettings);
    cfsetospeed(&PortSettings, Speed);
    cfsetispeed(&PortSettings, Speed);
    tcsetattr(PortFd, TCSADRAIN, &PortSettings);
    UnixLogPortSettings(&PortSettings);
}

void
SetBreak(PORTHANDLE PortFd, Boolean on)
{
    if (on) {
        tcsendbreak(PortFd, 0);
    }
}

void
SetFlush(PORTHANDLE PortFd, int selector)
{
    switch (selector) {
        /* Inbound flush */
    case TNCOM_PURGE_RX:
        tcflush(PortFd, TCIFLUSH);
        break;
        /* Outbound flush */
    case TNCOM_PURGE_TX:
        tcflush(PortFd, TCOFLUSH);
        break;
        /* Inbound/outbound flush */
    case TNCOM_PURGE_BOTH:
        tcflush(PortFd, TCIOFLUSH);
        break;
    }
}

/* Try to lock the file given in LockFile as pid LockPid using the classical
HDB (ASCII) file locking scheme */
static int
HDBLockFile(const char *LockFile, pid_t LockPid)
{
    pid_t Pid;
    int FileDes;
    int N;
    char HDBBuffer[HDBHeaderLen + 1];
    char LogStr[TmpStrLen];

    /* Try to create the lock file */
    while ((FileDes = open(LockFile, O_CREAT | O_WRONLY | O_EXCL, LockFileMode)) == OpenError) {
        /* Check the kind of error */
        if ((errno == EEXIST)
            && ((FileDes = open(LockFile, O_RDONLY, 0)) != OpenError)) {
            /* Read the HDB header from the existing lockfile */
            N = read(FileDes, HDBBuffer, HDBHeaderLen);
            close(FileDes);

            /* Check if the header has been read */
            if (N <= 0) {
                /* Emtpy lock file or error: may be another application
                   was writing its pid in it */
                snprintf(LogStr, sizeof(LogStr), "Can't read pid from lock file %s.", LockFile);
                LogStr[sizeof(LogStr) - 1] = '\0';
                LogMsg(LOG_NOTICE, LogStr);

                /* Lock process failed */
                return (LockKo);
            }

            /* Gets the pid of the locking process */
            HDBBuffer[N] = '\0';
            Pid = atoi(HDBBuffer);

            /* Check if it is our pid */
            if (Pid == LockPid) {
                /* File already locked by us */
                snprintf(LogStr, sizeof(LogStr), "Read our pid from lock %s.", LockFile);
                LogStr[sizeof(LogStr) - 1] = '\0';
                LogMsg(LOG_DEBUG, LogStr);

                /* Lock process succeded */
                return (LockOk);
            }

            /* Check if hte HDB header is valid and if the locking process
               is still alive */
            if ((Pid == 0) || ((kill(Pid, 0) != 0) && (errno == ESRCH)))
                /* Invalid lock, remove it */
                if (unlink(LockFile) == NoError) {
                    snprintf(LogStr, sizeof(LogStr),
                             "Removed stale lock %s (pid %d).", LockFile, Pid);
                    LogStr[sizeof(LogStr) - 1] = '\0';
                    LogMsg(LOG_NOTICE, LogStr);
                }
                else {
                    snprintf(LogStr, sizeof(LogStr),
                             "Couldn't remove stale lock %s (pid %d).", LockFile, Pid);
                    LogStr[sizeof(LogStr) - 1] = '\0';
                    LogMsg(LOG_ERR, LogStr);
                    return (LockKo);
                }
            else {
                /* The lock file is owned by another valid process */
                snprintf(LogStr, sizeof(LogStr), "Lock %s is owned by pid %d.", LockFile, Pid);
                LogStr[sizeof(LogStr) - 1] = '\0';
                LogMsg(LOG_INFO, LogStr);

                /* Lock process failed */
                return (Locked);
            }
        }
        else {
            /* Lock file creation problem */
            snprintf(LogStr, sizeof(LogStr), "Can't create lock file %s.", LockFile);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_ERR, LogStr);

            /* Lock process failed */
            return (LockKo);
        }
    }

    /* Prepare the HDB buffer with our pid */
    snprintf(HDBBuffer, sizeof(HDBBuffer), "%10d\n", (int) LockPid);
    LogStr[sizeof(HDBBuffer) - 1] = '\0';

    /* Fill the lock file with the HDB buffer */
    if (write(FileDes, HDBBuffer, HDBHeaderLen) != HDBHeaderLen) {
        /* Lock file creation problem, remove it */
        close(FileDes);
        snprintf(LogStr, sizeof(LogStr), "Can't write HDB header to lock file %s.", LockFile);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_ERR, LogStr);
        unlink(LockFile);

        /* Lock process failed */
        return (LockKo);
    }

    /* Closes the lock file */
    close(FileDes);

    /* Lock process succeded */
    return (LockOk);
}

/* Remove the lock file created with HDBLockFile */
static void
HDBUnlockFile(const char *LockFile, pid_t LockPid)
{
    char LogStr[TmpStrLen];

    /* Check if the lock file is still owned by us */
    if (HDBLockFile(LockFile, LockPid) == LockOk) {
        /* Remove the lock file */
        unlink(LockFile);
        snprintf(LogStr, sizeof(LogStr), "Unlocked lock file %s.", LockFile);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_NOTICE, LogStr);
    }
}

int
#ifndef ANDROID
OpenPort(const char *DeviceName, const char *LockFileName, PORTHANDLE * PortFd)
#else
OpenPort(const char *DeviceName, PORTHANDLE * PortFd)
#endif
{
    char LogStr[TmpStrLen];
    /* Actual port settings */
    struct termios PortSettings;

#ifndef ANDROID
    /* Try to lock the device */
    if (HDBLockFile(LockFileName, getpid()) != LockOk) {
        /* Lock failed */
        snprintf(LogStr, sizeof(LogStr), "Unable to lock %s. Exiting.", LockFileName);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_NOTICE, LogStr);
        return (Error);
    }
    else {
        /* Lock succeeded */
        snprintf(LogStr, sizeof(LogStr), "Device %s locked.", DeviceName);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_INFO, LogStr);
    }
#endif

    /* Open the device */
    if ((*PortFd = open(DeviceName, O_RDWR | O_NOCTTY | O_NONBLOCK, 0)) == OpenError) {
        return (Error);
    }

    /* Get the actual port settings */
    InitialPortSettings = &initialportsettings;
    tcgetattr(*PortFd, InitialPortSettings);
    tcgetattr(*PortFd, &PortSettings);
    UnixLogPortSettings(&PortSettings);

    /* Set the serial port to raw mode */
    cfmakeraw(&PortSettings);

    /* Enable HANGUP on close and disable modem control line handling */
    PortSettings.c_cflag = (PortSettings.c_cflag | HUPCL) | CLOCAL;

    /* Enable break handling */
    PortSettings.c_iflag = (PortSettings.c_iflag & ~IGNBRK) | BRKINT;

    /* Write the port settings to device */
    tcsetattr(*PortFd, TCSANOW, &PortSettings);

    return NoError;
}

#ifndef ANDROID
void
ClosePort(PORTHANDLE PortFd, const char *LockFileName)
#else
void
ClosePort(PORTHANDLE PortFd)
#endif
{
    /* Restores initial port settings */
    if (InitialPortSettings)
        tcsetattr(PortFd, TCSANOW, InitialPortSettings);

    /* Closes the device */
    close(PortFd);

    /* Removes the lock file */
#ifndef ANDROID
    HDBUnlockFile(LockFileName, getpid());
#endif

    /* Closes the log */
    if (!StdErrLogging) {
        closelog();
    }
}

/* Function called on many signals */
static void
SignalFunction(int unused)
{
    /* Just to avoid compilation warnings */
    /* There's no performance penalty in doing this 
       because this function is almost never called */
    unused = unused;

    /* Same as the exit function */
    ExitFunction();
}

void
PlatformInit()
{
#ifndef ANDROID
    if (!StdErrLogging) {
        openlog("sercd", LOG_PID, LOG_USER);
    }

    /* Register exit and signal handler functions */
    atexit(ExitFunction);
    signal(SIGHUP, SignalFunction);
    signal(SIGQUIT, SignalFunction);
    signal(SIGABRT, SignalFunction);
    signal(SIGTERM, SignalFunction);

    /* Register the function to be called on break condition */
    signal(SIGINT, BreakFunction);
#endif
}

/* Generic log function with log level control. Uses the same log levels
of the syslog(3) system call */
void
LogMsg(int LogLevel, const char *const Msg)
{
    if (LogLevel <= MaxLogLevel) {
#ifndef ANDROID
        if (StdErrLogging) {
            fprintf(stderr, "%s\n", Msg);
        }
        else {
            syslog(LogLevel, "%s", Msg);
        }
#else
        int prio;
        switch(LogLevel) {
        case LOG_EMERG:
        	prio = ANDROID_LOG_FATAL;
        	break;
        case LOG_ALERT:
        case LOG_CRIT:
        case LOG_ERR:
        	prio = ANDROID_LOG_ERROR;
        	break;
        case LOG_WARNING:
        	prio = ANDROID_LOG_WARN;
        	break;
        case LOG_NOTICE:
        case LOG_INFO:
        	prio = ANDROID_LOG_INFO;
        	break;
        case LOG_DEBUG:
        	prio = ANDROID_LOG_DEBUG;
        }
        __android_log_write(prio, "sercd", Msg);
#endif
    }
}


int
SercdSelect(PORTHANDLE * DeviceIn, PORTHANDLE * DeviceOut, PORTHANDLE * Modemstate,
            SERCD_SOCKET * SocketOut, SERCD_SOCKET * SocketIn,
            SERCD_SOCKET * SocketConnect, long PollInterval)
{
    fd_set InFdSet;
    fd_set OutFdSet;
    int highest_fd = -1, selret;
    struct timeval BTimeout;
    struct timeval newpoll;
    int ret = 0;

    FD_ZERO(&InFdSet);
    FD_ZERO(&OutFdSet);

    if (DeviceIn) {
        FD_SET(*DeviceIn, &InFdSet);
        highest_fd = MAX(highest_fd, *DeviceIn);
    }
    if (DeviceOut) {
        FD_SET(*DeviceOut, &OutFdSet);
        highest_fd = MAX(highest_fd, *DeviceOut);
    }
    if (SocketOut) {
        FD_SET(*SocketOut, &OutFdSet);
        highest_fd = MAX(highest_fd, *SocketOut);
    }
    if (SocketIn) {
        FD_SET(*SocketIn, &InFdSet);
        highest_fd = MAX(highest_fd, *SocketIn);
    }
    if (SocketConnect) {
        FD_SET(*SocketConnect, &InFdSet);
        highest_fd = MAX(highest_fd, *SocketConnect);
    }

    BTimeout.tv_sec = PollInterval / 1000;
    BTimeout.tv_usec = (PollInterval % 1000) * 1000;

    selret = select(highest_fd + 1, &InFdSet, &OutFdSet, NULL, &BTimeout);

    if (selret < 0)
        return selret;

    if (DeviceIn && FD_ISSET(*DeviceIn, &InFdSet)) {
        ret |= SERCD_EV_DEVICEIN;
    }
    if (DeviceOut && FD_ISSET(*DeviceOut, &OutFdSet)) {
        ret |= SERCD_EV_DEVICEOUT;
    }
    if (SocketOut && FD_ISSET(*SocketOut, &OutFdSet)) {
        ret |= SERCD_EV_SOCKETOUT;
    }
    if (SocketIn && FD_ISSET(*SocketIn, &InFdSet)) {
        ret |= SERCD_EV_SOCKETIN;
    }
    if (SocketConnect && FD_ISSET(*SocketConnect, &InFdSet)) {
        ret |= SERCD_EV_SOCKETCONNECT;
    }

    if (Modemstate) {
        gettimeofday(&newpoll, NULL);
        if (timercmp(&newpoll, &LastPoll, <)) {
            /* Time moved backwards */
            timerclear(&LastPoll);
        }
        newpoll.tv_sec -= PollInterval / 1000;
        newpoll.tv_usec -= PollInterval % 1000;
        if (timercmp(&LastPoll, &newpoll, <)) {
            gettimeofday(&LastPoll, NULL);
            ret |= SERCD_EV_MODEMSTATE;
        }
    }

    return ret;
}

void
NewListener(SERCD_SOCKET LSocketFd)
{

}

/* Drop client connection and close serial port */
#ifndef ANDROID
void
DropConnection(PORTHANDLE * DeviceFd, SERCD_SOCKET * InSocketFd, SERCD_SOCKET * OutSocketFd,
               const char *LockFileName)
#else
void
DropConnection(PORTHANDLE * DeviceFd, SERCD_SOCKET * InSocketFd, SERCD_SOCKET * OutSocketFd)
#endif
{
    if (DeviceFd) {
#ifndef ANDROID
        ClosePort(*DeviceFd, LockFileName);
#else
        ClosePort(*DeviceFd);
#endif
    }

    if (InSocketFd) {
        close(*InSocketFd);
    }

    if (OutSocketFd) {
        close(*OutSocketFd);
    }
}

ssize_t
WriteToDev(PORTHANDLE port, const void *buf, size_t count)
{
    return write(port, buf, count);
}

ssize_t
ReadFromDev(PORTHANDLE port, void *buf, size_t count)
{
    return read(port, buf, count);
}

ssize_t
WriteToNet(SERCD_SOCKET sock, const void *buf, size_t count)
{
    return write(sock, buf, count);
}

ssize_t
ReadFromNet(SERCD_SOCKET sock, void *buf, size_t count)
{
    return read(sock, buf, count);
}

void
ModemStateNotified()
{
}

#endif /* WIN32 */
