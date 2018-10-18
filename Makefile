TOPTARGETS := all clean
SUBDIRS := test
CFLAGS := -std=c++1y -I../WaterClient -Wall -Werror -O3 #-g -ggdb

all: clean waterServer test

waterServer.o:
	g++ $(CFLAGS) waterServer.cpp -c -o waterServer.o

guiProxy.o:
	g++ $(CFLAGS) `curl-config --cflags` guiProxy.cpp -c -o guiProxy.o

clientProxy.o:
	g++ $(CFLAGS) clientProxy.cpp -c -o clientProxy.o

modbusServer.o:
	g++ $(CFLAGS) modbusServer.cpp -c -o modbusServer.o

waterServer: waterServer.o guiProxy.o clientProxy.o modbusServer.o
	g++ `curl-config --libs` -llog4cxx -lmodbus -lboost_system -lboost_thread guiProxy.o clientProxy.o modbusServer.o waterServer.o -o waterServer

test:
	$(MAKE) -C test

clean:
	rm -f *.o *.so *.a waterServer
	$(MAKE) -C test clean

.PHONY: test
