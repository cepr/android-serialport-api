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

#ifdef ANDROID

#include "android.h"
#include <jni.h>

static jobject ConvertToJava(JNIEnv *env, ProxyState newstate)
{

	jclass class = (*env)->FindClass(env, "gnu.sercd.SercdService$ProxyState");

	const char *name;
	switch(newstate)
	{
	case STATE_READY:		name = "STATE_READY";		break;
	case STATE_CONNECTED:	name = "STATE_CONNECTED";	break;
	case STATE_PORT_OPENED:	name = "STATE_PORT_OPENED";	break;
	case STATE_STOPPED:		name = "STATE_STOPPED";		break;
	case STATE_CRASHED:		name = "STATE_CRASHED";		break;
	}

	jfieldID fieldid = (*env)->GetStaticFieldID(
			env,
			class,
			name,
			"Lgnu/sercd/SercdService$ProxyState;");

	return (*env)->GetStaticObjectField(
			env,
			class,
			fieldid);
}

void ChangeState(JNIEnv *env, jobject thiz, ProxyState newstate)
{

	jclass class = (*env)->GetObjectClass(
			env,
			thiz);

	jmethodID methodid = (*env)->GetMethodID(
			env,
			class,
			"ChangeState",
			"(Lgnu/sercd/SercdService$ProxyState;)V");

	jvalue value;
	value.l = ConvertToJava(
			env,
			newstate);

	(*env)->CallVoidMethodA(
			env,
			thiz,
			methodid,
			&value);
}

#endif /* ANDROID */
