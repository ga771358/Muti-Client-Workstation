#include <iostream>
#include <map>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <sys/types.h>
using namespace std;


class MyMap: public map<int,pair<int,int > >{
public:
	~MyMap(){
		map<int, pair<int,int> >::iterator it = begin();
	    if(it != end()){
	    	cout << "close " << it->second.first << endl;
	        cout << "close " << it->second.second << endl;
	        erase(it);
	        ++it;
	    }
	};
	
};

int main(){
	key_t key1 = 88,key2 = 2;
	int shmid1,shmid2;
	if((shmid1 = shmget((key_t)4567, sizeof(char[31][1000]), IPC_CREAT | 0666))<0) perror("shm");
	//if((shmid2 = shmget(key1, sizeof(char[2][2]), IPC_CREAT | 0666))<0) perror("shm");// fail if IPC_EXCL

	char* shmptr1 =  (char*)shmat(shmid1,0,0);
	//char* shmptr2 =  (char*)shmat(shmid2,0,0);
	//shmptr1[0] = 'a';
	shmdt(shmptr1);
	//shmdt(shmptr2);
 	if(shmctl((key_t)4567 , IPC_RMID , NULL)<0) perror("shmctl");

}