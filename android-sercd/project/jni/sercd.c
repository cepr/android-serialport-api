/*
    sercd: RFC 2217 compliant serial port redirector
    Copyright 2003-2008 Peter Åstrand <astrand@cendio.se> for Cendio AB
    Copyright (C) 1999 - 2003 InfoTecna s.r.l.
    Copyright (C) 2001, 2002 Trustees of Columbia University
    in the City of New York

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Current design issues:

      . does not properly check implement BREAK handling. Need to figure
        out how to turn a BREAK on and then off based upon receipt of
        COM-PORT Subnegotiations

      . Lack of login processing

      . Lack of Telnet START_TLS to protect the data stream

      . Lack of Telnet AUTHENTICATION

      . LineState processing is not implemented

      . The code probably won't compile on most versions of Unix due to the
        highly platform dependent nature of the serial apis.
        
*/

/* Return NoError, which is 0, on success */

/* Standard library includes */
#include <stdio.h>              /* snprintf */
#include <stdlib.h>             /* atoi */
#include <string.h>             /* strlen */
#include <unistd.h>             /* close */
#include <errno.h>              /* errno */
#include <time.h>               /* CLOCKS_PER_SEC */
#include <fcntl.h>              /* open */
#include <assert.h>             /* assert */
#include "sercd.h"
#include "unix.h"
#ifndef ANDROID
#include "win.h"
#endif
#ifdef ANDROID
#include <jni.h>
#include "android.h"
#endif

/* Buffer size */
#define BufferSize 2048

/* Cisco IOS bug compatibility */
Boolean CiscoIOSCompatible = False;

/* Log to stderr instead of syslog */
Boolean StdErrLogging = False;

/* Buffer structure */
typedef struct
{
    unsigned char Buffer[BufferSize];
    unsigned int RdPos;
    unsigned int WrPos;
}
BufferType;

#ifndef ANDROID
/* Complete lock file pathname */
static char *LockFileName;
#endif

/* Complete device file pathname */
static char *DeviceName;

/* Device file descriptor */
static PORTHANDLE *DeviceFd = NULL;

/* Network sockets */
static SERCD_SOCKET *InSocketFd = NULL;
static SERCD_SOCKET *OutSocketFd = NULL;

/* Com Port Control enabled flag */
Boolean PortControlEnable = True;

/* Maximum log level to log in the system log */
int MaxLogLevel = LOG_DEBUG + 1;

/* Status enumeration for IAC escaping and interpretation */
typedef enum
{ IACNormal, IACReceived, IACComReceiving }
IACState;

/* Effective status for IAC escaping and interpretation */
static IACState IACEscape = IACNormal;

/* Same as above during signature reception */
static IACState IACSigEscape;

/* Current IAC command begin received */
static unsigned char IACCommand[TmpStrLen];

/* Position of insertion into IACCommand[] */
static size_t IACPos;

/* Modem state mask set by the client */
static unsigned char ModemStateMask = ((unsigned char) 255);

/* Line state mask set by the client */
static unsigned char LineStateMask = ((unsigned char) 0);

#ifdef COMMENT
/* Current status of the line control lines */
static unsigned char LineState = ((unsigned char) 0);
#endif

/* Current status of the modem control lines */
static unsigned char ModemState = ((unsigned char) 0);

/* Break state flag */
Boolean BreakSignaled = False;

/* Input flow control flag */
Boolean InputFlow = True;

/* Telnet State Machine */
static struct _tnstate
{
    int sent_will:1;
    int sent_do:1;
    int sent_wont:1;
    int sent_dont:1;
    int is_will:1;
    int is_do:1;
}
tnstate[256];

/* Function prototypes */

/* initialize Telnet State Machine */
void InitTelnetStateMachine(void);

/* Initialize a buffer for operation */
void InitBuffer(BufferType * B);

/* Check if the buffer is empty */
Boolean IsBufferEmpty(BufferType * B);

/* Add a byte to a buffer */
void AddToBuffer(BufferType * B, unsigned char C);

/* Get a byte from a buffer */
unsigned char GetFromBuffer(BufferType * B);

/* Retrieves the port speed from PortFd */
unsigned long int GetPortSpeed(PORTHANDLE PortFd);

/* Retrieves the data size from PortFd */
unsigned char GetPortDataSize(PORTHANDLE PortFd);

/* Retrieves the parity settings from PortFd */
unsigned char GetPortParity(PORTHANDLE PortFd);

/* Retrieves the stop bits size from PortFd */
unsigned char GetPortStopSize(PORTHANDLE PortFd);

/* Retrieves the flow control status, including DTR and RTS status,
from PortFd */
unsigned char GetPortFlowControl(PORTHANDLE PortFd, unsigned char Which);

/* Return the status of the modem control lines (DCD, CTS, DSR, RNG) */
unsigned char GetModemState(PORTHANDLE PortFd, unsigned char PMState);

/* Set the serial port data size */
void SetPortDataSize(PORTHANDLE PortFd, unsigned char DataSize);

/* Set the serial port parity */
void SetPortParity(PORTHANDLE PortFd, unsigned char Parity);

/* Set the serial port stop bits size */
void SetPortStopSize(PORTHANDLE PortFd, unsigned char StopSize);

/* Set the port flow control and DTR and RTS status */
void SetPortFlowControl(PORTHANDLE PortFd, unsigned char How);

/* Set the serial port speed */
void SetPortSpeed(PORTHANDLE PortFd, unsigned long BaudRate);

/* Serial port break */
void SetBreak(PORTHANDLE PortFd, Boolean on);

/* Flush serial port */
void SetFlush(PORTHANDLE PortFd, int selector);

/* Init platform subsystems, such as the syslog */
void PlatformInit();

/* Initialize port */
#ifndef ANDROID
int OpenPort(const char *DeviceName, const char *LockFileName, PORTHANDLE * PortFd);
#else
int OpenPort(const char *DeviceName, PORTHANDLE * PortFd);
#endif

/* Close and uninit port */
#ifndef ANDROID
void ClosePort(PORTHANDLE PortFd, const char *LockFileName);
#else
void ClosePort(PORTHANDLE PortFd);
#endif

/* Send the signature Sig to the client */
void SendSignature(BufferType * B, char *Sig);

/* Write a char to SockFd performing IAC escaping */
void EscWriteChar(BufferType * B, unsigned char C);

/* Redirect char C to PortFd checking for IAC escape sequences */
void EscRedirectChar(BufferType * SockB, BufferType * DevB, PORTHANDLE PortFd, unsigned char C);

/* Send the specific telnet option to SockFd using Command as command */
void SendTelnetOption(BufferType * B, unsigned char Command, char Option);

/* Send a string to SockFd performing IAC escaping */
void SendStr(BufferType * B, char *Str);

/* Send the baud rate BR to SockFd */
void SendBaudRate(BufferType * B, unsigned long int BR);

/* Send the CPC command Command using Parm as parameter */
void SendCPCByteCommand(BufferType * B, unsigned char Command, unsigned char Parm);

/* Handling of COM Port Control specific commands */
void HandleCPCCommand(BufferType * B, PORTHANDLE PortFd, unsigned char *Command, size_t CSize);

/* Common telnet IAC commands handling */
void HandleIACCommand(BufferType * B, PORTHANDLE PortFd, unsigned char *Command, size_t CSize);

/* Write a buffer to SockFd with IAC escaping */
void EscWriteBuffer(BufferType * B, unsigned char *Buffer, unsigned int BSize);

/* initialize Telnet State Machine */
void
InitTelnetStateMachine(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        tnstate[i].sent_do = 0;
        tnstate[i].sent_will = 0;
        tnstate[i].sent_wont = 0;
        tnstate[i].sent_dont = 0;
        tnstate[i].is_do = 0;
        tnstate[i].is_will = 0;
    }
}

/* Setup sockets for low latency and automatic keepalive; doesn't
 * check if anything fails because failure doesn't prevent correct
 * functioning but only provides slightly worse behaviour
 */
void
SetSocketOptions(SERCD_SOCKET insocket, SERCD_SOCKET outsocket)
{
    /* Socket setup flag */
    int SockParmEnable = 1;

    setsockopt(insocket, SOL_SOCKET, SO_KEEPALIVE, (char *) &SockParmEnable,
               sizeof(SockParmEnable));
    setsockopt(insocket, SOL_SOCKET, SO_OOBINLINE, (char *) &SockParmEnable,
               sizeof(SockParmEnable));
    setsockopt(outsocket, SOL_SOCKET, SO_KEEPALIVE, (char *) &SockParmEnable,
               sizeof(SockParmEnable));
#ifndef WIN32
    /* Generic socket parameter */
    int SockParm;

    SockParm = IPTOS_LOWDELAY;
    setsockopt(insocket, SOL_IP, IP_TOS, &SockParm, sizeof(SockParm));
    setsockopt(outsocket, SOL_IP, IP_TOS, &SockParm, sizeof(SockParm));

    /* Make reads/writes non-blocking. In principle, non-blocking IO
       is not necessary, since we are using select. However, the Linux
       select man page BUGS section contains: "Under Linux, select()
       may report a socket file descriptor as "ready for reading",
       while nevertheless a subsequent read blocks." Besides, we need
       to use non-blocking for the serial port anyway, and using
       non-blocking means that we don't need to check the send buffer
       size. */
    ioctl(outsocket, FIONBIO, &SockParmEnable);
    ioctl(insocket, FIONBIO, &SockParmEnable);
#endif
}

/* Initialize a buffer for operation */
void
InitBuffer(BufferType * B)
{
    /* Set the initial buffer positions */
    B->RdPos = 0;
    B->WrPos = 0;
}


/* Return the length of the data in the buffer */
unsigned int
BufferLength(BufferType * B)
{
    return (B->WrPos - B->RdPos + BufferSize) % BufferSize;
}

/* Return how much room is left */
unsigned int
BufferRoomLeft(BufferType * B)
{
    /* -1 is for full/empty distinction */
    return BufferSize - 1 - BufferLength(B);
}

/* Check if there's room for a number of additional bytes */
Boolean
BufferHasRoomFor(BufferType * B, unsigned int x)
{
    return BufferRoomLeft(B) >= x;
}

/* Check if the buffer is empty */
Boolean
IsBufferEmpty(BufferType * B)
{
    return BufferLength(B) == 0;
}

/* Add a byte to a buffer. */
void
AddToBuffer(BufferType * B, unsigned char C)
{
    assert(BufferHasRoomFor(B, 1));

    B->Buffer[B->WrPos] = C;
    B->WrPos = (B->WrPos + 1) % BufferSize;
}

/* Get a byte from a buffer */
unsigned char
GetFromBuffer(BufferType * B)
{
    unsigned char C = B->Buffer[B->RdPos];
    B->RdPos = (B->RdPos + 1) % BufferSize;
    return (C);
}

/* Get string from buffer, without removing it. Returns the length of
   the string. */
unsigned char *
GetBufferString(BufferType * B, unsigned int *len)
{
    if (B->RdPos <= B->WrPos)
        *len = B->WrPos - B->RdPos;
    else
        *len = BufferSize - B->RdPos;

    return &(B->Buffer[B->RdPos]);
}

/* Remove the number of read bytes specified */
void
BufferPopBytes(BufferType * B, unsigned int len)
{
    B->RdPos += len;
    B->RdPos %= BufferSize;
}

/* Function executed when the program exits */
void
ExitFunction(void)
{
#ifndef ANDROID
    DropConnection(DeviceFd, InSocketFd, OutSocketFd, LockFileName);
#else
    DropConnection(DeviceFd, InSocketFd, OutSocketFd);
#endif

    /* Program termination notification */
    LogMsg(LOG_NOTICE, "sercd stopped.");
}

#ifndef ANDROID
/* Function called on break signal */
/* Unimplemented yet */
void
BreakFunction(int unused)
{
#ifndef COMMENT
    /* Just to avoid compilation warnings */
    /* There's no performance penalty in doing this 
       because this function is almost never called */
    unused = unused;

    /* ExitFunction will be called through atexit */
    exit(NoError);
#else /* COMMENT */

    unsigned char LineState;

    if (BreakSignaled == True) {
        BreakSignaled = False;
        LineState = 0;
    }
    else {
        BreakSignaled = True;
        LineState = 16;
    }

    /* Notify client of break change */
    if ((LineStateMask & (unsigned char) 16) != 0) {
        LogMsg(LOG_DEBUG, "Notifying break change.");
        SendCPCByteCommand(&ToNetBuf, TNASC_NOTIFY_LINESTATE, LineState);
    }
#endif /* COMMENT */
}
#endif

/* Send the signature Sig to the client. Sig must not be longer than
   255 characters. */
#define SendSignature_bytes (6 + 2 * 255)
void
SendSignature(BufferType * B, char *Sig)
{
    assert(strlen(Sig) <= 255);
    AddToBuffer(B, TNIAC);
    AddToBuffer(B, TNSB);
    AddToBuffer(B, TNCOM_PORT_OPTION);
    AddToBuffer(B, TNASC_SIGNATURE);
    SendStr(B, Sig);
    AddToBuffer(B, TNIAC);
    AddToBuffer(B, TNSE);
}

/* Write a char to socket performing IAC escaping */
#define EscWriteChar_bytes 2
void
EscWriteChar(BufferType * B, unsigned char C)
{
    /* Last received byte */
    static unsigned char Last = 0;

    if (C == TNIAC)
        AddToBuffer(B, C);
    else if (C != 0x0A && !tnstate[TN_TRANSMIT_BINARY].is_will && Last == 0x0D)
        AddToBuffer(B, 0x00);
    AddToBuffer(B, C);

    /* Set last received byte */
    Last = C;
}

/* Redirect char C to Device checking for IAC escape sequences */
#define EscRedirectChar_bytes_SockB HandleIACCommand_bytes
#define EscRedirectChar_bytes_DevB 1
void
EscRedirectChar(BufferType * SockB, BufferType * DevB, PORTHANDLE PortFd, unsigned char C)
{
    /* Last received byte */
    static unsigned char Last = 0;

    /* Check the IAC escape status */
    switch (IACEscape) {
        /* Normal status */
    case IACNormal:
        if (C == TNIAC)
            IACEscape = IACReceived;
        else if (!tnstate[TN_TRANSMIT_BINARY].is_do && C == 0x00 && Last == 0x0D)
            /* Swallow the NUL after a CR if not receiving BINARY */
            break;
        else
            AddToBuffer(DevB, C);
        break;

        /* IAC previously received */
    case IACReceived:
        if (C == TNIAC) {
            AddToBuffer(DevB, C);
            IACEscape = IACNormal;
        }
        else {
            IACCommand[0] = TNIAC;
            IACCommand[1] = C;
            IACPos = 2;
            IACEscape = IACComReceiving;
            IACSigEscape = IACNormal;
        }
        break;

        /* IAC Command reception */
    case IACComReceiving:
        /* Telnet suboption, could be only CPC */
        if (IACCommand[1] == TNSB) {
            /* Get the suboption signature */
            if (IACPos < 4) {
                IACCommand[IACPos] = C;
                IACPos++;
            }
            else {
                /* Check which suboption we are dealing with */
                switch (IACCommand[3]) {
                    /* Signature, which needs further escaping */
                case TNCAS_SIGNATURE:
                    switch (IACSigEscape) {
                    case IACNormal:
                        if (C == TNIAC)
                            IACSigEscape = IACReceived;
                        else if (IACPos < sizeof(IACCommand)) {
                            IACCommand[IACPos] = C;
                            IACPos++;
                        }
                        break;

                    case IACComReceiving:
                        IACSigEscape = IACNormal;
                        break;

                    case IACReceived:
                        if (C == TNIAC) {
                            if (IACPos < sizeof(IACCommand)) {
                                IACCommand[IACPos] = C;
                                IACPos++;
                            }
                            IACSigEscape = IACNormal;
                        }
                        else {
                            if (IACPos < sizeof(IACCommand)) {
                                IACCommand[IACPos] = TNIAC;
                                IACPos++;
                            }

                            if (IACPos < sizeof(IACCommand)) {
                                IACCommand[IACPos] = C;
                                IACPos++;
                            }

                            HandleIACCommand(SockB, PortFd, IACCommand, IACPos);
                            IACEscape = IACNormal;
                        }
                        break;
                    }
                    break;

                    /* Set baudrate */
                case TNCAS_SET_BAUDRATE:
                    IACCommand[IACPos] = C;
                    IACPos++;

                    if (IACPos == 10) {
                        HandleIACCommand(SockB, PortFd, IACCommand, IACPos);
                        IACEscape = IACNormal;
                    }
                    break;

                    /* Flow control command */
                case TNCAS_FLOWCONTROL_SUSPEND:
                case TNCAS_FLOWCONTROL_RESUME:
                    IACCommand[IACPos] = C;
                    IACPos++;

                    if (IACPos == 6) {
                        HandleIACCommand(SockB, PortFd, IACCommand, IACPos);
                        IACEscape = IACNormal;
                    }
                    break;

                    /* Normal CPC command with single byte parameter */
                default:
                    IACCommand[IACPos] = C;
                    IACPos++;

                    if (IACPos == 7) {
                        HandleIACCommand(SockB, PortFd, IACCommand, IACPos);
                        IACEscape = IACNormal;
                    }
                    break;
                }
            }
        }
        else {
            /* Normal 3 byte IAC option */
            IACCommand[IACPos] = C;
            IACPos++;

            if (IACPos == 3) {
                HandleIACCommand(SockB, PortFd, IACCommand, IACPos);
                IACEscape = IACNormal;
            }
        }
        break;
    }

    /* Set last received byte */
    Last = C;
}

/* Send the specific telnet option to SockFd using Command as command */
#define SendTelnetOption_bytes 3
void
SendTelnetOption(BufferType * B, unsigned char Command, char Option)
{
    unsigned char IAC = TNIAC;

    AddToBuffer(B, IAC);
    AddToBuffer(B, Command);
    AddToBuffer(B, Option);
}

/* Send initial Telnet negotiations to the client */
#define SendTelnetInitialOptions_bytes (SendTelnetOption_bytes*3)
void
SendTelnetInitialOptions(BufferType * B)
{
    SendTelnetOption(B, TNWILL, TN_TRANSMIT_BINARY);
    tnstate[TN_TRANSMIT_BINARY].sent_will = 1;
    SendTelnetOption(B, TNDO, TN_TRANSMIT_BINARY);
    tnstate[TN_TRANSMIT_BINARY].sent_do = 1;
    SendTelnetOption(B, TNWILL, TN_ECHO);
    tnstate[TN_ECHO].sent_will = 1;
    SendTelnetOption(B, TNWILL, TN_SUPPRESS_GO_AHEAD);
    tnstate[TN_SUPPRESS_GO_AHEAD].sent_will = 1;
    SendTelnetOption(B, TNDO, TN_SUPPRESS_GO_AHEAD);
    tnstate[TN_SUPPRESS_GO_AHEAD].sent_do = 1;
    SendTelnetOption(B, TNDO, TNCOM_PORT_OPTION);
    tnstate[TNCOM_PORT_OPTION].sent_do = 1;
}

/* Send a string to SockFd performing IAC escaping
   Max buffer fill: 2*len(Str) */
void
SendStr(BufferType * B, char *Str)
{
    size_t I;
    size_t L;

    L = strlen(Str);

    for (I = 0; I < L; I++)
        EscWriteChar(B, (unsigned char) Str[I]);
}

/* Send the baud rate BR to Buffer */
#define SendBaudRate_bytes (6 + 2*sizeof(unsigned long int))
void
SendBaudRate(BufferType * B, unsigned long int BR)
{
    unsigned char *p;
    unsigned long int NBR;
    int i;

    NBR = htonl(BR);

    AddToBuffer(B, TNIAC);
    AddToBuffer(B, TNSB);
    AddToBuffer(B, TNCOM_PORT_OPTION);
    AddToBuffer(B, TNASC_SET_BAUDRATE);
    p = (unsigned char *) &NBR;
    for (i = 0; i < (int) sizeof(NBR); i++)
        EscWriteChar(B, p[i]);
    AddToBuffer(B, TNIAC);
    AddToBuffer(B, TNSE);
}

/* Send the CPC command Command using Parm as parameter */
#define SendCPCByteCommand_bytes 8
void
SendCPCByteCommand(BufferType * B, unsigned char Command, unsigned char Parm)
{
    AddToBuffer(B, TNIAC);
    AddToBuffer(B, TNSB);
    AddToBuffer(B, TNCOM_PORT_OPTION);
    AddToBuffer(B, Command);
    EscWriteChar(B, Parm);
    AddToBuffer(B, TNIAC);
    AddToBuffer(B, TNSE);
}

/* Handling of COM Port Control specific commands */
#define HandleCPCCommand_bytes \
 MAX(SendSignature_bytes, MAX(SendBaudRate_bytes, SendCPCByteCommand_bytes))
void
HandleCPCCommand(BufferType * SockB, PORTHANDLE PortFd, unsigned char *Command, size_t CSize)
{
    char LogStr[TmpStrLen];
    char SigStr[255];
    unsigned long int BaudRate;
    unsigned char DataSize;
    unsigned char Parity;
    unsigned char StopSize;
    unsigned char FlowControl;

    /* Check wich command has been requested */
    switch (Command[3]) {
        /* Signature */
    case TNCAS_SIGNATURE:
        if (CSize == 6) {
            /* Void signature, client is asking for our signature */
            snprintf(SigStr, sizeof(SigStr), "sercd %s %s", VERSION, DeviceName);
            LogStr[sizeof(SigStr) - 1] = '\0';
            SendSignature(SockB, SigStr);
            snprintf(LogStr, sizeof(LogStr), "Sent signature: %s", SigStr);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_INFO, LogStr);
        }
        else {
            /* Received client signature */
            strncpy(SigStr, (char *) &Command[4], MAX(CSize - 6, sizeof(SigStr) - 1));
            snprintf(LogStr, sizeof(LogStr) - 1, "Received client signature: %s", SigStr);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_INFO, LogStr);
        }
        break;

        /* Set serial baud rate */
    case TNCAS_SET_BAUDRATE:
        /* Retrieve the baud rate which is in network order */
        BaudRate = ntohl(*((unsigned long int *) &Command[4]));

        if (BaudRate == 0)
            /* Client is asking for current baud rate */
            LogMsg(LOG_DEBUG, "Baud rate notification received.");
        else {
            /* Change the baud rate */
            snprintf(LogStr, sizeof(LogStr), "Port baud rate change to %lu requested.", BaudRate);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            SetPortSpeed(PortFd, BaudRate);
        }

        /* Send confirmation */
        BaudRate = GetPortSpeed(PortFd);
        SendBaudRate(SockB, BaudRate);
        snprintf(LogStr, sizeof(LogStr), "Port baud rate: %lu", BaudRate);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        break;

        /* Set serial data size */
    case TNCAS_SET_DATASIZE:
        if (Command[4] == 0)
            /* Client is asking for current data size */
            LogMsg(LOG_DEBUG, "Data size notification requested.");
        else {
            /* Set the data size */
            snprintf(LogStr, sizeof(LogStr),
                     "Port data size change to %u requested.", (unsigned int) Command[4]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            SetPortDataSize(PortFd, Command[4]);
        }

        /* Send confirmation */
        DataSize = GetPortDataSize(PortFd);
        SendCPCByteCommand(SockB, TNASC_SET_DATASIZE, DataSize);
        snprintf(LogStr, sizeof(LogStr), "Port data size: %u", (unsigned int) DataSize);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        break;

        /* Set the serial parity */
    case TNCAS_SET_PARITY:
        if (Command[4] == 0)
            /* Client is asking for current parity */
            LogMsg(LOG_DEBUG, "Parity notification requested.");
        else {
            /* Set the parity */
            snprintf(LogStr, sizeof(LogStr),
                     "Port parity change to %u requested", (unsigned int) Command[4]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            SetPortParity(PortFd, Command[4]);
        }

        /* Send confirmation */
        Parity = GetPortParity(PortFd);
        SendCPCByteCommand(SockB, TNASC_SET_PARITY, Parity);
        snprintf(LogStr, sizeof(LogStr), "Port parity: %u", (unsigned int) Parity);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        break;

        /* Set the serial stop size */
    case TNCAS_SET_STOPSIZE:
        if (Command[4] == 0)
            /* Client is asking for current stop size */
            LogMsg(LOG_DEBUG, "Stop size notification requested.");
        else {
            /* Set the stop size */
            snprintf(LogStr, sizeof(LogStr),
                     "Port stop size change to %u requested.", (unsigned int) Command[4]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            SetPortStopSize(PortFd, Command[4]);
        }

        /* Send confirmation */
        StopSize = GetPortStopSize(PortFd);
        SendCPCByteCommand(SockB, TNASC_SET_STOPSIZE, StopSize);
        snprintf(LogStr, sizeof(LogStr), "Port stop size: %u", (unsigned int) StopSize);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        break;

        /* Flow control and DTR/RTS handling */
    case TNCAS_SET_CONTROL:
        switch (Command[4]) {
        case TNCOM_CMD_FLOW_REQ:
        case TNCOM_CMD_DTR_REQ:
        case TNCOM_CMD_RTS_REQ:
        case TNCOM_CMD_INFLOW_REQ:
            /* Client is asking for current flow control or DTR/RTS status */
            LogMsg(LOG_DEBUG, "Flow control notification requested.");
            FlowControl = GetPortFlowControl(PortFd, Command[4]);
            SendCPCByteCommand(SockB, TNASC_SET_CONTROL, FlowControl);
            snprintf(LogStr, sizeof(LogStr), "Port flow control: %u", (unsigned int) FlowControl);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            break;

        case TNCOM_CMD_BREAK_REQ:
            if (BreakSignaled) {
                SendCPCByteCommand(SockB, TNASC_SET_CONTROL, TNCOM_CMD_BREAK_ON);
            }
            else {
                SendCPCByteCommand(SockB, TNASC_SET_CONTROL, TNCOM_CMD_BREAK_OFF);
            }
            break;

        case TNCOM_CMD_BREAK_ON:
            /* Break command */
            SetBreak(PortFd, True);
            BreakSignaled = True;
            LogMsg(LOG_DEBUG, "Break Signal ON.");
            SendCPCByteCommand(SockB, TNASC_SET_CONTROL, TNCOM_CMD_BREAK_ON);
            break;

        case TNCOM_CMD_BREAK_OFF:
            SetBreak(PortFd, False);
            BreakSignaled = False;
            LogMsg(LOG_DEBUG, "Break Signal OFF.");
            SendCPCByteCommand(SockB, TNASC_SET_CONTROL, TNCOM_CMD_BREAK_OFF);
            break;

        default:
            /* Set the flow control */
            snprintf(LogStr, sizeof(LogStr),
                     "Port flow control change to %u requested.", (unsigned int) Command[4]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            SetPortFlowControl(PortFd, Command[4]);

            /* Flow control status confirmation */
            if (CiscoIOSCompatible && Command[4] >= TNCOM_CMD_INFLOW_REQ
                && Command[4] <= TNCOM_CMD_INFLOW_HARDWARE)
                /* INBOUND not supported separately.
                   Following the behavior of Cisco ISO 11.3
                 */
                FlowControl = 0;
            else
                /* Return the actual port flow control settings */
                FlowControl = GetPortFlowControl(PortFd, TNCOM_CMD_FLOW_REQ);

            SendCPCByteCommand(SockB, TNASC_SET_CONTROL, FlowControl);
            snprintf(LogStr, sizeof(LogStr), "Port flow control: %u", (unsigned int) FlowControl);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            break;
        }
        break;

        /* Set the line state mask */
    case TNCAS_SET_LINESTATE_MASK:
        snprintf(LogStr, sizeof(LogStr), "Line state set to %u", (unsigned int) Command[4]);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);

        /* Only break notification supported */
        LineStateMask = Command[4] & (unsigned char) 16;
        SendCPCByteCommand(SockB, TNASC_SET_LINESTATE_MASK, LineStateMask);
        break;

        /* Set the modem state mask */
    case TNCAS_SET_MODEMSTATE_MASK:
        snprintf(LogStr, sizeof(LogStr), "Modem state mask set to %u", (unsigned int) Command[4]);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        ModemStateMask = Command[4];
        SendCPCByteCommand(SockB, TNASC_SET_MODEMSTATE_MASK, ModemStateMask);
        break;

        /* Port flush requested */
    case TNCAS_PURGE_DATA:
        snprintf(LogStr, sizeof(LogStr), "Port flush %u requested.", (unsigned int) Command[4]);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        SetFlush(PortFd, Command[4]);
        SendCPCByteCommand(SockB, TNASC_PURGE_DATA, Command[4]);
        break;

        /* Suspend output to the client */
    case TNCAS_FLOWCONTROL_SUSPEND:
        LogMsg(LOG_DEBUG, "Flow control suspend requested.");
        InputFlow = False;
        break;

        /* Resume output to the client */
    case TNCAS_FLOWCONTROL_RESUME:
        LogMsg(LOG_DEBUG, "Flow control resume requested.");
        InputFlow = True;
        break;

        /* Unknown request */
    default:
        snprintf(LogStr, sizeof(LogStr), "Unhandled request %u", (unsigned int) Command[3]);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        break;
    }
}

/* Common telnet IAC commands handling */
#define HandleIACCommand_bytes MAX(HandleCPCCommand_bytes, SendTelnetOption_bytes)
void
HandleIACCommand(BufferType * SockB, PORTHANDLE PortFd, unsigned char *Command, size_t CSize)
{
    char LogStr[TmpStrLen];

    /* Check which command */
    switch (Command[1]) {
        /* Suboptions */
    case TNSB:
        if (!(tnstate[Command[2]].is_will || tnstate[Command[2]].is_do))
            break;

        switch (Command[2]) {
            /* RFC 2217 COM Port Control Protocol option */
        case TNCOM_PORT_OPTION:
            HandleCPCCommand(SockB, PortFd, Command, CSize);
            break;

        default:
            snprintf(LogStr, sizeof(LogStr), "Unknown suboption received: %u",
                     (unsigned int) Command[2]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            break;
        }
        break;

        /* Requests for options */
    case TNWILL:
        switch (Command[2]) {
            /* COM Port Control Option */
        case TNCOM_PORT_OPTION:
            LogMsg(LOG_INFO, "Telnet COM Port Control Enabled (WILL).");
            PortControlEnable = True;
            if (!tnstate[Command[2]].sent_do) {
                SendTelnetOption(SockB, TNDO, Command[2]);
            }
            tnstate[Command[2]].is_do = 1;
            break;

            /* Telnet Binary mode */
        case TN_TRANSMIT_BINARY:
            LogMsg(LOG_INFO, "Telnet Binary Transfer Enabled (WILL).");
            if (!tnstate[Command[2]].sent_do)
                SendTelnetOption(SockB, TNDO, Command[2]);
            tnstate[Command[2]].is_do = 1;
            break;

            /* Echo request not handled */
        case TN_ECHO:
            LogMsg(LOG_INFO, "Rejecting Telnet Echo Option (WILL).");
            if (!tnstate[Command[2]].sent_do)
                SendTelnetOption(SockB, TNDO, Command[2]);
            tnstate[Command[2]].is_do = 1;
            break;

            /* No go ahead needed */
        case TN_SUPPRESS_GO_AHEAD:
            LogMsg(LOG_INFO, "Suppressing Go Ahead characters (WILL).");
            if (!tnstate[Command[2]].sent_do)
                SendTelnetOption(SockB, TNDO, Command[2]);
            tnstate[Command[2]].is_do = 1;
            break;

            /* Reject everything else */
        default:
            snprintf(LogStr, sizeof(LogStr), "Rejecting option WILL: %u",
                     (unsigned int) Command[2]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            SendTelnetOption(SockB, TNDONT, Command[2]);
            tnstate[Command[2]].is_do = 0;
            break;
        }
        tnstate[Command[2]].sent_do = 0;
        tnstate[Command[2]].sent_dont = 0;
        break;

        /* Confirmations for options */
    case TNDO:
        switch (Command[2]) {
            /* COM Port Control Option */
        case TNCOM_PORT_OPTION:
            LogMsg(LOG_INFO, "Telnet COM Port Control Enabled (DO).");
            PortControlEnable = True;
            if (!tnstate[Command[2]].sent_will)
                SendTelnetOption(SockB, TNWILL, Command[2]);
            tnstate[Command[2]].is_will = 1;
            break;

            /* Telnet Binary mode */
        case TN_TRANSMIT_BINARY:
            LogMsg(LOG_INFO, "Telnet Binary Transfer Enabled (DO).");
            if (!tnstate[Command[2]].sent_will)
                SendTelnetOption(SockB, TNWILL, Command[2]);
            tnstate[Command[2]].is_will = 1;
            break;

            /* Echo request handled.  The modem will echo for the user. */
        case TN_ECHO:
            LogMsg(LOG_INFO, "Rejecting Telnet Echo Option (DO).");
            if (!tnstate[Command[2]].sent_will)
                SendTelnetOption(SockB, TNWILL, Command[2]);
            tnstate[Command[2]].is_will = 1;
            break;

            /* No go ahead needed */
        case TN_SUPPRESS_GO_AHEAD:
            LogMsg(LOG_INFO, "Suppressing Go Ahead characters (DO).");
            if (!tnstate[Command[2]].sent_will)
                SendTelnetOption(SockB, TNWILL, Command[2]);
            tnstate[Command[2]].is_will = 1;
            break;

            /* Reject everything else */
        default:
            snprintf(LogStr, sizeof(LogStr), "Rejecting option DO: %u", (unsigned int) Command[2]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
            SendTelnetOption(SockB, TNWONT, Command[2]);
            tnstate[Command[2]].is_will = 0;
            break;
        }
        tnstate[Command[2]].sent_will = 0;
        tnstate[Command[2]].sent_wont = 0;
        break;

        /* Notifications of rejections for options */
    case TNDONT:
        snprintf(LogStr, sizeof(LogStr), "Received rejection for option: %u",
                 (unsigned int) Command[2]);
        LogStr[sizeof(LogStr) - 1] = '\0';
        LogMsg(LOG_DEBUG, LogStr);
        if (tnstate[Command[2]].is_will) {
            SendTelnetOption(SockB, TNWONT, Command[2]);
            tnstate[Command[2]].is_will = 0;
        }
        tnstate[Command[2]].sent_will = 0;
        tnstate[Command[2]].sent_wont = 0;
        break;

    case TNWONT:
        if (Command[2] == TNCOM_PORT_OPTION) {
            LogMsg(LOG_ERR, "Client doesn't support Telnet COM Port "
                   "Protocol Option (RFC 2217), trying to serve anyway.");
        }
        else {
            snprintf(LogStr, sizeof(LogStr),
                     "Received rejection for option: %u", (unsigned int) Command[2]);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_DEBUG, LogStr);
        }
        if (tnstate[Command[2]].is_do) {
            SendTelnetOption(SockB, TNDONT, Command[2]);
            tnstate[Command[2]].is_do = 0;
        }
        tnstate[Command[2]].sent_do = 0;
        tnstate[Command[2]].sent_dont = 0;
        break;
    }
}

/* Check and act upon read/write result. Uses errno. Returns true on error. */
Boolean
IOResultError(int iobytes, const char *err, const char *eof_err)
{
    switch (iobytes) {
    case -1:
        if (errno != EWOULDBLOCK) {
            LogMsg(LOG_NOTICE, err);
            return True;
        }
        break;
    case 0:
        LogMsg(LOG_NOTICE, eof_err);
        return True;
        break;
    }
    return False;
}

void
LogPortSettings(unsigned long speed, unsigned char datasize, unsigned char parity,
                unsigned char stopsize, unsigned char outflow, unsigned char inflow)
{
    char LogStr[TmpStrLen];
    char parchar;
    char *stopbits = "";
    char *outflowtype = "";
    char *inflowtype = "";

    switch (parity) {
    case TNCOM_ODDPARITY:
        parchar = 'O';
        break;
    case TNCOM_EVENPARITY:
        parchar = 'E';
        break;
    case TNCOM_MARKPARITY:
        parchar = 'M';
        break;
    case TNCOM_SPACEPARITY:
        parchar = 'S';
        break;
    case TNCOM_NOPARITY:
        parchar = 'N';
        break;
    default:
        parchar = 'I';
        break;
    }

    switch (stopsize) {
    case TNCOM_ONESTOPBIT:
        stopbits = "1";
        break;
    case TNCOM_ONE5STOPBITS:
        stopbits = "1.5";
        break;
    case TNCOM_TWOSTOPBITS:
        stopbits = "2";
        break;
    }

    switch (outflow) {
    case TNCOM_CMD_FLOW_NONE:
        inflowtype = "none";
        break;
    case TNCOM_CMD_FLOW_XONXOFF:
        inflowtype = "XON/XOFF";
        break;
    case TNCOM_CMD_FLOW_HARDWARE:
        inflowtype = "RTS/CTS";
        break;
    case TNCOM_CMD_FLOW_DCD:
        inflowtype = "DCD flow control";
        break;
    case TNCOM_CMD_FLOW_DSR:
        inflowtype = "DSR";
        break;
    default:
        inflowtype = "unknown";
        break;
    }

    switch (inflow) {
    case TNCOM_CMD_INFLOW_NONE:
        outflowtype = "none";
        break;
    case TNCOM_CMD_INFLOW_XONXOFF:
        outflowtype = "XON/XOFF";
        break;
    case TNCOM_CMD_INFLOW_HARDWARE:
        outflowtype = "RTS/CTS";
        break;
    case TNCOM_CMD_INFLOW_DTR:
        outflowtype = "DTR";
        break;
    default:
        outflowtype = "unknown";
        break;
    }

    snprintf(LogStr, sizeof(LogStr),
             "Port settings:%lu-%u-%c-%s outflow:%s inflow:%s",
             speed, datasize, parchar, stopbits, outflowtype, inflowtype);
    LogStr[sizeof(LogStr) - 1] = '\0';
    LogMsg(LOG_NOTICE, LogStr);
}

void
Usage(void)
{
    /* Write little usage information */
    fprintf(stderr,
            "sercd %s: RFC 2217 compliant serial port redirector\n"
            "This program can be run by the inetd superserver or standalone\n"
            "\n"
            "Usage:\n"
#ifndef ANDROID
            "sercd [-ie] [-p port] [-l addr] <loglevel> <device> <lockfile> [pollingterval]\n"
#else
        "sercd [-ie] [-p port] [-l addr] <loglevel> <device> [pollingterval]\n"
#endif
            "-i       indicates Cisco IOS Bug compatibility\n"
            "-e       send output to standard error instead of syslog\n"
            "-p port  listen on specified port, instead of port 7000\n"
            "-l addr  standalone mode, bind to specified adress, empty string for all\n"
            "Poll interval is in milliseconds, default is %d,\n"
            "0 means no polling\n", VERSION, DEFAULT_POLL_INTERVAL);
}

#ifdef ANDROID
SERCD_SOCKET *LSocketFd = NULL;
#endif

#ifndef ANDROID
/* Main function */
int
main(int argc, char **argv)
#else
JNIEXPORT jint JNICALL Java_gnu_sercd_SercdService_main
  (JNIEnv *env, jobject thiz, jstring serialport, jstring netinterface, jint port)
#endif
{
    /* Chars read */
    char readbuf[512];

    /* Temporary string for logging */
    char LogStr[TmpStrLen];

    /* Poll interval and timer */
    long PollInterval;

    /* Buffer to Device from Network */
    BufferType ToDevBuf;

    /* Buffer to Network from Device */
    BufferType ToNetBuf;

    int opt = 0;
    char *optstring = "iep:l:";
    unsigned int opt_port = 7000;
    Boolean inetd_mode = True;
    struct in_addr opt_bind_addr;
    SERCD_SOCKET insocket, outsocket, lsocket;
    PORTHANDLE devicefd;

    opt_bind_addr.s_addr = INADDR_ANY;

#ifndef ANDROID
    while (opt != -1) {
        opt = getopt(argc, argv, optstring);
        switch (opt) {
            /* Cisco IOS compatibility */
        case 'i':
            CiscoIOSCompatible = True;
            break;
        case 'e':
            StdErrLogging = True;
            break;
        case 'p':
            opt_port = strtol(optarg, NULL, 10);
            if (opt_port == 0) {
                fprintf(stderr, "Invalid port\n");
                exit(Error);
            }
            break;
        case 'l':
            if (*optarg) {
                opt_bind_addr.s_addr = inet_addr(optarg);
                if (opt_bind_addr.s_addr == (unsigned) -1) {
                    fprintf(stderr, "Invalid bind address\n");
                    exit(Error);
                }
            }
            inetd_mode = False;
            break;
        }
    }

    /* Check the command line argument count */
#ifndef ANDROID
    if (argc - optind < 3 || argc - optind > 4) {
#else
    if (argc - optind < 2 || argc - optind > 3) {
#endif
        Usage();
        exit(Error);
    }
#else /* ANDROID */
    opt_port = port;
	jboolean isCopy;
    opt_bind_addr.s_addr = inet_addr((*env)->GetStringUTFChars(env, netinterface, &isCopy));
    inetd_mode = False;
#endif /* ANDROID */

    /* Sets the log level */
#ifndef ANDROID
	MaxLogLevel = atoi(argv[optind++]);
#else
	MaxLogLevel = 7;
#endif

    /* Gets device and lock file names */
#ifndef ANDROID
	DeviceName = argv[optind++];
#else
	DeviceName = (char*)(*env)->GetStringUTFChars(env, serialport, &isCopy);
#endif
#ifndef ANDROID
    LockFileName = argv[optind++];
#endif

    /* Retrieve the polling interval */
#ifndef ANDROID
    if (optind < argc) {
        char *endptr;
        PollInterval = strtol(argv[optind++], &endptr, 0);
        if (!endptr || *endptr || PollInterval < 0) {
            fprintf(stderr, "Invalid polling interval\n");
            exit(Error);
        }
    }
    else {
        PollInterval = DEFAULT_POLL_INTERVAL;
    }
#else
    PollInterval = DEFAULT_POLL_INTERVAL;
#endif

    PlatformInit();

    /* Logs sercd start */
    LogMsg(LOG_NOTICE, "sercd started.");

    /* Logs sercd log level */
    snprintf(LogStr, sizeof(LogStr), "Log level: %i", MaxLogLevel);
    LogStr[sizeof(LogStr) - 1] = '\0';
    LogMsg(LOG_INFO, LogStr);

    /* Logs the polling interval */
    snprintf(LogStr, sizeof(LogStr), "Polling interval (ms): %ld", PollInterval);
    LogStr[sizeof(LogStr) - 1] = '\0';
    LogMsg(LOG_INFO, LogStr);

    if (inetd_mode) {
        /* inetd mode */
        insocket = STDIN_FILENO;
        outsocket = STDOUT_FILENO;
        InSocketFd = &insocket;
        OutSocketFd = &outsocket;
        SetSocketOptions(*InSocketFd, *OutSocketFd);
        InitBuffer(&ToNetBuf);
        InitTelnetStateMachine();
        SendTelnetInitialOptions(&ToNetBuf);
    }
    else {
        /* Standalone mode */
        struct sockaddr_in sin;
        lsocket = socket(PF_INET, SOCK_STREAM, 0);
        if (lsocket < 0) {
            perror("socket");
            exit(Error);
        }
#ifndef WIN32
        int SockParmEnable = 1;
        /* Windows is totally broken wrt SO_REUSEADDR - it uses
           non-standard and mostly useless semantics. This is
           confirmed by Microsoft: From
           http://msdn.microsoft.com/en-us/library/ms740621(VS.85).aspx:
           "the behavior for all sockets bound to that port is
           indeterminate". "The exception to this non-deterministic
           behavior is multicast sockets. " Instead, they
           are recommending SO_EXCLUSIVEADDRUSE, but it typically only
           works if you have administrator privs. Bah. */
        setsockopt(lsocket, SOL_SOCKET, SO_REUSEADDR, (char *) &SockParmEnable,
                   sizeof(SockParmEnable));
#endif

        sin.sin_family = AF_INET;
        sin.sin_port = htons(opt_port);
        sin.sin_addr.s_addr = opt_bind_addr.s_addr;
        if (bind(lsocket, (struct sockaddr *) &sin, sizeof(struct sockaddr))) {
            perror("bind");
            fprintf(stderr, "Couldn't bind to tcp port %d\n", opt_port);
            exit(Error);
        }
        if (listen(lsocket, 1) < 0) {
            perror("listen");
            exit(Error);
        }
        LSocketFd = &lsocket;
        NewListener(*LSocketFd);
    }

    /* Main loop with fd's control. General note: We basically have
       three states:

       1) No client connection, no open port
       2) Client connected, port not yet open
       3) Client connected, port open

       This means that if DeviceFd is set, InSocketFd and OutSocketFd
       should be set as well. */
    ChangeState(env, thiz, STATE_READY);

    while (True) {
        int selret;

        PORTHANDLE *DeviceIn = NULL;
        PORTHANDLE *DeviceOut = NULL;
        PORTHANDLE *Modemstate = NULL;
        SERCD_SOCKET *SocketOut = NULL;
        SERCD_SOCKET *SocketIn = NULL;

        if (DeviceFd && BufferHasRoomFor(&ToNetBuf, EscWriteChar_bytes) && InputFlow) {
            DeviceIn = DeviceFd;
        }
        if (DeviceFd && !IsBufferEmpty(&ToDevBuf)) {
            DeviceOut = DeviceFd;
        }
        if (DeviceFd && PortControlEnable && InputFlow &&
            BufferHasRoomFor(&ToNetBuf, SendCPCByteCommand_bytes)) {
            Modemstate = DeviceFd;
        }
        if (OutSocketFd && !IsBufferEmpty(&ToNetBuf)) {
            SocketOut = OutSocketFd;
        }
        if (DeviceFd && BufferHasRoomFor(&ToDevBuf, EscRedirectChar_bytes_DevB) &&
            InSocketFd && BufferHasRoomFor(&ToNetBuf, EscRedirectChar_bytes_SockB)) {
            SocketIn = InSocketFd;
        }

        if (!DeviceIn && !DeviceOut && !SocketOut && !SocketIn && !LSocketFd) {
            /* Nothing more to do */
            exit(NoError);
        }

        selret = SercdSelect(DeviceIn, DeviceOut, Modemstate, SocketOut, SocketIn,
                             LSocketFd, PollInterval);
        if (selret < 0) {
            snprintf(LogStr, sizeof(LogStr), "select error: %d", errno);
            LogStr[sizeof(LogStr) - 1] = '\0';
            LogMsg(LOG_ERR, LogStr);
            exit(Error);
        }
        else if (selret > 0) {
            /* Handle buffers in the following order:
               Serial input
               Serial output
               Network output
               Network input

               Motivation: Needs to read away data from the serial
               port to prevent buffer overruns. Needs to drain our
               buffers as fast as possible, to reduce latency and make
               room for more. This order should be used in function
               signatures etc as well.
             */
            ssize_t iobytes;
            unsigned int i, trybytes;
            unsigned char *p;

            if (selret & SERCD_EV_DEVICEIN) {
                /* Read from serial port. Each serial port byte might
                   produce EscWriteChar_bytes of network data. */
                trybytes = MIN(sizeof(readbuf), BufferRoomLeft(&ToNetBuf) / EscWriteChar_bytes);
                iobytes = ReadFromDev(*DeviceFd, &readbuf, trybytes);
                if (IOResultError(iobytes, "Error reading from device", "EOF from device")) {
#ifndef ANDROID
                    DropConnection(DeviceFd, InSocketFd, OutSocketFd, LockFileName);
#else
                    ChangeState(env, thiz, STATE_READY);
                    DropConnection(DeviceFd, InSocketFd, OutSocketFd);
#endif
                    InSocketFd = OutSocketFd = NULL;
                    DeviceFd = NULL;
                    continue;
                }
                else {
                    for (i = 0; i < iobytes; i++) {
                        EscWriteChar(&ToNetBuf, readbuf[i]);
                    }
                }
            }

            if (selret & SERCD_EV_DEVICEOUT) {
                /* Write to serial port */
                p = GetBufferString(&ToDevBuf, &trybytes);
                iobytes = WriteToDev(*DeviceFd, p, trybytes);
                if (IOResultError(iobytes, "Error writing to device.", "EOF to device")) {
#ifndef ANDROID
                        DropConnection(DeviceFd, InSocketFd, OutSocketFd, LockFileName);
#else
                        ChangeState(env, thiz, STATE_READY);
                        DropConnection(DeviceFd, InSocketFd, OutSocketFd);
#endif
                    InSocketFd = OutSocketFd = NULL;
                    DeviceFd = NULL;
                    continue;
                }
                else {
                    BufferPopBytes(&ToDevBuf, iobytes);
                }
            }

            if (selret & SERCD_EV_SOCKETOUT) {
                /* Write to network */
                p = GetBufferString(&ToNetBuf, &trybytes);
                iobytes = WriteToNet(*OutSocketFd, p, trybytes);
                if (IOResultError(iobytes, "Error writing to network", "EOF to network")) {
#ifndef ANDROID
                        DropConnection(DeviceFd, InSocketFd, OutSocketFd, LockFileName);
#else
                        ChangeState(env, thiz, STATE_READY);
                        DropConnection(DeviceFd, InSocketFd, OutSocketFd);
#endif
                        InSocketFd = OutSocketFd = NULL;
                    DeviceFd = NULL;
                    continue;
                }
                else {
                    BufferPopBytes(&ToNetBuf, iobytes);
                }
            }

            if (selret & SERCD_EV_SOCKETIN) {
                /* Read from network. Each network byte might produce
                   EscRedirectChar_bytes_DevB or or up to
                   EscRedirectChar_bytes_SockB network data. */
                trybytes = sizeof(readbuf);
                trybytes = MIN(trybytes, BufferRoomLeft(&ToNetBuf) / EscRedirectChar_bytes_SockB);
                trybytes = MIN(trybytes, BufferRoomLeft(&ToDevBuf) / EscRedirectChar_bytes_DevB);
                iobytes = ReadFromNet(*InSocketFd, readbuf, trybytes);
                if (IOResultError(iobytes, "Error readbuf from network.", "EOF from network")) {
#ifndef ANDROID
                        DropConnection(DeviceFd, InSocketFd, OutSocketFd, LockFileName);
#else
                        ChangeState(env, thiz, STATE_READY);
                        DropConnection(DeviceFd, InSocketFd, OutSocketFd);
#endif
                        InSocketFd = OutSocketFd = NULL;
                    DeviceFd = NULL;
                    continue;
                }
                else {
                    for (i = 0; i < iobytes; i++) {
                        EscRedirectChar(&ToNetBuf, &ToDevBuf, *DeviceFd, readbuf[i]);
                    }
                }
            }

            /* accept new connections */
            if (selret & SERCD_EV_SOCKETCONNECT) {
                struct sockaddr addr;
                socklen_t addrlen = sizeof(addr);
                int csock;

                /* FIXME: Might be a good idea to log the client addr */
                LogMsg(LOG_NOTICE, "New connection");
                csock = accept(*LSocketFd, &addr, &addrlen);
                if (csock < 0) {
                    /* FIXME: Log what kind of error. */
                    LogMsg(LOG_ERR, "Error accepting socket");
                }
                else if (InSocketFd && OutSocketFd) {
                    /* We can only handle one connection at a time. */
                    LogMsg(LOG_ERR, "Another client connected, dropping new connection");
                    closesocket(csock);
                }
                else {
                    ChangeState(env, thiz, STATE_CONNECTED);

                    /* Set up networking */
                    insocket = csock;
                    OutSocketFd = InSocketFd = &insocket;
                    SetSocketOptions(*InSocketFd, *OutSocketFd);
                    InitBuffer(&ToNetBuf);
                    InitTelnetStateMachine();
                    SendTelnetInitialOptions(&ToNetBuf);
                }
            }

            /* Open serial port if not yet open */
            if (InSocketFd && OutSocketFd && !DeviceFd) {
                DeviceFd = &devicefd;
#ifndef ANDROID
                if (OpenPort(DeviceName, LockFileName, DeviceFd) == Error) {
#else
                if (OpenPort(DeviceName, DeviceFd) == Error) {
#endif
                    /* Open failed */
                    snprintf(LogStr, sizeof(LogStr), "Unable to open device %s. Exiting.",
                             DeviceName);
                    LogStr[sizeof(LogStr) - 1] = '\0';
                    LogMsg(LOG_ERR, LogStr);
                    /* Emulate the inetd behaviour: Close the connection. */
#ifndef ANDROID
                    DropConnection(NULL, InSocketFd, OutSocketFd, LockFileName);
#else
                    ChangeState(env, thiz, STATE_READY);
                    DropConnection(NULL, InSocketFd, OutSocketFd);
#endif
                    InSocketFd = OutSocketFd = NULL;
                    DeviceFd = NULL;
                    continue;
                }
                else {
                    /* Successfully opened port */
                	ChangeState(env, thiz, STATE_PORT_OPENED);
                    InitBuffer(&ToDevBuf);
                }
            }

            /* Check the port state and notify the client if it's changed */
            if (selret & SERCD_EV_MODEMSTATE) {
                unsigned char newstate;
                ModemStateNotified();
                newstate = GetModemState(*DeviceFd, ModemState);
                /* Don't send update if only delta changes */
                if ((newstate & ModemStateMask & TNCOM_MODMASK_NODELTA)
                    != (ModemState & ModemStateMask & TNCOM_MODMASK_NODELTA)) {
                    ModemState = newstate;
                    SendCPCByteCommand(&ToNetBuf, TNASC_NOTIFY_MODEMSTATE,
                                       (ModemState & ModemStateMask));
                    snprintf(LogStr, sizeof(LogStr), "Sent modem state: %u",
                             (unsigned int) (ModemState & ModemStateMask));
                    LogStr[sizeof(LogStr) - 1] = '\0';
                    LogMsg(LOG_DEBUG, LogStr);
                }
            }
        }
    }
}

#ifdef ANDROID
JNIEXPORT void JNICALL Java_gnu_sercd_SercdService_exit(JNIEnv *env, jobject thiz)
{
	/* Close the files descriptors to force the next call to "select" to fail. */
	DropConnection(DeviceFd, InSocketFd, OutSocketFd);
	close(*LSocketFd);
}
#endif
