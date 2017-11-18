all: server 

server: select.cpp server.cpp config.h
	g++ server.cpp -o ../server
	g++ select.cpp -o ../select

