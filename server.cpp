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

int TcpListen(struct sockaddr_in* servaddr,socklen_t servlen,int port){
    int listenfd,reuse = 1;
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) cout << "socket error" << endl;
    
    bzero(servaddr, servlen);
    servaddr->sin_family = AF_INET;
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr->sin_port = htons(port);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    if(bind(listenfd, (struct sockaddr*) servaddr, servlen) < 0) cout << "bind error" << endl;
    if(listen(listenfd, MAXCONN) < 0) cout << "listen error" << endl;/*server listen port*/
    return listenfd;
}

int readline(int sockfd,char* ptr) {
    while(read(sockfd, ptr, 1) > 0) {
        if(*ptr !='\n') ++ptr;
        else {
            *ptr = 0;
            return 1;
        }
    }
    close(sockfd);
    return 0;
}

char* share_msg;
int* share_pid;
int connfd;

void read_msg(int signo){
    write(connfd, share_msg, strlen(share_msg));
    write(connfd, "\n", 1);
}

typedef struct data{
    char client_id[5];
    char name[30];
    char ip[25];
    char port[10];
} client_data;
client_data* share_data;

int main(int argc, char* argv[], char* envp[]){
    
    struct sockaddr_in cli_addr, serv_addr;
    int listenfd = TcpListen(&serv_addr, sizeof(serv_addr), atoi(argv[1])),msg_shmid,data_shmid,pid_shmid;
    key_t broadcast_msg_key = 3567, client_data_key = 4567, pid_key = 5567; //need system to decide the key
    socklen_t clilen = sizeof(cli_addr);

    if((msg_shmid = shmget(broadcast_msg_key, sizeof(char[1024]) , IPC_CREAT | 0660 ) ) < 0) perror("shm");
    if((data_shmid = shmget(client_data_key, sizeof(client_data[32]) , IPC_CREAT | 0660 ) ) < 0) perror("shm"); // care
    if((pid_shmid = shmget(pid_key, sizeof(int[32]) , IPC_CREAT | 0660 ) ) < 0) perror("shm");
    share_msg = (char*)shmat(msg_shmid, 0, 0);
    share_data = (client_data*)shmat(data_shmid, 0, 0);
    share_pid = (int*)shmat(pid_shmid, 0, 0);
    memset(share_msg, 0, sizeof(char[1024]));
    memset(share_pid, 0, sizeof(int[32]));
    memset(share_data, 0, sizeof(client_data[32]));
    share_pid[0] = getpid();
    
serv_next:
    connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &clilen);

    int pid = fork();
    if(pid == 0) {

        int step = 0, next, client_id;
        MyMap pipe_table;
        enum state{ PIPE , END , FILE };
        for(int i = 1; i < 32; i++)
            if(share_pid[i] == 0) {
                client_id = i;
                share_pid[i] = getpid();
                break;
            }

        strcpy(share_data[client_id].name, "(no name)");
        strcpy(share_data[client_id].ip, inet_ntoa(cli_addr.sin_addr));
        sprintf(share_data[client_id].client_id,"%d", client_id);
        sprintf(share_data[client_id].port,"%d", ntohs(cli_addr.sin_port));
        
        signal(SIGUSR1,read_msg);
        close(listenfd);
        write(connfd,"****************************************\n** Welcome to the information server. **\n****************************************\n",123);
        setenv("PATH","bin:.", 1);
        chdir("ras");
        
        while(true) {
        next_line:
            write(connfd,"% ",2);
            char buf[MAXLINE] = {0},response[MAXLINE] = {0};
            if(!readline(connfd, buf)) break;
         
            string input(buf), tok;
            int first = 1,isProgram = 1,count = 0, cnt = 0,found = 0,found_pos,test_fd;
            istringstream line(input),preparsing(input);
            while(preparsing >> tok) {

            	if(isProgram) {
            		if(!(tok == "exit" || tok == "setenv" || tok == "printenv" || tok == "yell")) {
            			string syspath(getenv("PATH"));
	                    found_pos = 0,found = 0,test_fd = -1;
	                    
	                    while((found_pos = syspath.find(":")) != string::npos) {
	                        if((test_fd = open((syspath.substr(0,found_pos)+"/"+tok).c_str(),O_RDONLY)) > 0) {
	                            found = 1;
	                            break;
	                        }
	                        syspath = syspath.substr(found_pos+1);
	                    }
	                    if(!found && found_pos == string::npos) {
	                        if((test_fd = open((syspath+"/"+tok).c_str(),O_RDONLY)) > 0) {
	                            found = 1;
	                        }
	                    }
	                    if(!found) break;
	                    close(test_fd);
            		}            
                }
                if(tok[0] == '|') isProgram = 1;
            	else isProgram = 0; 
            }
            if(found) count = -1;
            
            while(line >> tok){
                step++, cnt++;
                vector<string> Arglist;
                state s = END;
                if(tok == "yell") {
                    char* cmd = strtok(buf," ");
                    char* msg = strtok(NULL,"\r\n");
                    memset(share_msg, 0, sizeof(char[1024]));
                    sprintf(share_msg, "%s", msg);
                    for(int i = 1; i < 50; i++) {
                        if(share_pid[i] != 0) {
                            kill(share_pid[i],SIGUSR1);
                        }
                    }
                    break;
                }
                if(tok == "who") {
                    write(connfd,"<ID>\t<nickname>\t<IP/port>\t<indicate me>\n",40);
                    for(int i = 1; i < 32; i++) {
                        if(share_data[i].name[0] != 0) { ///
                            sprintf(buf,"%s\t%s\t%s\t%s",share_data[i].client_id,share_data[i].name,share_data[i].ip,share_data[i].port);
                            write(connfd, buf, strlen(buf));
                            if(i == client_id) write(connfd, "\t<-me\n",6);
                            else write(connfd, "\n", 1);
                        }
                    }
                    break;
                }
                if(tok == "tell") {
                    char* cmd = strtok(buf," ");
                    char* id = strtok(NULL," ");
                    char* msg = strtok(NULL,"\r\n");
                
                    memset(share_msg, 0, sizeof(char[1024]));
                    if(share_pid[atoi(id)] != 0) {
                        sprintf(share_msg, "*** %s told you ***: %s",share_data[client_id].name,msg);
                        kill(share_pid[atoi(id)],SIGUSR1);
                    }
                    else {
                        sprintf(share_msg, "*** Error: user %s does not exist yet. ***\n", id);
                        write(connfd, share_msg, strlen(share_msg));
                    }
                    break;
                }
                if(tok == "name") { 
                    char* cmd = strtok(buf," ");
                    char* Name = strtok(NULL,"\r\n");
                    for(int i = 1; i < 32; i++) {
                        if(share_data[i].name[0] != 0) {
                            if(strcmp(share_data[i].name, Name) == 0) {
                                sprintf(share_msg, "*** User '%s' already exists. ***\n", Name);
                                write(connfd, share_msg, strlen(share_msg));
                                goto next_line;
                            }
                        }
                    }
                    strcpy(share_data[client_id].name, Name);
                    sprintf(share_msg,"*** User from %s/%s is named '%s'. ***",share_data[client_id].ip,share_data[client_id].port,share_data[client_id].name);
                    for(int i = 1; i < 32; i++) {
                        if(share_data[i].name[0] != 0 && i != client_id) {
                            kill(share_pid[i], SIGUSR1);
                        }
                    }
                    break;
                }
              
                do {
                    if(tok[0] == '|') {
                        
                        if(tok.size() != 1) {
                            next = atoi(tok.substr(1,tok.size()-1).c_str());
                        }
                        else 
                        	next = 1;
                        
                        s = PIPE;
                        break;
                    }
                    if(tok[0] != '>') {
                        if(s == FILE) {
                            break;
                        }
                        Arglist.push_back(tok);
                    }
                    else s = FILE;
                }
                while(line >> tok);

                if(Arglist.empty()) continue;

                const char** arglist = new const char* [Arglist.size()+1];
                int i;
                for(i = 0 ; i < Arglist.size(); i++) {
                    arglist[i] = Arglist[i].c_str();
                }
                arglist[i] = NULL;
    
                if(Arglist[0] == "exit") {
                    share_pid[client_id] = 0; // not only share_pid...
                    memset(&share_data[client_id*1000],0,1000);
                    exit(0);
                }         

                if(Arglist[0] == "setenv") {
                    if(arglist[2] != NULL) setenv(arglist[1],arglist[2], 1);
                    break;
                }
                if(Arglist[0] != "printenv") {
                	if(cnt == count) {
                        string str("Unknown command: [" + Arglist[0] + "].\n");
                        memcpy(response, str.c_str(), str.size());
                        write(connfd, response, str.size());
                        if(!first) step--;
	                    break;
	                }
                }
                
                first = 0;
                int dont_create_pipe = 0;

                if(tok == "|" && count == cnt + 1) dont_create_pipe = 1;
                
                //read arg//
                int file_fd,data_fd[2];
                char data_buf[MAXLINE] = {0};

                if(s == PIPE){
                    if(!dont_create_pipe) {
                        if(pipe_table.find(step+next) == pipe_table.end()) {
                            pipe(data_fd);
                            pipe_table[step+next] = pair<int,int>(data_fd[0],data_fd[1]);
                        }
                        else {
                            data_fd[0] = pipe_table[step+next].first;
                            data_fd[1] = pipe_table[step+next].second;
                        }
                    }
                    
                }
                else if(s == FILE) {
                    file_fd = open(tok.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
                    if(file_fd < 0) break;
                }
                else pipe(data_fd);
               
                pid_t pid = fork();
                if(pid == 0) {
        
                    if(dont_create_pipe) pipe(data_fd);
                    
                    if(pipe_table.find(step) != pipe_table.end()) {
                        dup2(pipe_table[step].first, 0);
                        close(pipe_table[step].second);
                    }
                    
                    if(s == PIPE || s == END) dup2(data_fd[1],1);

                    else if(s == FILE) dup2(file_fd,1);
                    
                    dup2(connfd, 2);
                    
                    if(Arglist[0] == "printenv") {
                        
                        char* val = getenv(arglist[1]);
                        if(arglist[2] == NULL) {
                            string str(Arglist[1] + "=" + val + "\n");
                            memcpy(response, str.c_str(), str.size());
                            if(val != NULL) write(connfd, response, str.size());
                            else ;
                        }
                        else ;
                        exit(0);
                    }
                     
                    if(execvp(arglist[0], (char*const*)arglist) < 0) exit(errno);
                }
                else {
                    pipe_table.remove_pipe(step);
                    
                    if(s == END) close(data_fd[1]);
                    if(s == FILE) close(file_fd);
                    int n;
                    if(s == END) { //end of pipe
                        while((n = read(data_fd[0],data_buf,MAXLINE)) > 0) {
                            write(connfd, data_buf, n);
                            memset(data_buf, 0, sizeof(data_buf));
                        }
                    }
                    
                    if(s == END) close(data_fd[0]);
                    
                    int status = -1;
                    wait(&status);
                    if(WEXITSTATUS(status) > 0) break;
                    //cout << "The exit code of " << Arglist[0] << " is " << WEXITSTATUS(status) << endl;
                }
            }

        }
    }
    else {
        close(connfd);
        goto serv_next;
    }
    shmctl(pid_key , IPC_RMID , NULL);
    shmctl(client_data_key , IPC_RMID , NULL);
    shmctl(broadcast_msg_key , IPC_RMID , NULL);
}

