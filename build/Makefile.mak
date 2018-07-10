CC     = gcc
WINDRES= windres
RM     = rm -f
OBJS   = ../src/IcBox.o \
         ../src/win/main.o \
         ../src/win/CommPort.o \
         ../src/win/Event.o \
         ../src/win/Mutex.o \
         ../src/win/Thread.o \
         ../src/util/StringBuilder.o \
         ../src/util/Queue.o \
         AppResource.res

LIBS   = -lwinspool -m64
CFLAGS = -I..\include -I..\src\system -I..\src\comm -I..\src\util -DDEBUGLIB -m64 -fno-diagnostics-show-option

.PHONY: all

all: ../bin/x64/IcBoxTest.exe

clean:
	$(RM) $(OBJS) ../bin/x64/IcBoxTest.exe

clear:
	$(RM) $(OBJS)

../bin/x64/IcBoxTest.exe: $(OBJS)
	$(CC) -Wall -s -O2 -o $@ $(OBJS) $(LIBS)

../src/IcBox.o: ../src/IcBox.c ../src/system/Thread.h ../src/system/Mutex.h ../src/system/Event.h ../src/comm/CommPort.h ../src/util/StringBuilder.h ../src/util/Queue.h ../src/system/Mutex.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

../src/win/main.o: ../src/win/main.c ../include/IcBox.h ../src/system/Thread.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

../src/win/CommPort.o: ../src/win/CommPort.c ../src/comm/CommPort.h ../src/system/Thread.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

../src/win/Event.o: ../src/win/Event.c ../src/system/Event.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

../src/win/Mutex.o: ../src/win/Mutex.c ../src/system/Mutex.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

../src/win/Thread.o: ../src/win/Thread.c ../src/system/Thread.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

../src/util/StringBuilder.o: ../src/util/StringBuilder.c ../src/util/StringBuilder.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

../src/util/Queue.o: ../src/util/Queue.c ../src/util/Queue.h
	$(CC) -Wall -s -O2 -c $< -o $@ $(CFLAGS)

AppResource.res: AppResource.rc
	$(WINDRES) -i AppResource.rc -J rc -o AppResource.res -O coff

