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

static const char gVersion[] = "1.1.0.0";
static const int REPEAT_BAUND = 9600;
static const int baundValues[] = {115200, 57600, 19200, 9600, 14400, 38400, 230400, 460800, 921600, 110, 300, 600, 1200, 2400, 4800};

struct IcBox
{
    int canceled;
    int connected;
    int abandoned;
    int testing;
    Thread* thConnect;
    Thread* thReceive;
    Thread* thAlive;
    Event* evCancel;
    Event* evDetected;
    Event* evReceive;
    CommPort * comm;
    int connectTimeout;
    int retryTimeout;
    int requestAliveInterval;
    char port[8];
    int baundIndex;
    CommSettings commSettings;
    StringBuilder* config;
    Mutex* mutex;
    Mutex* writeMutex;
    char buffer[BUFFER_SIZE];
    int bufferPos;
    Queue* queue;
};

static int _IcBox_echoTest(IcBox * lib);

static void _IcBox_disconnect(IcBox * lib)
{
#ifdef DEBUGLIB
    printf("Disconnecting device\n");
#endif
    lib->abandoned = 1;
    Mutex_lock(lib->writeMutex);
    if(lib->comm != NULL)
        CommPort_cancel(lib->comm);
    Mutex_unlock(lib->writeMutex);
    Thread_join(lib->thReceive);
    Mutex_lock(lib->writeMutex);
    if(lib->comm != NULL)
        CommPort_free(lib->comm);
    lib->comm = NULL;
    Mutex_unlock(lib->writeMutex);
    lib->abandoned = 0;
    lib->connected = 0;
#ifdef DEBUGLIB
    printf("Device disconnected\n");
#endif
}

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

static void _IcBox_cancel(IcBox * lib)
{
#ifdef DEBUGLIB
    printf("Canceling connection\n");
#endif
    _IcBox_disconnect(lib);
    if(lib->canceled)
        Event_post(lib->evCancel);
    _IcBox_pushEvent(lib, ICBOX_EVENT_DISCONNECTED, NULL, 0);
#ifdef DEBUGLIB
    printf("Connection canceled\n");
#endif
}

static void _IcBox_reconnect(IcBox * lib)
{
    _IcBox_cancel(lib);
    Thread_start(lib->thConnect); // try connect again
}

static int _IcBox_execute(IcBox * lib, const unsigned char * buffer, int size)
{
    if (size < 3) {
        return 0;
    }
    if (buffer[0] == 'F' || buffer[0] == 'I' || buffer[0] == 'A' ||
        buffer[0] == 'D' || buffer[0] == 'O')
    {
        if (buffer[1] == '\r' && buffer[2] == '\n')
        {
            Event_post(lib->evDetected);
            return 3;
        }
        return 0;
    }
    if (size < 47) {
        return 0;
    }
    if (buffer[22] != 'E' || buffer[45] != '\r' || buffer[46] != '\n')
    {
        return 0;
    }
    Event_post(lib->evDetected);
    if (!lib->testing && buffer[22] == 'E')
    {
        Event_reset(lib->evDetected);
        if (buffer[38] == 'I')
        {
            _IcBox_pushEvent(lib, ICBOX_EVENT_CALLERID, 
                (const char*)buffer + 10, 12);
        }
        else if (buffer[38] == 'D')
        {
            _IcBox_pushEvent(lib, ICBOX_EVENT_HANGUP, NULL, 0);
        }
        else if (buffer[38] == 'A')
        {
            _IcBox_pushEvent(lib, ICBOX_EVENT_OFFHOOK, NULL, 0);
        }
    }
    return 47;
}

static int _IcBox_dataReceived(IcBox * lib, const unsigned char * buffer, 
    int size)
{
    int consummed = 0;
    int min_response, max_response, offset;
#ifdef DEBUGLIB
    printf("Data received (%d byte): %s\n", size, buffer);
#endif
    min_response = 3;
    max_response = 47;
    
    offset = 0;
    while((size - offset) >= min_response)
    {
        consummed = _IcBox_execute(lib, buffer + offset, size - offset);
        if(consummed)
            break;
        offset++;
    }
    consummed += offset;
    if(consummed == 0 && size > max_response)
        consummed = size - max_response + 1; // j� verificou esses bytes
    return consummed;
}

static void _IcBox_aliveTest(void* data)
{
    IcBox * lib = (IcBox*)data;
    if(!_IcBox_echoTest(lib))
    {
        if (lib->canceled)
            return;
        _IcBox_reconnect(lib);// try connect again
    }
#ifdef DEBUGLIB
    else 
    {
        printf("Device alive\n");
    }
#endif
}

static void _IcBox_receiveFunc(void* data)
{
    IcBox * lib = (IcBox*)data;
    unsigned char buffer[BUFFER_SIZE];
    unsigned char window[BUFFER_SIZE + 1];
    int window_size = 0;
    int buffer_size;
    int remaining, consumed, unused, used;

    while(!lib->canceled && !lib->abandoned)
    {
#ifdef DEBUGLIB
    printf("Waiting for receive event\n");
#endif
        buffer_size = CommPort_readEx(lib->comm, buffer, BUFFER_SIZE, lib->requestAliveInterval);
#ifdef DEBUGLIB
        printf("Bytes received: %d\n", buffer_size);
#endif
        buffer_size = buffer_size < BUFFER_SIZE?buffer_size:BUFFER_SIZE;
        if(lib->canceled || lib->abandoned)
            continue;
        if(buffer_size == 0 && !lib->testing)
        {
            Thread_start(lib->thAlive);
            continue;
        }
        if(buffer_size == 0)
            continue;
#ifdef DEBUGLIB
        printf("Bytes read: %d\n", buffer_size);
#endif
        remaining = buffer_size;
        consumed = 0;
        while(remaining > 0 || consumed > 0)
        {
            unused = BUFFER_SIZE - window_size;
            consumed = remaining < unused?remaining:unused;
            if(consumed == 0 && remaining > 0)
                consumed++; // consome pelo menos 1 byte
            used = buffer_size - remaining;
            remaining -= consumed;
            if(consumed > unused)
            {
                memmove(window, window + consumed, BUFFER_SIZE - consumed);
                window_size -= consumed;
            }
            memcpy(window + window_size, buffer + used, consumed);
            window_size += consumed;
#ifdef DEBUGLIB
            window[window_size] = 0; // permite mostrar como texto
#endif
            consumed = _IcBox_dataReceived(lib, window, window_size);
            window_size -= consumed;
            if(window_size > 0)
                memmove(window, window + consumed, BUFFER_SIZE - consumed);
            if(consumed > 0 && window_size == 0)
                consumed = 0;
        }
    }
}

static int _IcBox_sendCmd(IcBox * lib, const char * buffer)
{
    int size, bwritten;

    size = strlen(buffer);
#ifdef DEBUGLIB
    printf("Sending active check command...\n");
#endif
    Mutex_lock(lib->writeMutex);
    bwritten = CommPort_writeEx(lib->comm, (unsigned char*)buffer, size,
        lib->connectTimeout / 2);
    Mutex_unlock(lib->writeMutex);
    return bwritten == size;
}

static int _IcBox_echoTest(IcBox * lib)
{
    int old_testing = lib->testing;
#ifdef DEBUGLIB
    printf("Echo test started\n");
#endif
    lib->testing = 1;
    _IcBox_sendCmd(lib, "@CX\r\n");
    Event* object = Event_waitMultipleEx(&lib->evDetected, 1, lib->connectTimeout);
    lib->testing = old_testing;
    if(object != lib->evDetected)
        return 0;
    return 1;
}

static void _IcBox_connectFunc(void* data)
{
    IcBox * lib = (IcBox*)data;
    CommPort* comm;
    int i, need, count, len, tried;
    char * ports, * port;
#ifdef DEBUGLIB
    printf("Starting connection\n");
#endif
    if(lib->canceled || lib->abandoned) 
    {
#ifdef DEBUGLIB
        printf("Connection aborted\n");
#endif
        return;
    }
    i = 0;
    count = 0;
    need = 4096;
    do
    {
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
        int port_count = CommPort_enum(ports + len, need);
        if(port_count == 0)
        {
#ifdef DEBUGLIB
            printf("Error on enum ports\n");
#endif
            break;
        }
        if(port_count < 0)
        {
            if(i > 1)
            {
#ifdef DEBUGLIB
                printf("Error trying to enum ports again, stoped!\n");
#endif
                break;
            }
#ifdef DEBUGLIB
            printf("Enum ports, need more %d byte of memory\n", -port_count);
#endif
            need = -port_count;
            i++;
            free(ports);
            continue;	
        }
        count += port_count;
        break;
    } while(1);
#ifdef DEBUGLIB
    printf("Searching port... %d found\n", count);
#endif
    tried = 1;
    while(!lib->canceled && !lib->abandoned)
    {
        // try connect to one port
        comm = NULL;
        port = ports;
        for(i = 1; i <= count; i++)
        {
#ifdef DEBUGLIB
            printf("Trying connect to %s baund %d\n", port, lib->commSettings.baund);
#endif
            comm = CommPort_createEx(port, &lib->commSettings);
            if(comm != NULL)
            {
                // connection successful, start receive event
                lib->comm = comm;
                Thread_start(lib->thReceive);
                if(_IcBox_echoTest(lib))
                    break;
                _IcBox_disconnect(lib);
                comm = NULL;
            }
            if(lib->canceled || lib->abandoned)
                break;
            port += strlen(port) + 1;
        }
        if(comm != NULL)
        {
#ifdef DEBUGLIB
            printf("Device connected using %s\n", port);
#endif
            lib->connected = 1;
            strcpy(lib->port, port);
            _IcBox_pushEvent(lib, ICBOX_EVENT_CONNECTED, NULL, 0);
            break;
        }
#ifdef DEBUGLIB
        printf("No port available, trying again\n");
#endif
        if(lib->canceled || lib->abandoned)
            break;
        // not port available, wait few seconds and try again
        Thread_wait(lib->retryTimeout);
        lib->baundIndex = (lib->baundIndex + 1) % (sizeof(baundValues) / sizeof(int));
        if(tried < 3 && baundValues[lib->baundIndex] > REPEAT_BAUND)
        {
            lib->baundIndex = 0;
            tried++;
        }
        else if(tried >= 3 && lib->baundIndex == 0)
        {
            tried = 1;
        }
        lib->commSettings.baund = baundValues[lib->baundIndex];
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
    lib->connected = 0;
    lib->testing = 0;
    lib->abandoned = 0;
    lib->canceled = 0;
    lib->baundIndex = 0;
    lib->evCancel = Event_create();
    lib->evDetected = Event_createEx(0, 0);
    lib->evReceive = Event_createEx(0, 0);
    lib->thReceive = Thread_create(_IcBox_receiveFunc, lib);
    lib->thConnect = Thread_create(_IcBox_connectFunc, lib);
    lib->thAlive = Thread_create(_IcBox_aliveTest, lib);
    lib->comm = NULL;
    lib->port[0] = 0;
    lib->commSettings.baund = baundValues[lib->baundIndex];
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
    if(lib->comm == NULL)
        return;
    if(!portChanged && !commSettingsChanged)
        return;
    if(commSettingsChanged) 
        CommPort_configure(lib->comm, &lib->commSettings);
    _IcBox_reconnect(lib);
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
    if(object == lib->evCancel || lib->canceled)
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
    if(lib->canceled)
        return;
    lib->canceled = 1;
    _IcBox_cancel(lib);
}

LIBEXPORT void LIBCALL IcBox_free(IcBox * lib)
{
    IcBox_cancel(lib);
    Thread_join(lib->thAlive);
    Thread_join(lib->thReceive);
    Thread_join(lib->thConnect);
    Thread_free(lib->thAlive);
    Thread_free(lib->thReceive);
    Thread_free(lib->thConnect);
    Event_free(lib->evCancel);
    Event_free(lib->evReceive);
    Event_free(lib->evDetected);
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
