#include <private/Platform.h>
#include <windows.h>

#ifdef BUILD_DLL
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch(fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		return IcBox_initialize();
	case DLL_PROCESS_DETACH:
		IcBox_terminate();
		break;
	}
	return TRUE; // succesful
}
#endif

#if !defined(BUILD_DLL) && !defined(LIB_STATIC)
#include "IcBox.h"
#include "Thread.h"
#include <windows.h>
#include <stdio.h>
#include <conio.h>

void inputListener(void * data)
{
	IcBox* lib = (IcBox*)data;
	int done = 0;
	char config[100], input[100];
	
	while(!done)
	{
		int ch = getch();
		switch(ch)
		{
		case '\b':
			system("cls");
			break;
		case '\'':
			// port:COM2;baund:9600;data:8;stop:1;parity:none;flow:none;timeout:1000;retry:1500;alive:10000;
			config[0] = '\0';
			input[0] = '\0';
			printf("Digite a porta: ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "port:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite a velocidade: ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "baund:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite os bits de dados: ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "data:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite os bits de parada (1.5, 2): ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "stop:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite a paridade (none, space, mark, even, odd): ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "parity:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite o fluxo (dsrdtr, rtscts, xonxoff): ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "flow:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite o tempo de espera: ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "timeout:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite o tempo de reconexao: ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "retry:");
				strcat(config, input);
				strcat(config, ";");
			}
			input[0] = '\0';
			printf("Digite o tempo de vida da conexao: ");
			scanf("%99[^\n]", input);
			scanf("%*1[\n]");
			if(input[0] != '\0')
			{
				strcat(config, "alive:");
				strcat(config, input);
				strcat(config, ";");
			}
			IcBox_setConfiguration(lib, config);
			printf("OK\n");
			break;
		case '?':
			printf("Comandos:\n\n");
			printf("\tLimpar tela: Backspace\n");
			printf("\tMostrar configuracao: C\n");
			printf("\tAlterar configuracao: '\n");
			printf("\tSair: Q\n\n");
			break;
		case 'c':
		case 'C':
			printf("Configuracao:\n%s\n\n", IcBox_getConfiguration(lib));
			break;
		case 'q':
		case 'Q':
			done = 1;
			IcBox_cancel(lib);
			break;
		}
	}
}

int main(int argc, char** argv)
{
	DWORD startTick;
	IcBoxEvent event;
	DWORD endTick;
	IcBox* lib;
	
	startTick = GetTickCount();
	IcBox_initialize();
	lib = IcBox_create(argc > 0 ? argv[1] : NULL);
	Thread* thInput = Thread_create(inputListener, lib);
    Thread_start(thInput);
	while(IcBox_pollEvent(lib, &event))
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
	IcBox_free(lib);
	IcBox_terminate();
	Thread_join(thInput);
	return 0;
}
#endif