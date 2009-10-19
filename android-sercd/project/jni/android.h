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
*/

#ifndef _ANDROID_H_
#define _ANDROID_H_
#ifdef ANDROID

#include <jni.h>

#define exit(code) { \
	char msg[64]; \
	sprintf(msg, "Exiting at %d", __LINE__); \
	LogMsg(LOG_ERR, msg); \
	return code; \
}

typedef enum {
	STATE_READY, STATE_CONNECTED, STATE_PORT_OPENED, STATE_STOPPED, STATE_CRASHED
} ProxyState;

void ChangeState(JNIEnv *env, jobject thiz, ProxyState newstate);

#endif /* ANDROID */
#endif /* _ANDROID_H_ */
