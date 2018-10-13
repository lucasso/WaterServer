all: clean guiProxyTest

guiProxyTest.o:
	g++ -std=c++1y -I../../WaterClient -Wall -Werror guiProxyTest.cpp -c -o guiProxyTest.o

guiProxyTest: guiProxyTest.o
	g++ `curl-config --libs` -llog4cxx -lmodbus -lboost_system -lboost_thread ../guiProxy.o guiProxyTest.o -o guiProxyTest

clean:
	rm -f *.o guiProxyTest

