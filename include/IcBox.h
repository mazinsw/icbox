/*
    IcBox - CallerID Detection
    Copyright (C) 2010-2014 MZSW Creative Software

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    MZSW Creative Software
    contato@mzsw.com.br
*/

/** @file IcBox.h
 *  Main include header for the IcBox library
 */

#ifndef _ICBOX_H_
#define _ICBOX_H_

#ifdef __cplusplus
extern "C" {
#endif

#define ICBOX_EVENT_CONNECTED 1
#define ICBOX_EVENT_DISCONNECTED 2
#define ICBOX_EVENT_RING 3
#define ICBOX_EVENT_CALLERID 4
#define ICBOX_EVENT_ONHOOK 5
#define ICBOX_EVENT_OFFHOOK 6
#define ICBOX_EVENT_HANGUP 7

typedef struct IcBoxEvent
{
	int type;
	char data[28];
} IcBoxEvent;

typedef struct IcBox IcBox;

#ifdef BUILD_DLL
# define LIBEXPORT __declspec(dllexport)
#else
#  ifdef LIB_STATIC
#    define LIBEXPORT
#  else
#    define LIBEXPORT extern
#  endif
#endif
#define LIBCALL __stdcall

#include "private/Platform.h"

/**
 * Start the connection using some configurations
 * 
 * parameters
 *   config: configuração da porta
 * 
 * return
 *   a pointer for a IcBox instance
 */
LIBEXPORT IcBox * LIBCALL IcBox_create(const char* config);

/**
 * Check if it is connected to a IcBox device
 * 
 * parameters
 *   lib: pointer for a IcBox instance
 * 
 * return
 *   1 for connected, 0 otherwise
 */
LIBEXPORT int LIBCALL IcBox_isConected(IcBox * lib);

/**
 * Change connection configuration for IcBox device
 * 
 * parameters
 *   lib: pointer for a IcBox instance
 *   config: new configuration, including connection information
 */
LIBEXPORT void LIBCALL IcBox_setConfiguration(IcBox * lib, const char * config);

/**
 * Obtém a configuração atual da conexão com a balabça
 * 
 * parameters
 *   lib: pointer for a IcBox instance
 * 
 * return
 *   a list of connection parameters splited with ;
 */
LIBEXPORT const char* LIBCALL IcBox_getConfiguration(IcBox * lib);

/**
 * Wait for a event
 * 
 * parameters
 *   lib: pointer for a IcBox instance
 * 
 * return
 *   0 when instance is freed or canceled,
 *   otherwise a new event has picked from queue,
 */
LIBEXPORT int LIBCALL IcBox_pollEvent(IcBox * lib, IcBoxEvent* event);

/**
 * Cancel a poll event waiting call
 * 
 * parameters
 *   lib: pointer for a IcBox instance
 */
LIBEXPORT void LIBCALL IcBox_cancel(IcBox * lib);

/**
 * Disconnect and free a instance
 * 
 * parameters
 *   lib: pointer for a IcBox instance
 */
LIBEXPORT void LIBCALL IcBox_free(IcBox * lib);


/**
 * Get the library version
 * 
 * parameters
 *   lib: pointer for a IcBox instance
 */
LIBEXPORT const char* LIBCALL IcBox_getVersion();

#ifdef __cplusplus
}
#endif

#endif /* _ICBOX_H_ */