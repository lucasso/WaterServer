all: clean waterServer

waterServer.o:
	g++ -std=c++1y -I../WaterClient -Wall -Werror waterServer.cpp -c -o waterServer.o

guiProxy.o:
	g++ -std=c++1y -I../WaterClient `curl-config --cflags` -Wall -Werror guiProxy.cpp -c -o guiProxy.o

clientProxy.o:
	g++ -std=c++1y -I../WaterClient -Wall -Werror clientProxy.cpp -c -o clientProxy.o

modbusServer.o:
	g++ -std=c++1y -I../WaterClient -Wall -Werror modbusServer.cpp -c -o modbusServer.o


waterServer: waterServer.o guiProxy.o clientProxy.o modbusServer.o
	g++ `curl-config --libs` -llog4cxx -lmodbus -lboost_system -lboost_thread waterServer.o guiProxy.o clientProxy.o modbusServer.o -o waterServer

clean:
	rm -f *.o waterServer
