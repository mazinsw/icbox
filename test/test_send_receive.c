#include <stdio.h>
#include <stdlib.h>
#include <IcBox.h>
#include <windows.h>

typedef IcBox * (LIBCALL * IcBox_createFunc)(const char*);
typedef int (LIBCALL * IcBox_pollEventFunc)(IcBox*, IcBoxEvent*);
typedef void (LIBCALL * IcBox_cancelFunc)(IcBox*);
typedef void (LIBCALL * IcBox_freeFunc)(IcBox*);
typedef void (LIBCALL * IcBox_setConfigurationFunc)(IcBox*, const char*);
typedef const char * (LIBCALL * IcBox_getConfigurationFunc)(IcBox*);

int main(int argc, char *argv[])
{
	DWORD startTick;
	IcBoxEvent event;
	DWORD endTick;
	IcBox* lib;
	HMODULE hModule;
	
	startTick = GetTickCount();
	hModule = LoadLibrary("../bin/x86/IcBox.dll");
	IcBox_createFunc _IcBox_create;
	IcBox_pollEventFunc _IcBox_pollEvent;
	IcBox_setConfigurationFunc _IcBox_setConfiguration;
	IcBox_getConfigurationFunc _IcBox_getConfiguration;
	IcBox_cancelFunc _IcBox_cancel;
	IcBox_freeFunc _IcBox_free;
	if(hModule == 0)
		return 1;
	_IcBox_create = (IcBox_createFunc)GetProcAddress(hModule, "IcBox_create");
	_IcBox_pollEvent = (IcBox_pollEventFunc)GetProcAddress(hModule, "IcBox_pollEvent");
	_IcBox_setConfiguration = (IcBox_setConfigurationFunc)GetProcAddress(hModule, "IcBox_setConfiguration");
	_IcBox_getConfiguration = (IcBox_getConfigurationFunc)GetProcAddress(hModule, "IcBox_getConfiguration");
	_IcBox_cancel = (IcBox_cancelFunc)GetProcAddress(hModule, "IcBox_cancel");
	_IcBox_free = (IcBox_freeFunc)GetProcAddress(hModule, "IcBox_free");
	lib = _IcBox_create(NULL);
	while(_IcBox_pollEvent(lib, &event))
	{
		switch(event.type)
		{
		case ICBOX_EVENT_CONNECTED:
			endTick = GetTickCount() - startTick;
			printf("IcBox connected\n");
			printf("Time: %.3lfs\n", endTick / 1000.0);
			break;
		case ICBOX_EVENT_DISCONNECTED:
			printf("IcBox disconnected\n");
			startTick = GetTickCount();
			break;
		case ICBOX_EVENT_RING:
			printf("IcBox riging\n");
			break;
		case ICBOX_EVENT_CALLERID:
			printf("Number: %s\n", event.data);
			break;
		case ICBOX_EVENT_ONHOOK:
			printf("IcBox on-hook\n");
			break;
		case ICBOX_EVENT_OFFHOOK:
			printf("IcBox off-hook\n");
			break;
		case ICBOX_EVENT_HANGUP:
			printf("IcBox hang up\n");
			break;
		default:
			printf("Event %d received\n", event.type);
		}
	}
	_IcBox_free(lib);
	FreeLibrary(hModule);
	return 0;
}
