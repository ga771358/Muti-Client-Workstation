#include "config.h"

int TcpListen(struct sockaddr_in* servaddr,socklen_t servlen,int port){
    int listenfd,reuse = 1;
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) cout << "socket error" << endl;
    
    bzero(servaddr, servlen);
    servaddr->sin_family = AF_INET;
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr->sin_port = htons(port);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    if(bind(listenfd, (struct sockaddr*) servaddr, servlen) < 0) cout << "bind error" << endl;
    if(listen(listenfd, MAXCONN) < 0) cout << "listen error" << endl; /*server listen port*/
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

typedef struct data {
    int pid;
    char name[30];
    char ip[20];
    int port;
    int shmid;
    char* mybuffer;
    int from[MAXNUM];
} client_data;

client_data* share_data;
char* share_msg;
int connfd;

void read_msg(int signo){
    write(connfd, share_msg, strlen(share_msg));
}

void removezombie(int signo){
    union wait status;
    while (wait3(&status, WNOHANG,(struct rusage *)0)>= 0) ;
}

void broadcast(void) {
    for(int id = 1; id < MAXCLI; id++) 
        if(share_data[id].pid) kill(share_data[id].pid, SIGUSR1);
}

int main(int argc, char* argv[], char* envp[]){
    
    struct sockaddr_in cli_addr, serv_addr;
    if(argv[1] == NULL) return 0;
    int listenfd = TcpListen(&serv_addr, sizeof(serv_addr), atoi(argv[1])), msg_shmid, data_shmid;
    socklen_t clilen = sizeof(cli_addr);

    if((msg_shmid = shmget(IPC_PRIVATE, MAXBUF , IPC_CREAT | 0600 ) ) < 0) perror("shm");
    if((data_shmid = shmget(IPC_PRIVATE, sizeof(client_data[MAXCLI]) , IPC_CREAT | 0600 ) ) < 0) perror("shm"); // care
    share_msg = (char*) shmat(msg_shmid, 0, 0);
    share_data = (client_data*) shmat(data_shmid, 0, 0);

    memset(share_msg, 0, MAXBUF);
    memset(share_data, 0, sizeof(client_data[MAXCLI]));
    for(int id = 1; id < MAXCLI; id++) { ///
        share_data[id].shmid = shmget(IPC_PRIVATE, MAXBUF*MAXNUM, IPC_CREAT | 0600 );
        share_data[id].mybuffer = (char*) shmat(share_data[id].shmid, 0, 0);
        memset(share_data[id].mybuffer, 0, MAXBUF*MAXNUM);
    }
    share_data[0].pid = getpid();
    signal(SIGCHLD, removezombie);

serv_next:
    connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &clilen);
    write(connfd,"****************************************\n** Welcome to the information server. **\n****************************************\n",123);
    setenv("PATH","bin:.", 1);
    chdir("ras");

    int pid = fork();
    if(pid == 0) {

        int step = 0, next, client_id;
        MyMap pipe_table;
        enum state{ PIPE , END , FILE ,PIPE_OTHER };
        for(int id = 1; id < MAXCLI; id++)
            if(share_data[id].pid == 0) {
                client_id = id;
                share_data[id].pid = getpid();
                memset(share_data[id].from, 0, sizeof(int[MAXNUM]));
                strcpy(share_data[id].name, "(no name)");
                strcpy(share_data[id].ip, inet_ntoa(cli_addr.sin_addr));
                share_data[id].port = ntohs(cli_addr.sin_port); 
                
                signal(SIGUSR1,read_msg);
                close(listenfd);
                break;
            }

        sprintf(share_msg,"*** User '%s' entered from %s/%d. ***\n",share_data[client_id].name,"CGILAB",511);
        broadcast();
next_line:
            write(connfd,"% ",2);
            char buf[MAXLINE] = {0},response[MAXLINE] = {0};
            if(!readline(connfd, buf)) goto next_line;
         
            string input(buf), tok;
            int first = 1,isProgram = 1,count = 0, cnt = 0,found = 1,found_pos;
            istringstream line(input),preparsing(input);
            while(preparsing >> tok) {

            	if(isProgram) {
                    count++;
            		if(!(tok == "exit" || tok == "setenv" || tok == "printenv" || tok == "yell" || tok == "name" || tok == "tell" || tok == "who")) {
            			string syspath(getenv("PATH"));
                        struct stat sb;
	                    found_pos = 0,found = 0;  
	                    
	                    while((found_pos = syspath.find(":")) != string::npos) {

	                        if(stat((syspath.substr(0,found_pos)+"/"+tok).c_str(), &sb) == 0 && sb.st_mode & S_IXUSR) {
	                            found = 1;
	                            break;
	                        }
	                        syspath = syspath.substr(found_pos+1);
	                    }
	                    if(!found && found_pos == string::npos) {
	                        if(stat((syspath.substr(0,found_pos)+"/"+tok).c_str(), &sb) == 0 && sb.st_mode & S_IXUSR) {
	                            found = 1;
	                        }
	                    }
	                    if(!found) break;
	                  
            		}
                    else found = 1;            
                }
                if(tok[0] == '|') isProgram = 1;
            	else isProgram = 0; 
            }
            if(found) count = -1;
            
            while(line >> tok) {
                step++, cnt++;
                vector<string > Arglist;
                vector<int > pipe_from, pipe_pos;
                state s = END;
                int file_fd = 0, target_id;

                if(tok == "exit") {
                    share_data[client_id].pid = 0; // not only share_pid
                    memset(share_data[client_id].from, 0 , sizeof(int[MAXNUM]));
                    shmctl(share_data[client_id].shmid , IPC_RMID , NULL);
                    sprintf(share_msg,"*** User '%s' left. ***\n",share_data[client_id].name);
                    broadcast();
                    write(connfd, share_msg, strlen(share_msg));
                    exit(0);
                }
                if(tok == "yell") {
                    char* cmd = strtok(buf," "), *msg = strtok(NULL,"\r\n");
                    sprintf(share_msg, "*** %s yelled ***: %s\n", share_data[client_id].name, msg);
                    broadcast();
                    goto next_line;
                }
                if(tok == "who") {
                    write(connfd,"<ID>\t<nickname>\t<IP/port>\t<indicate me>\n",40);
                    for(int id = 1; id < MAXCLI; id++) {
                        if(share_data[id].pid) {
                            if(id == client_id) sprintf(buf,"%d\t%s\t%s/%d\t<-me\n",id,share_data[id].name,"CGILAB",511);
                            else sprintf(buf,"%d\t%s\t%s/%d\n",id,share_data[id].name,"CGILAB",511);
                            write(connfd, buf, strlen(buf));
                        }
                    }
                    goto next_line;
                }
                if(tok == "tell") {
                    char* cmd = strtok(buf," "),*number = strtok(NULL," "),*msg = strtok(NULL,"\r\n");
                    int id = atoi(number);
                    if(share_data[id].pid) {
                        sprintf(share_msg, "*** %s told you ***: %s\n", share_data[client_id].name,msg);
                        kill(share_data[id].pid,SIGUSR1);
                    }
                    else {
                        sprintf(share_msg, "*** Error: user #%d does not exist yet. ***\n", id);
                        kill(share_data[client_id].pid,SIGUSR1);
                    }
                    goto next_line;
                }
                if(tok == "name") { 
                    char* cmd = strtok(buf," "),*NAME = strtok(NULL,"\r\n");
             
                    for(int id = 1; id < MAXCLI; id++) {
                        if(share_data[id].pid) {
                            if(strcmp(share_data[id].name, NAME) == 0) {
                                sprintf(share_msg, "*** User '%s' already exists. ***\n", NAME);
                                kill(share_data[client_id].pid,SIGUSR1);
                                goto next_line;
                            }
                        }
                    }
                    strcpy(share_data[client_id].name, NAME);
                    sprintf(share_msg,"*** User from %s/%d is named '%s'. ***\n","CGILAB",511,share_data[client_id].name);
                    broadcast();
                    goto next_line;
                }
              
                do {
                    if(tok[0] == '|') {
                        
                        if(tok.size() != 1) 
                            next = atoi(tok.substr(1).c_str());
                        else 
                        	next = 1;
                        
                        s = PIPE;
                        break;
                    }
                    if(tok[0] != '>' && tok[0] != '<') { 
                        if(s == FILE) file_fd = open(tok.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
                        else Arglist.push_back(tok);
                    }
                    else {
                        if(tok == ">") s = FILE;
                        else if(tok[0] == '<') {
                            int read_from = atoi(tok.substr(1).c_str()), msg_pos;
                            
                            for(msg_pos = 0; msg_pos != MAXNUM; msg_pos++) {
                                if(share_data[client_id].from[msg_pos] == read_from) {
                                    pipe_pos.push_back(msg_pos);
                                    break;
                                }
                            }
                            if(msg_pos == MAXNUM) {
                                sprintf(share_msg, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", read_from, client_id);
                                kill(share_data[client_id].pid, SIGUSR1);
                                goto next_line; // cat <2
                            }
                            else {
                                char* cmd = strtok(buf, "\r\n");
                                sprintf(share_msg, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n",share_data[client_id].name, client_id, share_data[read_from].name, read_from, cmd);
                                broadcast();
                            }
                
                        }
                        else  {
                            s = PIPE_OTHER;
                            target_id = atoi(tok.substr(1).c_str());
                        }
                    }
                }while(line >> tok);

                if(Arglist.empty()) continue;

                const char** arglist = new const char* [Arglist.size()+1];
                int i;
                for(i = 0 ; i < Arglist.size(); i++) {
                    arglist[i] = Arglist[i].c_str();
                }
                arglist[i] = NULL;      

                if(Arglist[0] == "setenv") {
                    if(arglist[2] != NULL) setenv(arglist[1],arglist[2], 1);
                    goto next_line;
                }
                if(Arglist[0] != "printenv") {
                    cout << cnt << " " << count << endl;
                	if(cnt == count) {
                        string str("Unknown command: [" + Arglist[0] + "].\n");
                        strcpy(response, str.c_str());
                        write(connfd, response, strlen(response));
                        if(!first) step--;
	                    goto next_line;
	                }
                }
                
                first = 0;
                int dont_create_pipe = 0;

                if(tok == "|" && count == cnt + 1) dont_create_pipe = 1;
                
                //read arg//
                int data_fd[2];
                char data_buf[MAXLINE] = {0};

                while(!pipe_pos.empty()) {
                    if(pipe_table.find(step) == pipe_table.end()) {
                        pipe(data_fd);
                        pipe_table[step] = pair<int,int>(data_fd[0],data_fd[1]);
                    } 
                    write(pipe_table[step].second, share_data[client_id].mybuffer+pipe_pos.back()*MAXBUF, strlen(share_data[client_id].mybuffer+pipe_pos.back()*MAXBUF));
                    share_data[client_id].from[pipe_pos.back()] = 0;
                    pipe_pos.pop_back();
                }
                if(s == PIPE) {
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
                else pipe(data_fd);
               
                pid_t pid = fork();
                if(pid == 0) {
        
                    if(dont_create_pipe) pipe(data_fd);
                    
                    if(pipe_table.find(step) != pipe_table.end()) {
                        dup2(pipe_table[step].first, 0);
                        close(pipe_table[step].second);
                    }
                    
                    if(file_fd != 0) dup2(file_fd,1);
                    else dup2(data_fd[1],1);

                    if(s == PIPE_OTHER) dup2(data_fd[1],2);
                    else dup2(connfd, 2);
                    
                    if(Arglist[0] == "printenv") {
                        
                        char* val = getenv(arglist[1]);
                        if(arglist[2] == NULL) {
                            string str(Arglist[1] + "=" + val + "\n");
                            strcpy(response, str.c_str());
                            if(val != NULL) write(connfd, response, strlen(response));
                        }
                        exit(0);
                    }
                     
                    if(execvp(arglist[0], (char*const*)arglist) < 0) exit(errno);
                }
                else {
                    pipe_table.remove_pipe(step);

                    if(s == END || s == PIPE_OTHER) close(data_fd[1]);
                    if(s == FILE) close(file_fd);
                    int n, msg_pos;
                    if(s == PIPE_OTHER) {
                        if(share_data[target_id].pid) {   
                            for(msg_pos = 0; msg_pos != MAXNUM; msg_pos++) {
                                if (client_id == share_data[target_id].from[msg_pos]) {
                                    sprintf(share_msg, "*** Error: the pipe #%d->#%d already exists. ***\n",client_id,target_id);
                                    kill(share_data[client_id].pid, SIGUSR1);
                                    goto next_line;
                                }
                            }
                        }
                        else {
                            sprintf(share_msg, "*** Error: user #%d does not exist yet. ***\n",target_id);
                            kill(share_data[client_id].pid, SIGUSR1);
                            goto next_line;
                        }
                    }
                    
                    if(s == END || s == PIPE_OTHER) { //end of pipe
                        memset(data_buf, 0, sizeof(data_buf));
                        n = read(data_fd[0], data_buf, MAXBUF);
                        if(s == END) write(connfd, data_buf, n);
                        else {
                            for(msg_pos = 0; msg_pos != MAXNUM; msg_pos++) 
                                if(share_data[target_id].from[msg_pos] == 0) break;
                            memset(share_data[target_id].mybuffer+msg_pos*MAXBUF, 0, MAXBUF);
                            memcpy(share_data[target_id].mybuffer+msg_pos*MAXBUF, data_buf, n);      
                        }
                    }
                    
                    if(s == END || s == PIPE_OTHER) close(data_fd[0]);
                    if(s == PIPE_OTHER) {
                        share_data[target_id].from[msg_pos] = client_id;
                        char* cmd = strtok(buf, "\r\n");
                        sprintf(share_msg, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n",share_data[client_id].name,client_id,cmd,share_data[target_id].name,target_id);
                        broadcast();
                    }
                    
                    int status = -1;
                    wait(&status);
                    cout << "The exit code of " << Arglist[0] << " is " << WEXITSTATUS(status) << endl;
                }
            }
            goto next_line;
    }
    else {
        close(connfd);
        goto serv_next;
    }
    shmctl(msg_shmid , IPC_RMID , NULL);
    shmctl(data_shmid , IPC_RMID , NULL);
}

