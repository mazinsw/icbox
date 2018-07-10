#include <IcBox.h>
#include <stdio.h>
#include "Thread.h"
#include "Mutex.h"
#include "Event.h"
#include "CommPort.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "StringBuilder.h"
#include "Queue.h"
#include "Mutex.h"
#define PROPERY_BUFFER_SIZE 256
#define BUFFER_SIZE 256

static const char gVersion[] = "1.0.0.0";

struct IcBox
{
	int canceled;
	Thread* thConnect;
	Thread* thReceive;
	Event* evCancel;
	Event* evReceive;
	CommPort * comm;
	int connectTimeout;
	int retryTimeout;
	int requestAliveInterval;
	char port[8];
	CommSettings commSettings;
	StringBuilder* config;
	Mutex* mutex;
	Mutex* writeMutex;
	char buffer[BUFFER_SIZE];
	int bufferPos;
	Queue* queue;
	int testing;
};

static int _IcBox_echoTest(IcBox * lib, CommPort* comm);

static void _IcBox_pushEvent(IcBox * lib, int eventType, const char * data, int bytes)
{
	IcBoxEvent* event = (IcBoxEvent*)malloc(sizeof(IcBoxEvent));
	memset(event, 0, sizeof(IcBoxEvent));
	event->type = eventType;
	memcpy(event->data, data, bytes);
	
	Mutex_lock(lib->writeMutex);
	Queue_push(lib->queue, event);
#ifdef DEBUGLIB
	printf("pushEvent %d\n", eventType);
#endif
	Event_post(lib->evReceive);
	Mutex_unlock(lib->writeMutex);
}

static void _IcBox_reconnect(IcBox * lib)
{
#ifdef DEBUGLIB
	printf("disconnecting\n");
#endif
	CommPort_free(lib->comm);
#ifdef DEBUGLIB
	printf("comm port freed\n");
#endif
	lib->comm = NULL;
	_IcBox_pushEvent(lib, ICBOX_EVENT_DISCONNECTED, NULL, 0);
#ifdef DEBUGLIB
	printf("sent disconnect event\n");
#endif
	lib->canceled = 1;
	Thread_start(lib->thConnect); // try connect again
}

static int _IcBox_proccessResponse(IcBox * lib, const char * response)
{
	if(strcmp("F", response) == 0 || strcmp("I", response) == 0 || strcmp("A", response) == 0 || strcmp("D", response) == 0)
    {
		return 1;
    }
	else if(strcmp("RING", response) == 0)
	{
		if(!lib->testing)
			_IcBox_pushEvent(lib, ICBOX_EVENT_RING, NULL, 0);
		return 1;
	}
	else if(strncmp("          ", response, 4) == 0)
	{
		int offset = 10;
		if(!lib->testing)
			_IcBox_pushEvent(lib, ICBOX_EVENT_CALLERID, response + offset, 12);
		return 1;
	}
	else if(strcmp("NO CARRIER", response) == 0)
	{
		if(!lib->testing)
			_IcBox_pushEvent(lib, ICBOX_EVENT_HANGUP, NULL, 0);
		return 1;
	}
	return 0;
}

static int _IcBox_dataReceived(IcBox * lib, const unsigned char * buffer, 
	int size)
{
	int count = 0, i, prev_pos, bytesToRead;
	
	while(size > 0)
	{
		bytesToRead = size < (BUFFER_SIZE - lib->bufferPos)? size: (BUFFER_SIZE - lib->bufferPos);
		if(!bytesToRead) 
		{
			// insuficient buffer, clear it!
#ifdef DEBUGLIB
			printf("insuficient buffer, need %d, have %d, cleaning!\n", size, (BUFFER_SIZE - lib->bufferPos));
#endif
			lib->bufferPos = 0;
			lib->buffer[lib->bufferPos] = '\0';
			break;
		}
		memcpy(lib->buffer + lib->bufferPos, buffer, bytesToRead);
		size -= bytesToRead;
		prev_pos = lib->bufferPos;
		lib->bufferPos += bytesToRead;
		lib->buffer[lib->bufferPos] = '\0';
		// find carriage return
		i = prev_pos;
		while(i < lib->bufferPos)
		{
			if(lib->buffer[i] == 13 || lib->buffer[i] == 10) {
				lib->buffer[i] = '\0';
				if(lib->buffer[0] != '\0')
				{
					if(_IcBox_proccessResponse(lib, lib->buffer))
						count++;
				}
				if(lib->buffer[i] == 10)
					i++; // skip LF
				i++; // skip CR
				// copy remaining command and find again
				memcpy(lib->buffer, lib->buffer + i, lib->bufferPos - i);
				lib->bufferPos = lib->bufferPos - i;
				lib->buffer[lib->bufferPos] = '\0';
				i = 0;
				continue;
			}
			i++;
		}
	}
	return count;
}

static void _IcBox_receiveFunc(void* data)
{
	IcBox * lib = (IcBox*)data;
	int bytesAvailable;
	Mutex_lock(lib->mutex);
	while(lib->canceled == 0)
	{
#ifdef DEBUGLIB
	printf("waiting for receive event\n");
#endif
		if(CommPort_waitEx(lib->comm, &bytesAvailable, lib->requestAliveInterval))
		{
#ifdef DEBUGLIB
			printf("%d bytes available\n", bytesAvailable);
#endif
			if(bytesAvailable == 0)
				continue;
			unsigned char * buffer = (unsigned char *)malloc(
				sizeof(unsigned char) * (bytesAvailable + 1));
			bytesAvailable = CommPort_read(lib->comm, buffer, bytesAvailable);
			buffer[bytesAvailable] = 0;
			_IcBox_dataReceived(lib, buffer, bytesAvailable);
			free(buffer);
			continue;
		}
		if(lib->canceled == 1)
			continue;
		else
		{
			if(!_IcBox_echoTest(lib, lib->comm))
			{
				if(lib->canceled == 1)
					continue;
				_IcBox_reconnect(lib);// try connect again
			} 
			else 
			{
#ifdef DEBUGLIB
				printf("device alive\n");
#endif
			}
		}
	}
	Mutex_unlock(lib->mutex);
#ifdef DEBUGLIB
	printf("leave _IcBox_receiveFunc\n");
#endif
}

static int _IcBox_sendCmd(IcBox * lib, CommPort* comm, const char * buffer)
{
	int size, bytesAvailable, bwritten;

	size = strlen(buffer);
#ifdef DEBUGLIB
	printf("sending active check command...\n");
#endif
	Mutex_lock(lib->writeMutex);
	bwritten = CommPort_writeEx(comm, (unsigned char*)buffer, size, lib->connectTimeout / 2);
	Mutex_unlock(lib->writeMutex);
	if(bwritten != size)
	{
#ifdef DEBUGLIB
		printf("Error sending command\n");
#endif
		return 0;
	}
	if(!CommPort_waitEx(comm, &bytesAvailable, lib->connectTimeout * 10))
	{
#ifdef DEBUGLIB
		printf("Waiting response timeout\n");
#endif
		return 0;
	}
	if(bytesAvailable <= 0)
	{
#ifdef DEBUGLIB
		printf("No response data\n");
#endif
		return 0;
	}
	unsigned char * buff = (unsigned char *)malloc(
			sizeof(unsigned char) * (bytesAvailable + 1));
	bytesAvailable = CommPort_readEx(comm, buff, bytesAvailable, lib->connectTimeout / 2);
	if(bytesAvailable <= 0)
	{
		free(buff);
#ifdef DEBUGLIB
		printf("Read response data failed\n");
#endif
		return 0;
	}
	buff[bytesAvailable] = 0;
	lib->testing = 1;
	if(_IcBox_dataReceived(lib, buff, bytesAvailable))
	{
		free(buff);
		lib->testing = 0;
		return 1;
	}
	lib->testing = 0;
	free(buff);
#ifdef DEBUGLIB
	printf("No compatible device or invalid command\n");
#endif
	return 0;
}

static int _IcBox_echoTest(IcBox * lib, CommPort* comm)
{
#ifdef DEBUGLIB
	printf("Echo test started\n");
#endif
	return _IcBox_sendCmd(lib, comm, "@CX\r\n");
}

static void _IcBox_connectFunc(void* data)
{
	IcBox * lib = (IcBox*)data;
	CommPort* comm;
	int i, need, count, len;
	char * ports, * port;
#ifdef DEBUGLIB
	printf("enter _IcBox_connectFunc\n");
#endif
	Mutex_lock(lib->mutex);
	lib->canceled = 0;
	Mutex_unlock(lib->mutex);
	count = 0;
	need = CommPort_enum(NULL, 0);
	if(lib->port[0] != 0)
		len = strlen(lib->port) + 1;
	else
		len = 0;
	ports = (char*)malloc(need + len + 1);
	if(len > 0)
	{
		memcpy(ports, lib->port, len);
		ports[len] = 0;
		count++;
	}
	count += CommPort_enum(ports + len, need);
#ifdef DEBUGLIB
	printf("searching port... %d found\n", count);
#endif
	while(lib->canceled == 0)
	{
		// try connect to one port
		comm = NULL;
		port = ports;
		for(i = 1; i <= count; i++)
		{
#ifdef DEBUGLIB
			printf("trying connect to %s\n", port);
#endif
			comm = CommPort_create(port);
			if(comm != NULL)
			{
				if(_IcBox_echoTest(lib, comm))
					break;
				CommPort_free(comm);
				comm = NULL;
			}
			if(lib->canceled == 1)
				break;
			port += strlen(port) + 1;
		}
		if(comm != NULL)
		{
#ifdef DEBUGLIB
			printf("%s connected\n", port);
#endif
			// connection successful, start receive event
			lib->comm = comm;
			strcpy(lib->port, port);
			_IcBox_pushEvent(lib, ICBOX_EVENT_CONNECTED, NULL, 0);
#ifdef DEBUGLIB
			// printf("Enabling Caller ID\n");
#endif
			// int enabled = 0;
			// enabled = enabled || _IcBox_sendCmd(lib, comm, "AT+VCID=1\r\n");
			// enabled = enabled || _IcBox_sendCmd(lib, comm, "AT#CID=1\r\n");
			// enabled = enabled || _IcBox_sendCmd(lib, comm, "AT#CC1\r\n");
			// enabled = enabled || _IcBox_sendCmd(lib, comm, "AT%CCID=1\r\n");
			// enabled = enabled || _IcBox_sendCmd(lib, comm, "AT*ID1\r\n");
			Thread_start(lib->thReceive);
			break;
		}
#ifdef DEBUGLIB
		printf("no port available, trying again\n");
#endif
		if(lib->canceled == 1)
			break;
		// not port available, wait few seconds and try again
		Event_waitEx(lib->evCancel, lib->retryTimeout);
	}
	free(ports);
#ifdef DEBUGLIB
	printf("leave _IcBox_connectFunc\n");
#endif
}

static int _IcBox_getProperty(const char * lwconfig, const char * config, 
	const char * key, 
	char * buffer, int size)
{
	char * pos = strstr(lwconfig, key);
	if(pos == NULL)
		return 0;
	pos = strstr(pos, ":");
	if(pos == NULL)
		return 0;
	pos++;
	char * end = strstr(pos, ";");
	int count = strlen(pos);
	if(end != NULL)
		count = end - pos;
	if(size < count)
		return count - size;
	strncpy(buffer, config + (pos - lwconfig), count);
	buffer[count] = 0;
#ifdef DEBUGLIB
	printf("property %s: value %s\n", key, buffer);
#endif
	return 1;
}

LIBEXPORT IcBox * LIBCALL IcBox_create(const char* config)
{
	IcBox * lib = (IcBox*)malloc(sizeof(IcBox));
	memset(lib, 0, sizeof(IcBox));
	lib->queue = Queue_create();
	lib->mutex = Mutex_create();
	lib->writeMutex = Mutex_create();
	lib->canceled = 0;
	lib->evCancel = Event_create();
	lib->evReceive = Event_createEx(0, 0);
	lib->thReceive = Thread_create(_IcBox_receiveFunc, lib);
	lib->thConnect = Thread_create(_IcBox_connectFunc, lib);
	lib->comm = NULL;
	lib->port[0] = 0;
	lib->commSettings.baund = 9600;
	lib->commSettings.data = 8;
	lib->commSettings.stop = StopBits_One;
	lib->commSettings.parity = Parity_None;
	lib->commSettings.flow = Flow_None;
	lib->connectTimeout = 1000;
	lib->retryTimeout = 1500;
	lib->requestAliveInterval = 10000;
	lib->config = StringBuilder_create();
	IcBox_setConfiguration(lib, config);
	Thread_start(lib->thConnect);
	return lib;
}

LIBEXPORT int LIBCALL IcBox_isConnected(IcBox * lib)
{
	return lib->comm != NULL;
}

LIBEXPORT void LIBCALL IcBox_setConfiguration(IcBox * lib, const char * config)
{
	if(config == NULL)
		return;
	char buff[PROPERY_BUFFER_SIZE];
	char * lwconfig = (char*)malloc(sizeof(char) * (strlen(config) + 1));
	strcpy(lwconfig, config);
	strlwr(lwconfig);
	int portChanged = 0;
	int commSettingsChanged = 0;
	if(_IcBox_getProperty(lwconfig, config, "port", buff, PROPERY_BUFFER_SIZE))
	{
		if(strcmp(lib->port, buff) != 0)
			portChanged = 1;
		strcpy(lib->port, buff);
	}
	if(_IcBox_getProperty(lwconfig, config, "baund", buff, PROPERY_BUFFER_SIZE))
	{
		lib->commSettings.baund = atoi(buff);
		commSettingsChanged = 1;
	}
	if(_IcBox_getProperty(lwconfig, config, "data", buff, PROPERY_BUFFER_SIZE))
	{
		lib->commSettings.data = atoi(buff);
		commSettingsChanged = 1;
	}
	if(_IcBox_getProperty(lwconfig, config, "stop", buff, PROPERY_BUFFER_SIZE))
	{
		if(strcmp(buff, "1.5") == 0)
			lib->commSettings.stop = StopBits_OneAndHalf;
		else if(strcmp(buff, "2") == 0)
			lib->commSettings.stop = StopBits_Two;
		else
			lib->commSettings.stop = StopBits_One;
		commSettingsChanged = 1;
	}
	if(_IcBox_getProperty(lwconfig, config, "parity", buff, PROPERY_BUFFER_SIZE))
	{
		if(strcmp(buff, "space") == 0)
			lib->commSettings.parity = Parity_Space;
		else if(strcmp(buff, "mark") == 0)
			lib->commSettings.parity = Parity_Mark;
		else if(strcmp(buff, "even") == 0)
			lib->commSettings.parity = Parity_Even;
		else if(strcmp(buff, "odd") == 0)
			lib->commSettings.parity = Parity_Odd;
		else
			lib->commSettings.parity = Parity_None;
		commSettingsChanged = 1;
	}
	if(_IcBox_getProperty(lwconfig, config, "flow", buff, PROPERY_BUFFER_SIZE))
	{
		if(strcmp(buff, "dsrdtr") == 0)
			lib->commSettings.flow = Flow_DSRDTR;
		else if(strcmp(buff, "rtscts") == 0)
			lib->commSettings.flow = Flow_RTSCTS;
		else if(strcmp(buff, "xonxoff") == 0)
			lib->commSettings.flow = Flow_XONXOFF;
		else
			lib->commSettings.flow = Flow_None;
		commSettingsChanged = 1;
	}
	if(_IcBox_getProperty(lwconfig, config, "timeout", buff, PROPERY_BUFFER_SIZE))
	{
		int tm = atoi(buff);
		if(tm >= 50)
			lib->connectTimeout = tm;
	}
	if(_IcBox_getProperty(lwconfig, config, "retry", buff, PROPERY_BUFFER_SIZE))
	{
		int tm = atoi(buff);
		if(tm >= 0)
			lib->retryTimeout = tm;
	}
	if(_IcBox_getProperty(lwconfig, config, "alive", buff, PROPERY_BUFFER_SIZE))
	{
		int tm = atoi(buff);
		if(tm >= 1000)
			lib->requestAliveInterval = tm;
	}
	free(lwconfig);
	if(IcBox_isConnected(lib))
	{
		if(portChanged)
		{
			lib->canceled = 1;
			CommPort_cancel(lib->comm);
			Thread_join(lib->thReceive);
			_IcBox_reconnect(lib);// try connect again
		} 
		else if(commSettingsChanged) 
		{
			if(!CommPort_configure(lib->comm, &lib->commSettings))
			{
				lib->canceled = 1;
				CommPort_cancel(lib->comm);
				Thread_join(lib->thReceive);
				_IcBox_reconnect(lib);// try connect again
			}
		}
	}
}

LIBEXPORT const char* LIBCALL IcBox_getConfiguration(IcBox * lib)
{
	StringBuilder_clear(lib->config);
	// Port
	if(lib->port[0] != 0)
		StringBuilder_appendFormat(lib->config, "port:%s;", lib->port);
	// Baund
	StringBuilder_appendFormat(lib->config, "baund:%d;", lib->commSettings.baund);
	// Data Bits
	StringBuilder_appendFormat(lib->config, "data:%d;", (int)lib->commSettings.data);
	// Stop Bits
	StringBuilder_append(lib->config, "stop:");
	if(lib->commSettings.stop == StopBits_OneAndHalf)
		StringBuilder_append(lib->config, "1.5;");
	else if(lib->commSettings.stop == StopBits_Two)
		StringBuilder_append(lib->config, "2;");
	else
		StringBuilder_append(lib->config, "1;");
	// Parity
	StringBuilder_append(lib->config, "parity:");
	if(lib->commSettings.parity == Parity_Space)
		StringBuilder_append(lib->config, "space;");
	else if(lib->commSettings.parity == Parity_Mark)
		StringBuilder_append(lib->config, "mark;");
	else if(lib->commSettings.parity == Parity_Even)
		StringBuilder_append(lib->config, "even;");
	else if(lib->commSettings.parity == Parity_Odd)
		StringBuilder_append(lib->config, "odd;");
	else
		StringBuilder_append(lib->config, "none;");
	// Flow
	StringBuilder_append(lib->config, "flow:");
	if(lib->commSettings.flow == Flow_DSRDTR)
		StringBuilder_append(lib->config, "dsrdtr;");
	else if(lib->commSettings.flow == Flow_RTSCTS)
		StringBuilder_append(lib->config, "rtscts;");
	else if(lib->commSettings.flow == Flow_XONXOFF)
		StringBuilder_append(lib->config, "xonxoff;");
	else
		StringBuilder_append(lib->config, "none;");
	// timeout
	StringBuilder_appendFormat(lib->config, "timeout:%d;", lib->connectTimeout);
	StringBuilder_appendFormat(lib->config, "retry:%d;", lib->retryTimeout);
	StringBuilder_appendFormat(lib->config, "alive:%d;", lib->requestAliveInterval);
	return StringBuilder_getData(lib->config);
}

LIBEXPORT int LIBCALL IcBox_pollEvent(IcBox * lib, IcBoxEvent* event)
{
	Event* events[2];
#ifdef DEBUGLIB
		printf("IcBox_pollEvent\n");
#endif
	events[0] = lib->evCancel;
	events[1] = lib->evReceive;
	Event* object = NULL;
	if(Queue_empty(lib->queue))
	{
		Event_waitMultiple(events, 2);
#ifdef DEBUGLIB
		printf("IcBox_pollEvent[Event_waitMultiple %d items]\n", Queue_count(lib->queue));
	} else {
		printf("IcBox_pollEvent[Queue not empty %d items]\n", Queue_count(lib->queue));
#endif
	}
	if(object == lib->evCancel || lib->canceled == 1)
		return 0;
	Mutex_lock(lib->writeMutex);
	IcBoxEvent* qEvent = (IcBoxEvent*)Queue_pop(lib->queue);
	Event_reset(lib->evReceive);
	Mutex_unlock(lib->writeMutex);
	memcpy(event, qEvent, sizeof(IcBoxEvent));
	free(qEvent);
	return 1;
}

LIBEXPORT void LIBCALL IcBox_cancel(IcBox * lib)
{
	Mutex_lock(lib->writeMutex);
	lib->canceled = 1;
	if(lib->comm != NULL)
		CommPort_cancel(lib->comm);
	Event_post(lib->evCancel);
	Mutex_unlock(lib->writeMutex);
}

LIBEXPORT void LIBCALL IcBox_free(IcBox * lib)
{
	IcBox_cancel(lib);
	Event_post(lib->evReceive);
	Thread_join(lib->thReceive);
	Thread_join(lib->thConnect);
	Thread_free(lib->thReceive);
	Thread_free(lib->thConnect);
	Event_free(lib->evCancel);
	Event_free(lib->evReceive);
	if(lib->comm != NULL)
		CommPort_free(lib->comm);
	StringBuilder_free(lib->config);
	Mutex_lock(lib->writeMutex);
	while(!Queue_empty(lib->queue))
	{
		IcBoxEvent * event = (IcBoxEvent*)Queue_pop(lib->queue);
		free(event);
	}
	Mutex_unlock(lib->writeMutex);
	Mutex_free(lib->writeMutex);
	Mutex_free(lib->mutex);
	Queue_free(lib->queue);
	free(lib);
}

LIBEXPORT const char* LIBCALL IcBox_getVersion()
{
	return gVersion;
}

int IcBox_initialize()
{
#ifdef DEBUGLIB
	printf("testing string builder\n");
	StringBuilder* builder = StringBuilder_create();
	StringBuilder_append(builder, "Hello World!");
	StringBuilder_append(builder, "\n");
	StringBuilder_getData(builder);
	int i;
	for(i = 0; i < 10000; i++)
		StringBuilder_append(builder, "Composes a string with the same text that would be printed if format was used on printf, but using the elements in the variable argument list identified by arg instead of additional function arguments and storing the resulting content as a C string in the buffer pointed by s (taking n as the maximum buffer capacity to fill).");
	StringBuilder_getData(builder);
	StringBuilder_append(builder, "\n");
	StringBuilder_clear(builder);
	StringBuilder_append(builder, "Composes a string with the same text that would be printed if format was used on printf, but using the elements in the variable argument list identified by arg instead of additional function arguments and storing the resulting content as a C string in the buffer pointed by s (taking n as the maximum buffer capacity to fill).");
	StringBuilder_append(builder, "\n");
	StringBuilder_getData(builder);
	for(i = 0; i < 10000; i++)
		StringBuilder_appendFormat(builder, "%d %.2f %s %c %p\n", 256, 1.5f, "String", 'C', builder);
	StringBuilder_getData(builder);
	StringBuilder_free(builder);
	printf("library initialized\n");
#endif
	return 1;
}

void IcBox_terminate()
{
#ifdef DEBUGLIB
	printf("library terminated\n");
#endif
}
