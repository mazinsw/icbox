#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <Event.h>
#include <Thread.h>
#include <CommPort.h>

typedef enum RingState {
	rsIdle = 0,
	rsRing,
	rsAnswer,
	rsHangUp,
	rsNoCarrier,
} RingState;

typedef struct _IcBox
{
	Thread* thread;
	int deviceResult;
	Event* deviceEvent;
	Event* deviceReady;
	CommPort* commPort;
	// random ring
	Thread* ringThread;
	Event* ringEvent;
	Event* onHook;
	int ringRunning;
	int randomTimeout;
	int ringState;
	// listen
	Thread* listenThread;
	int done;
	char number[20];
} IcBox;

// include VSPE API header and library
#include "VSPE_API.h"

void createDevices(void* data)
{
	IcBox* icBox = (IcBox*)data;
    // ****************************
    // STEP 1 - INITIALIZATION
    // ****************************
    const char* activationKey = "SRBGMZSYPuIHWILsmLjF5CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPRJEEYxlJg+4gdYguyYuMXkIPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9CDyBL3GQYD0IPIEvcZBgPQg8gS9xkGA9JyUaC2ZWE1DZV2+wYWlRm7FFYrW3MDbZg8MkQsOQ8r1IPIEvcZBgPQg8gS9xkGA9LzrhjimHDiMlKqr6pSiw9CDl9n+0bAgFr2ho7nXjCoTMHYzt4tsbEkJGNktLGVG42SZ63UbmIUNcKmfhSzXldVCLhfvZv3StR9c/vkYG471Nh62eC1qIYuBUvm+a3BK8iR0POD8w5ovtuYr0T8aQP3eh4b8lUwnPHG9NRJxerttq/+/zX7c++9LDSQym3ThbWesK+A+X/vNw9qDgYt1dsJxDEEytsCRiT7bTiV5Djh1RlpIwETXWA089hiE9OYd7GpjKLq5dQOqSVcA3Fg1Wfdbqn/yn8q0/AIDOd0iZlbVeLY68zKh1Di4gGEoa1kR8EOBp2mxeaFrfwUm3DsJ5Pc04f7aEw9XljfBUwl/bAs3LVH5HRii8lXZvUVvnnfpcQ==1F250CF0960AE1C09E9450C816DE1232"; // <-----  PUT ACTIVATION KEY HERE
    bool result;
    // activate VSPE API
    result = vspe_activate(activationKey);
    if(result == false){
        printf("VSPE API activation error\n");
        icBox->deviceResult = 1;
		Event_post(icBox->deviceReady);
		return;
    }
    // initialize VSPE python binding subsystem
    result = vspe_initialize();
    if(result == false){
        printf("Initialization error\n");
        icBox->deviceResult = 1;
		Event_post(icBox->deviceReady);
		return;
    }
    // remove all existing devices
    vspe_destroyAllDevices();
    // stop current emulation
    result = vspe_stopEmulation();
    if(result == false)
    {
        printf("Error: emulation can not be stopped: maybe one of VSPE devices is still used.\n");
        vspe_release();
        icBox->deviceResult = 1;
		Event_post(icBox->deviceReady);
		return;
    }
    // *********************************
    // Dynamically creating devices
    // *********************************
    // Create Pair device (COM4 => COM5)
    int deviceId = vspe_createDevice("Pair", "4;5;0");
    if(deviceId == -1)
    {
        printf("Error: can not create device\n");
        vspe_release();
        icBox->deviceResult = 1;
		Event_post(icBox->deviceReady);
		return;
    }
    // ****************************
    // STEP 2 - EMULATION LOOP
    // ****************************
    // start emulation
    result = vspe_startEmulation();
    if(result == false)
    {
        printf("Error: can not start emulation\n");
        vspe_release();
        icBox->deviceResult = 1;
		Event_post(icBox->deviceReady);
		return;
    }
    icBox->deviceResult = 0;
	Event_post(icBox->deviceReady);
    // emulation loop
    Event_wait(icBox->deviceEvent);
    // ****************************
    // STEP 3 - EXIT
    // ****************************

    // stop emulation before exit (skip this call to force kernel devices continue to work)
    result = vspe_stopEmulation();
    if(result == false)
    {
        printf("Error: emulation can not be stopped: maybe one of VSPE devices is still used.\n");
        icBox->deviceResult = 1;
		return;
    }
    vspe_release();
    icBox->deviceResult = 0;
    printf("Emulation stopped successful.\n");
}

void sendString(IcBox *icBox, const char * str) 
{
	CommPort_write(icBox->commPort, (unsigned char*)str, strlen(str));
}

void randomRing(void* data)
{
	IcBox* icBox = (IcBox*)data;
	char cmd[100];
	srand(time(NULL));
	while(icBox->ringRunning)
	{
		// above 5 seconds and 6 minutes bellow
		icBox->randomTimeout = 5000 + (rand() % (6 * 60 + 1)) * 1000;
		printf("Waiting %dmin %ds to Ring...\n", icBox->randomTimeout / 60000, 
			(icBox->randomTimeout % 60000) / 1000);
		Event_waitEx(icBox->ringEvent, icBox->randomTimeout);
		Event_reset(icBox->ringEvent);
		if(!icBox->ringRunning)
			break;
		printf("Ringing...\n");
		icBox->ringState = rsRing;
        strcpy(cmd, "          ");
        strcat(cmd, icBox->number);
        strcat(cmd, "E001000000000000I000000\r\n");
        sendString(icBox, cmd);
		Thread_wait(5000);
		int i;
		for(i = 0; i < 5; i++)
		{
			if(icBox->ringState == rsRing) {
				Thread_wait(5000);
			} else 
				break;
		}
		if(icBox->ringState == rsAnswer)
		{
			icBox->onHook = Event_create();
			Event_wait(icBox->onHook);
			if(icBox->ringState == rsAnswer)
				icBox->ringState = rsIdle;
			Event_free(icBox->onHook);
			icBox->onHook = NULL;
		} else {			
			icBox->ringState = rsNoCarrier;
		}
	}
}

void createRandomRing(IcBox *icBox) 
{
	icBox->ringEvent = Event_create();
	icBox->ringThread = Thread_create(randomRing, icBox);
	icBox->ringRunning = 1;
	Thread_start(icBox->ringThread);
}

void freeRandomRing(IcBox *icBox) 
{
	icBox->ringRunning = 0;
	icBox->ringState = rsNoCarrier;
	if(icBox->onHook != NULL)
		Event_post(icBox->onHook);
	Event_post(icBox->ringEvent);
	Thread_join(icBox->ringThread);
	Thread_free(icBox->ringThread);
	Event_free(icBox->ringEvent);
}

void proccessCommand(IcBox *icBox, const char * cmd) 
{
	printf("%s\n", cmd);
	if(stricmp(cmd, "@CX\r\n") == 0)
    {
        sendString(icBox, "F\r\n");
    }
}

void listenCommands(void* data)
{
	IcBox* icBox = (IcBox*)data;
	char buffer[256];
	int bytesRead, bytesToRead;
	
	while(!icBox->done)
	{
        bytesToRead = 256;
        bytesRead = CommPort_read(icBox->commPort, (unsigned char*)buffer, bytesToRead);
        if(!bytesRead)
            continue;
        buffer[bytesRead] = '\0';
        proccessCommand(icBox, buffer);
	}
}

void mainMenu(IcBox * icBox)
{
	char cmd[256];
	while(scanf(" %255[^\n]", cmd) != EOF && stricmp(cmd, "EXIT") != 0)
	{
		// force ring
		if(stricmp(cmd, "RING") == 0)
		{
			Event_post(icBox->ringEvent);
			printf("OK\n");
		}
		// force ring
		else if(strnicmp(cmd, "SET ", 4) == 0)
		{
			if(strnicmp(cmd + 4, "NUMBER ", 7) == 0)
			{
				strcpy(icBox->number, cmd + 11);
				printf("OK\n");
			}
			else if(strnicmp(cmd + 4, "NAME ", 5) == 0)
			{
				strcpy(icBox->number, cmd + 9);
				printf("OK\n");
			}
		}
	}
	printf("Exiting...\n");
}

int main(int argc, char** argv)
{
	IcBox icBox;
	memset(&icBox, 0, sizeof(IcBox));
	strcpy(icBox.number, "086987654321");
	icBox.deviceEvent = Event_create();
	icBox.deviceReady = Event_create();
	icBox.thread = Thread_create(createDevices, &icBox);
	Thread_start(icBox.thread);
    Event_wait(icBox.deviceReady);
	if(!icBox.deviceResult)
	{
		icBox.commPort = CommPort_create("COM5");
		createRandomRing(&icBox);
		icBox.listenThread = Thread_create(listenCommands, &icBox);
		Thread_start(icBox.listenThread);
		mainMenu(&icBox);
		icBox.done = 1;
		freeRandomRing(&icBox);
		CommPort_free(icBox.commPort);
		Thread_join(icBox.listenThread);
		Thread_free(icBox.listenThread);
	}
	Event_post(icBox.deviceEvent);
	Thread_join(icBox.thread);
	Thread_free(icBox.thread);
	Event_free(icBox.deviceEvent);
	Event_free(icBox.deviceReady);
	return 0;
}