all: clean waterServer

waterServer.o:
	g++ -Wall waterServer.cpp -c -o waterServer.o

waterServer: waterServer.o
	g++ -lmodbus waterServer.o -o waterServer

clean:
	rm -f waterServer.o waterServer
