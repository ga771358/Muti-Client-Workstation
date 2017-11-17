all: server 

server: server.cpp config.h
	g++ -g server.cpp -o ../server

