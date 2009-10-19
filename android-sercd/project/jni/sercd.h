/*
 * sercd
 * Copyright 2008 Peter Åstrand <astrand@cendio.se> for Cendio AB
 * see file COPYING for license details
 */

#ifndef SERCD_H
#define SERCD_H

#include "unix.h"
#ifndef ANDROID
#include "win.h"
#endif
#include <sys/types.h>

/* Standard boolean definition */
typedef enum
{ False, True }
Boolean;

/* Maximum length of temporary strings */
#define TmpStrLen 255

/* Error conditions constants */
#define NoError 0
#define Error 1
#define OpenError -1

/* Base Telnet protocol constants (STD 8) */
#define TNSE ((unsigned char) 240)
#define TNNOP ((unsigned char) 241)
#define TNSB ((unsigned char) 250)
#define TNWILL ((unsigned char) 251)
#define TNWONT ((unsigned char) 252)
#define TNDO ((unsigned char) 253)
#define TNDONT ((unsigned char) 254)
#define TNIAC ((unsigned char) 255)

/* Base Telnet protocol options constants (STD 27, STD 28, STD 29) */
#define TN_TRANSMIT_BINARY ((unsigned char) 0)
#define TN_ECHO ((unsigned char) 1)
#define TN_SUPPRESS_GO_AHEAD ((unsigned char) 3)

/* Base Telnet Com Port Control (CPC) protocol constants (RFC 2217) */
#define TNCOM_PORT_OPTION ((unsigned char) 44)

/* CPC Client to Access Server constants */
#define TNCAS_SIGNATURE ((unsigned char) 0)
#define TNCAS_SET_BAUDRATE ((unsigned char) 1)
#define TNCAS_SET_DATASIZE ((unsigned char) 2)
#define TNCAS_SET_PARITY ((unsigned char) 3)
#define TNCAS_SET_STOPSIZE ((unsigned char) 4)
#define TNCAS_SET_CONTROL ((unsigned char) 5)
#define TNCAS_NOTIFY_LINESTATE ((unsigned char) 6)
#define TNCAS_NOTIFY_MODEMSTATE ((unsigned char) 7)
#define TNCAS_FLOWCONTROL_SUSPEND ((unsigned char) 8)
#define TNCAS_FLOWCONTROL_RESUME ((unsigned char) 9)
#define TNCAS_SET_LINESTATE_MASK ((unsigned char) 10)
#define TNCAS_SET_MODEMSTATE_MASK ((unsigned char) 11)
#define TNCAS_PURGE_DATA ((unsigned char) 12)

/* CPC Access Server to Client constants */
#define TNASC_SIGNATURE ((unsigned char) 100)
#define TNASC_SET_BAUDRATE ((unsigned char) 101)
#define TNASC_SET_DATASIZE ((unsigned char) 102)
#define TNASC_SET_PARITY ((unsigned char) 103)
#define TNASC_SET_STOPSIZE ((unsigned char) 104)
#define TNASC_SET_CONTROL ((unsigned char) 105)
#define TNASC_NOTIFY_LINESTATE ((unsigned char) 106)
#define TNASC_NOTIFY_MODEMSTATE ((unsigned char) 107)
#define TNASC_FLOWCONTROL_SUSPEND ((unsigned char) 108)
#define TNASC_FLOWCONTROL_RESUME ((unsigned char) 109)
#define TNASC_SET_LINESTATE_MASK ((unsigned char) 110)
#define TNASC_SET_MODEMSTATE_MASK ((unsigned char) 111)
#define TNASC_PURGE_DATA ((unsigned char) 112)

/* CPC set parity */
#define TNCOM_PARITY_REQUEST ((unsigned char) 0)
#define TNCOM_NOPARITY ((unsigned char) 1)
#define TNCOM_ODDPARITY ((unsigned char) 2)
#define TNCOM_EVENPARITY ((unsigned char) 3)
#define TNCOM_MARKPARITY ((unsigned char) 4)
#define TNCOM_SPACEPARITY ((unsigned char) 5)

/* CPC set stopsize */
#define TNCOM_STOPSIZE_REQUEST ((unsigned char) 0)
#define TNCOM_ONESTOPBIT ((unsigned char) 1)
#define TNCOM_TWOSTOPBITS ((unsigned char) 2)
#define TNCOM_ONE5STOPBITS ((unsigned char) 3)

/* CPC commands */
#define TNCOM_CMD_FLOW_REQ ((unsigned char) 0)
#define TNCOM_CMD_FLOW_NONE ((unsigned char) 1)
#define TNCOM_CMD_FLOW_XONXOFF ((unsigned char) 2)
#define TNCOM_CMD_FLOW_HARDWARE ((unsigned char) 3)
#define TNCOM_CMD_BREAK_REQ ((unsigned char) 4)
#define TNCOM_CMD_BREAK_ON ((unsigned char) 5)
#define TNCOM_CMD_BREAK_OFF ((unsigned char) 6)
#define TNCOM_CMD_DTR_REQ ((unsigned char) 7)
#define TNCOM_CMD_DTR_ON ((unsigned char) 8)
#define TNCOM_CMD_DTR_OFF ((unsigned char) 9)
#define TNCOM_CMD_RTS_REQ ((unsigned char) 10)
#define TNCOM_CMD_RTS_ON ((unsigned char) 11)
#define TNCOM_CMD_RTS_OFF ((unsigned char) 12)
#define TNCOM_CMD_INFLOW_REQ ((unsigned char) 13)
#define TNCOM_CMD_INFLOW_NONE ((unsigned char) 14)
#define TNCOM_CMD_INFLOW_XONXOFF ((unsigned char) 15)
#define TNCOM_CMD_INFLOW_HARDWARE ((unsigned char) 16)
#define TNCOM_CMD_FLOW_DCD ((unsigned char) 17)
#define TNCOM_CMD_INFLOW_DTR ((unsigned char) 18)
#define TNCOM_CMD_FLOW_DSR ((unsigned char) 19)

/* CPC linestate mask and notifies */
#define TNCOM_LINEMASK_TIMEOUT ((unsigned char) 128)
#define TNCOM_LINEMASK_TRANS_SHIFT_EMTPY ((unsigned char) 64)
#define TNCOM_LINEMASK_TRANS_HOLD_EMPTY ((unsigned char) 32)
#define TNCOM_LINEMASK_BREAK_ERR ((unsigned char) 16)
#define TNCOM_LINEMASK_FRAME_ERR ((unsigned char) 8)
#define TNCOM_LINEMASK_PARITY_ERR ((unsigned char) 4)
#define TNCOM_LINEMASK_OVERRUN_ERR ((unsigned char) 2)
#define TNCOM_LINEMASK_DATA_RD ((unsigned char) 1)

/* CPC modemstate mask and notifies */
#define TNCOM_MODMASK_RLSD ((unsigned char) 128)
#define TNCOM_MODMASK_RING ((unsigned char) 64)
#define TNCOM_MODMASK_DSR ((unsigned char) 32)
#define TNCOM_MODMASK_CTS ((unsigned char) 16)
#define TNCOM_MODMASK_NODELTA (TNCOM_MODMASK_RLSD|TNCOM_MODMASK_RING|TNCOM_MODMASK_DSR|TNCOM_MODMASK_CTS)
#define TNCOM_MODMASK_RLSD_DELTA ((unsigned char) 8)
#define TNCOM_MODMASK_RING_TRAIL ((unsigned char) 4)
#define TNCOM_MODMASK_DSR_DELTA ((unsigned char) 2)
#define TNCOM_MODMASK_CTS_DELTA ((unsigned char) 1)

/* CPC purge data */
#define TNCOM_PURGE_RX ((unsigned char) 1)
#define TNCOM_PURGE_TX ((unsigned char) 2)
#define TNCOM_PURGE_BOTH ((unsigned char) 3)

/* Generic log function with log level control. Uses the same log levels
of the syslog(3) system call */
void LogMsg(int LogLevel, const char *const Msg);

/* Function executed when the program exits */
void ExitFunction(void);

/* Function called on break signal */
void BreakFunction(int unused);

/* Abstract platform-independent select function */
int SercdSelect(PORTHANDLE *DeviceIn, PORTHANDLE *DeviceOut, PORTHANDLE *Modemstate,
                SERCD_SOCKET *SocketOut, SERCD_SOCKET *SocketIn,
                SERCD_SOCKET *SocketConnect, long PollInterval);
#define SERCD_EV_DEVICEIN 1
#define SERCD_EV_DEVICEOUT 2
#define SERCD_EV_SOCKETOUT 4
#define SERCD_EV_SOCKETIN 8
#define SERCD_EV_SOCKETCONNECT 16
#define SERCD_EV_MODEMSTATE 32

/* macros */
#ifndef MAX
#define MAX(x,y)                (((x) > (y)) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x,y)                (((x) > (y)) ? (y) : (x))
#endif

void NewListener(SERCD_SOCKET LSocketFd);
#ifndef ANDROID
void DropConnection(PORTHANDLE * DeviceFd, SERCD_SOCKET * InSocketFd, SERCD_SOCKET * OutSocketFd, 
                    const char *LockFileName);
#else
void DropConnection(PORTHANDLE * DeviceFd, SERCD_SOCKET * InSocketFd, SERCD_SOCKET * OutSocketFd);
#endif
ssize_t WriteToDev(PORTHANDLE port, const void *buf, size_t count);
ssize_t ReadFromDev(PORTHANDLE port, void *buf, size_t count);
ssize_t WriteToNet(SERCD_SOCKET sock, const void *buf, size_t count);
ssize_t ReadFromNet(SERCD_SOCKET sock,  void *buf, size_t count);
void ModemStateNotified();
void LogPortSettings(unsigned long speed, unsigned char datasize, unsigned char parity,
                     unsigned char stopsize, unsigned char outflow, unsigned char inflow);
#endif /* SERCD_H */
