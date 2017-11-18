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
    MyMap* pipe_table_ptr;
    int connfd;
    int step;
    char name[30];
    char ip[20];
    int port;
    char path[20];
    char mybuffer[MAXBUF*MAXNUM];
    char private_buffer[MAXBUF];
    int from[MAXNUM];
} client_data;

client_data share_data[MAXCLI];
char share_msg[MAXBUF];

void broadcast(int client_id) {
    for(int id = 1; id < MAXCLI; id++) 
        if(share_data[id].connfd) {
            write(share_data[id].connfd, share_msg, strlen(share_msg));
        }
}

int main(int argc, char* argv[], char* envp[]){
    
    struct sockaddr_in cli_addr, serv_addr;
    fd_set result,interest;
    if(argv[1] == NULL) return 0;
    int listenfd = TcpListen(&serv_addr, sizeof(serv_addr), atoi(argv[1])),nready,maxfd;
    socklen_t clilen = sizeof(cli_addr);

    memset(share_msg, 0, MAXBUF);
    memset(share_data, 0, sizeof(client_data[MAXCLI]));

    for(int id = 1; id < MAXCLI; id++) {
        memset(share_data[id].mybuffer, 0, MAXBUF*MAXNUM);
        memset(share_data[id].private_buffer, 0, MAXBUF);
        share_data[id].connfd = 0;
    }
    //signal(SIGINT, dealloc);
    FD_ZERO(&interest);
    FD_SET(listenfd,&interest);
    maxfd = listenfd;


while(true){
    result = interest;
    if((nready = select(maxfd+1, &result, NULL, NULL, NULL)) < 0) exit(0);
    if(FD_ISSET(listenfd,&result)) {
        int connfd = accept(listenfd, (struct sockaddr *) &cli_addr, &clilen);

        write(connfd,"****************************************\n** Welcome to the information server. **\n****************************************\n",123);
        
        setenv("PATH","bin:.", 1);
        chdir("ras");
        int id;
        for(id = 1; id < MAXCLI; id++) {
            if(!share_data[id].connfd) {
                share_data[id].connfd = connfd;
                share_data[id].step = 0;
                share_data[id].pipe_table_ptr = new MyMap();
                memset(share_data[id].from, 0, sizeof(int[MAXNUM]));
                strcpy(share_data[id].name, "(no name)");
                strcpy(share_data[id].ip, inet_ntoa(cli_addr.sin_addr));
                share_data[id].port = ntohs(cli_addr.sin_port); 
                strcpy(share_data[id].path, "bin:.");
                FD_SET(connfd, &interest);
                if(connfd > maxfd) maxfd = connfd;
                break;
            }
        }
        
        //MyMap pipe_table;
        sprintf(share_msg,"*** User '%s' entered from %s/%d. ***\n",share_data[id].name,"CGILAB",511);
        broadcast(id);
        write(connfd,"% ",2);
    }
    enum state{ PIPE , END , FILE ,PIPE_OTHER };

    for(int id = 1; id < MAXCLI; id++) {

        if(share_data[id].connfd && FD_ISSET(share_data[id].connfd,&result)) {
            cout << "read" << endl;
            int client_id = id;
            MyMap& pipe_table = (*share_data[client_id].pipe_table_ptr);
            setenv("PATH",share_data[client_id].path, 1);
            //MyMap pipe_table(*(share_data[id].pipe_table_ptr));
            char buf[MAXLINE] = {0},response[MAXLINE] = {0};
            if(!readline(share_data[client_id].connfd, buf)) break;
         
            string input(buf), tok;
            int first = 1, isProgram = 1, count = 0, cnt = 0, found = 0, found_pos;
            istringstream line(input),preparsing(input);
            while(preparsing >> tok) {

                if(isProgram) {
                    count++;
                    if(!(tok == "exit" || tok == "setenv" || tok == "printenv" || tok == "yell" || tok == "name" || tok == "tell" || tok == "who")) {
                        string syspath(share_data[client_id].path);
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
                share_data[client_id].step++, cnt++;
                vector<string > Arglist;
                vector<int >  pipe_pos;
                state s = END;
                int step = share_data[client_id].step, next, file_fd = 0, target_id;

                if(tok == "exit") {
                    pipe_table.~MyMap();
                    memset(share_data[client_id].from, 0 , sizeof(int[MAXNUM]));
                    sprintf(share_msg,"*** User '%s' left. ***\n",share_data[client_id].name);
                    broadcast(client_id);
                    close(share_data[client_id].connfd);
                    FD_CLR(share_data[client_id].connfd,&interest);
                    share_data[client_id].connfd = 0; // not only share_pid
                    break;
                }
                if(tok == "yell") {
                    char* cmd = strtok(buf," "), *msg = strtok(NULL,"\r\n");
                    sprintf(share_msg, "*** %s yelled ***: %s\n", share_data[client_id].name, msg);
                    broadcast(client_id);
                    break;
                }
                if(tok == "who") {
                    write(share_data[client_id].connfd,"<ID>\t<nickname>\t<IP/port>\t<indicate me>\n",40);
                    for(int id = 1; id < MAXCLI; id++) {
                        if(share_data[id].connfd) {
                            if(id == client_id) sprintf(response,"%d\t%s\t%s/%d\t<-me\n",id,share_data[id].name,"CGILAB",511);
                            else sprintf(response,"%d\t%s\t%s/%d\n",id,share_data[id].name,"CGILAB",511);
                            write(share_data[client_id].connfd, response, strlen(response));
                        }
                    }
                    break;
                }
                if(tok == "tell") {
                    char* cmd = strtok(buf," "),*number = strtok(NULL," "),*msg = strtok(NULL,"\r\n");
                    int id = atoi(number);
                    if(share_data[id].connfd) {
                        sprintf(share_data[id].private_buffer, "*** %s told you ***: %s\n", share_data[client_id].name,msg);
                        write(share_data[id].connfd, share_data[id].private_buffer, strlen(share_data[id].private_buffer));
                    }
                    else {
                        sprintf(response, "*** Error: user #%d does not exist yet. ***\n", id);
                        write(share_data[client_id].connfd, response, strlen(response));
                    }
                    break;
                }
                if(tok == "name") { 
                    char* cmd = strtok(buf," "),*NAME = strtok(NULL,"\r\n");
                    int id;
                    for(id = 1; id < MAXCLI; id++) {
                        if(share_data[id].connfd) {
                            if(strcmp(share_data[id].name, NAME) == 0 && strcmp(share_data[id].name, "(no name)") != 0 && id != client_id) {
                                sprintf(response, "*** User '%s' already exists. ***\n", NAME);
                                write(share_data[client_id].connfd, response, strlen(response));
                                break;
                            }
                        }
                    }
                    if(id < MAXCLI) break;
                    strcpy(share_data[client_id].name, NAME);
                    sprintf(share_msg,"*** User from %s/%d is named '%s'. ***\n","CGILAB",511,share_data[client_id].name);
                    broadcast(client_id);
                    break;
                }
                int error = 0;
                do {
                    if(tok[0] == '|') {
                        
                        if(tok.size() != 1) next = atoi(tok.substr(1).c_str());
                        else next = 1;
                        
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
                                sprintf(response, "*** Error: the pipe #%d->#%d does not exist yet. ***\n", read_from, client_id);
                                write(share_data[client_id].connfd, response, strlen(response));
                                error = 1;
                                break;
                            }
                            else {
                                char* cmd = strtok(buf, "\r\n");
                                sprintf(share_msg, "*** %s (#%d) just received from %s (#%d) by '%s' ***\n",share_data[client_id].name, client_id, share_data[read_from].name, read_from, cmd);
                                broadcast(client_id);
                            }
                
                        }
                        else  {
                            s = PIPE_OTHER;
                            target_id = atoi(tok.substr(1).c_str());
                        }
                    }
                } while(line >> tok);
                if(error) break;

                if(Arglist.empty()) continue;

                const char** arglist = new const char* [Arglist.size()+1];
                int i;
                for(i = 0 ; i < Arglist.size(); i++) {
                    arglist[i] = Arglist[i].c_str();
                }
                arglist[i] = NULL;      

                if(Arglist[0] == "setenv") {
                    if(arglist[2] != NULL) strcpy(share_data[id].path, arglist[2]);
                    break;
                }
                if(Arglist[0] != "printenv") {
                    cout << cnt << " " << count << endl;
                    if(cnt == count) {
                        string str("Unknown command: [" + Arglist[0] + "].\n");
                        strcpy(response, str.c_str());
                        write(share_data[client_id].connfd, response, strlen(response));
                        if(!first) share_data[client_id].step--;
                        break;
                    }
                }
                
                first = 0;
                int dont_create_pipe = 0;
                if(tok == "|" && count == cnt + 1) dont_create_pipe = 1;
                
                int data_fd[2],err_fd[2];
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
                else if(s != FILE) pipe(data_fd);
                pipe(err_fd);
                
                pid_t pid = fork();
                if(pid == 0) {
        
                    if(dont_create_pipe) pipe(data_fd);
                    
                    if(pipe_table.find(step) != pipe_table.end()) {
                        dup2(pipe_table[step].first, 0);
                        close(pipe_table[step].second);
                    }
                    
                    if(file_fd != 0) dup2(file_fd,1);
                    else dup2(data_fd[1],1);

                    if(s == PIPE_OTHER) dup2(err_fd[1],2);
                    else dup2(share_data[id].connfd, 2);
                    
                    if(Arglist[0] == "printenv") {
                        
                        char* val = getenv(arglist[1]);
                        if(arglist[2] == NULL) {
                            string str(Arglist[1] + "=" + val + "\n");
                            strcpy(response, str.c_str());
                            if(val != NULL) write(share_data[id].connfd, response, strlen(response));
                        }
                        exit(0);
                    }
                     
                    if(execvp(arglist[0], (char*const*)arglist) < 0) exit(errno);
                }
                else {
                    pipe_table.remove_pipe(step);
                    close(err_fd[1]);

                    if(s == END || s == PIPE_OTHER) close(data_fd[1]);
                    if(s == FILE) close(file_fd);
                    int n, msg_pos,error = 0;
                    if(s == PIPE_OTHER) {
                        if(share_data[target_id].connfd) {   
                            for(msg_pos = 0; msg_pos != MAXNUM; msg_pos++) {
                                if (client_id == share_data[target_id].from[msg_pos]) {
                                    sprintf(response, "*** Error: the pipe #%d->#%d already exists. ***\n",client_id,target_id);
                                    write(share_data[client_id].connfd, response, strlen(response));
                                    error = 1;
                                    break;
                                }
                            }
                        }
                        else {
                            sprintf(response, "*** Error: user #%d does not exist yet. ***\n",target_id);
                            write(share_data[client_id].connfd, response, strlen(response));
                            error = 1;
                            break;
                        }
                    }
                    
                    if(s == END || s == PIPE_OTHER) { //end of pipe
                        memset(data_buf, 0, sizeof(data_buf));
                        n = read(data_fd[0], data_buf, MAXBUF);
                        if(s == END) write(share_data[id].connfd, data_buf, n);
                        else {
                            for(msg_pos = 0; msg_pos != MAXNUM; msg_pos++) 
                                if(share_data[target_id].from[msg_pos] == 0) break;
                            memset(share_data[target_id].mybuffer+msg_pos*MAXBUF, 0, MAXBUF);
                            memset(response, 0, MAXBUF);
                            read(err_fd[0], response , MAXBUF);
                            strcat(response, data_buf);
                            memcpy(share_data[target_id].mybuffer+msg_pos*MAXBUF, response, strlen(response));      
                        }
                    }
                    
                    close(err_fd[0]);
                    if(s == END || s == PIPE_OTHER) close(data_fd[0]);
                    if(s == PIPE_OTHER) {
                        if(error) break;
                        share_data[target_id].from[msg_pos] = client_id;
                        char* cmd = strtok(buf, "\r\n");
                        sprintf(share_msg, "*** %s (#%d) just piped '%s' to %s (#%d) ***\n",share_data[client_id].name,client_id,cmd,share_data[target_id].name,target_id);
                        broadcast(client_id);
                    }
                    
                    int status = -1;
                    wait(&status);
                    //cout << "The exit code of " << Arglist[0] << " is " << WEXITSTATUS(status) << endl;
                }
            }
            if(tok == "exit") break;
            else write(share_data[client_id].connfd,"% ",2);
        }
        }
    }
    
}
    

