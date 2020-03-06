RM     = rm -f
OBJS   = build/obj/IcBox.o \
         build/obj/win/main.o \
         build/obj/win/CommPort.o \
         build/obj/win/Event.o \
         build/obj/win/Mutex.o \
         build/obj/win/Thread.o \
         build/obj/util/StringBuilder.o \
         build/obj/util/Queue.o \
         build/obj/AppResource.res

shared64: build/x64/IcBox.dll
shared32: build/x86/IcBox.dll

static64: build/x64/IcBoxTest.exe
static32: build/x86/IcBoxTest.exe

build64:
	$(eval RCFLAGS=-O coff -F pe-x86-64)
	$(eval CC=x86_64-w64-mingw32-gcc)
	$(eval WINDRES=x86_64-w64-mingw32-windres)

build32:
	$(eval RCFLAGS=-O coff -F pe-i386)
	$(eval CC=i686-w64-mingw32-gcc)
	$(eval WINDRES=i686-w64-mingw32-windres)

dll64: build64
	$(eval LIBS=-shared -Wl,--kill-at,--out-implib,lib/x64/libIcBox.dll.a -lwinspool -m64)
	$(eval CFLAGS=-Iinclude -Isrc/system -Isrc/comm -Isrc/util -DBUILD_DLL -m64)

dll32: build32
	$(eval LIBS=-shared -Wl,--kill-at,--out-implib,lib/x86/libIcBox.dll.a -lwinspool -m32)
	$(eval CFLAGS=-Iinclude -Isrc/system -Isrc/comm -Isrc/util -DBUILD_DLL -m32)

exe64: build64
	$(eval LIBS=-lwinspool -m64)
	$(eval CFLAGS=-Iinclude -Isrc/system -Isrc/comm -Isrc/util -DDEBUGLIB -m64)

exe32: build32
	$(eval LIBS=-lwinspool -m32)
	$(eval CFLAGS=-Iinclude -Isrc/system -Isrc/comm -Isrc/util -DDEBUGLIB -m32)

clean:
	$(RM) $(OBJS)

purge: clean
	$(RM) build/x64/IcBox.dll build/x86/IcBox.dll build/x64/IcBoxTest.exe build/x86/IcBoxTest.exe

build/x64/IcBox.dll: dll64 $(OBJS)
	mkdir -p $(dir $@)
	mkdir -p lib/x64
	$(CC) -Wall -s -O2 -o $@ $(OBJS) $(LIBS)

build/x86/IcBox.dll: dll32 $(OBJS)
	mkdir -p $(dir $@)
	mkdir -p lib/x86
	$(CC) -Wall -s -O2 -o $@ $(OBJS) $(LIBS)

build/x64/IcBoxTest.exe: exe64 $(OBJS)
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -o $@ $(OBJS) $(LIBS)

build/x86/IcBoxTest.exe: exe32 $(OBJS)
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -o $@ $(OBJS) $(LIBS)

build/obj/IcBox.o: src/IcBox.c src/system/Thread.h src/system/Mutex.h src/system/Event.h src/comm/CommPort.h src/util/StringBuilder.h src/system/Mutex.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/win/main.o: src/win/main.c include/IcBox.h src/comm/CommPort.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/win/CommPort.o: src/win/CommPort.c src/comm/CommPort.h src/system/Thread.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/win/Event.o: src/win/Event.c src/system/Event.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/win/Mutex.o: src/win/Mutex.c src/system/Mutex.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/win/Thread.o: src/win/Thread.c src/system/Thread.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/util/StringBuilder.o: src/util/StringBuilder.c src/util/StringBuilder.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/util/Queue.o: src/util/Queue.c src/util/Queue.h
	mkdir -p $(dir $@)
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

build/obj/AppResource.res: res/AppResource.rc
	mkdir -p $(dir $@)
	$(WINDRES) -i res/AppResource.rc -J rc -o $@ $(RCFLAGS)
