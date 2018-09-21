all: clean waterServer

waterServer.o:
	g++ -std=c++11 -I../WaterClient -Wall waterServer.cpp -c -o waterServer.o

waterServer: waterServer.o
	g++ -lmodbus waterServer.o -o waterServer

clean:
	rm -f waterServer.o waterServer
