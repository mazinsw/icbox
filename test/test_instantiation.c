#include <stdio.h>
#include <stdlib.h>
#include <IcBox.h>
#include <windows.h>
typedef IcBox * (LIBCALL * IcBox_createFunc)(const char*);
typedef void (LIBCALL * IcBox_freeFunc)(IcBox*);

int main(int argc, char *argv[])
{
	HMODULE hModule = LoadLibrary("../bin/x86/IcBox.dll");
	IcBox_createFunc _IcBox_create;
	IcBox_freeFunc _IcBox_free;
	if(hModule == 0)
		return 1;
	_IcBox_create = (IcBox_createFunc)GetProcAddress(hModule, "IcBox_create");
	_IcBox_free = (IcBox_freeFunc)GetProcAddress(hModule, "IcBox_free");
	
	IcBox* lib = _IcBox_create(NULL);
	system("pause");
	_IcBox_free(lib);
	FreeLibrary(hModule);
	return 0;
}
