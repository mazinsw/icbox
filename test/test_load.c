#include <stdio.h>
#include <stdlib.h>
#include <IcBox.h>
#include <windows.h>

int main(int argc, char *argv[])
{
	HMODULE hModule = LoadLibrary("../bin/x86/IcBox.dll");
	if(hModule == 0)
		return 1;
	FreeLibrary(hModule);
	return 0;
}
