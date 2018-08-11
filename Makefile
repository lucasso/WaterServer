all: clean waterServer

waterServer.o:
	g++ -Wall waterServer.cpp -c -o waterServer.o

waterServer: waterServer.o
	g++ -L/usr/local/lib -lmodbus waterServer.o -o waterServer

clean:
	rm -f waterServer.o waterServer