#include <iostream>
#include <sstream>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <errno.h>
#include <string.h>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <arpa/inet.h>

using namespace std;
#define MAXLINE 15000
#define MAXCONN 1000
#define MAXCLI 35
#define MAXNUM 10
#define MAXBUF 1025

class MyMap: public map<int,pair<int,int > > {
public:
	void remove_pipe(int pos) {
		map<int, pair<int,int> >::iterator it;
	    if((it = find(pos)) != end()){
	    	close(it->second.first);
	    	close(it->second.second);
	        erase(it);
	    }
	}
	~MyMap() {
		map<int, pair<int,int> >::iterator it = begin();
	    while(it != end()){
	    	close(it->second.first);
	    	close(it->second.second);
	        ++it;
	    }
	    erase(begin(),end());
	}
};