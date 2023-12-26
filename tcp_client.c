#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>


#define MAX_BUFFER 1024

/*
    ::compile:: 
    gcc -(l)pthread -o client tcp_client.c
    
    ::ssh tunnel::
    local_machine:$ ssh -L 7171:localhost:7272 remote_user@remote_addr
    remote_machine:$ ./server 7272
    local_machine:$ ./client 127.0.0.1 7171
*/

typedef struct {
    char send_msg[MAX_BUFFER];
    char buffer[MAX_BUFFER];
    int sock;
} thr_args;


bool recv__file(int sockfd)
{
    FILE *fp;
    char buff[MAX_BUFFER], recv_file[MAX_BUFFER], chr_to_str[1];
    char *ext, *fulllfpath, *ptrstrtolend, *strfilesize; // ext & ptrstrtolend don't use malloc??
    char *lfpath = "./recvfile";
    ssize_t bytes, send_bytes, filesize, overall_bytes = 0;
    int i, cnt = 0;
    if(recv(sockfd, recv_file, MAX_BUFFER -1, 0) <= 0){
        perror("recv() metadata error");
        return false;
    }
    ext = strrchr(recv_file, '.');
    strfilesize = malloc((strlen(recv_file) +1) - (strlen(ext) +1));
    if(strfilesize == NULL){
        perror("malloc() strfilesize error");
        return false;
    }
    // strcat operates on strings => chr_to_str as temp var
    for(i=0; i<strlen(recv_file); i++)
    {
        chr_to_str[0] = recv_file[i];
        if(isdigit(recv_file[i])){
            strcat(strfilesize, chr_to_str);
        }
    }
    filesize = strtol(strfilesize, &ptrstrtolend, 10); // convert to long
    printf("Extension: %s\nFile size: %ld\n", ext, filesize);
    if(ext == NULL){
        printf("No file extension.\n");
        fulllfpath = malloc(strlen(lfpath) +1);
        strncpy(fulllfpath, lfpath, strlen(lfpath) +1);
    }
    else {
        fulllfpath = malloc(strlen(lfpath)+1 + strlen(ext) +1);
        strncpy(fulllfpath, lfpath, strlen(lfpath) +1);
        strcat(fulllfpath, ext);
    }
    if(fulllfpath == NULL){
        perror("malloc() fullpath error");
        return false;
    }
    if(fulllfpath[strlen(fulllfpath) -1] == '\n'){
        fulllfpath[strlen(fulllfpath) -1] = '\0';
    }
    if((fp = fopen(fulllfpath, "wb")) == NULL){
        fprintf(stderr, "error fopen() wb\n");
        return false;
    }
    printf("Path: %s\n", realpath(fulllfpath, NULL));
    while(1) {
        cnt++;
        bytes = 0;
        memset(buff, 0, sizeof(buff));
        if((bytes = recv(sockfd, buff, MAX_BUFFER -1, 0)) > 0)
        {
            if(buff[bytes -1] == '\n'){
                buff[bytes -1] = '\0';
            }
            overall_bytes += bytes;
            printf("Iteration [%d] - Bytes [%ld] - Overall Bytes [%ld]\n", cnt, bytes, overall_bytes);
            // close after spqcial sequence recieved => EOF
            if(strstr(buff, "--sf--end") != NULL){
                printf("End of transmission..\n");
                break;
            }
            if(fwrite(buff, sizeof(char), bytes, fp) != bytes){
                perror("fwrite() error");
                break;
            }
        }
        if(bytes == 0){
            printf("Server disconnected.\n");
            break;
        }
        if(bytes == -1){
            perror("recv() error");
            return false;
        }
    }
    printf("Recieved: [%ld]b\n", overall_bytes);
    free(fulllfpath);
    free(strfilesize);
    fclose(fp);
    return true;
}

void *send_handler(void *args) { // passing multiple args by pointer to struct 
    thr_args *send_args = args;
    ssize_t bytes = 0;
    printf("Send handler start..\n");
    while(1) {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        memset(send_args->send_msg, '\0', sizeof(send_args->send_msg));
        fgets(send_args->send_msg, sizeof(send_args->send_msg), stdin);
        if(send_args->send_msg[strlen(send_args->send_msg) -1] == '\n') {
            send_args->send_msg[strlen(send_args->send_msg) -1] = '\0';
        }
        if(send_args->send_msg[0] == '\0') {
            printf("Enter msg..\n");
        }
        else if(!strncmp(send_args->send_msg,"x",1)) {
            printf("[!] Exit.\n");
            exit(0);
        }
        else {
            // check if client is connected
            if(recv(send_args->sock,NULL,1, MSG_PEEK | MSG_DONTWAIT) == 0){
                printf("Client disconnected.\n");
                break;
            }
            if((bytes = send(send_args->sock, send_args->send_msg, strlen(send_args->send_msg), 0)) <= 0) {
                perror("send() error");
            }
            printf("%d-%02d-%02d %02d:%02d:%02d: Sent to server! \tbytes: %ld\n",
                    tm.tm_year + 1900,
                    tm.tm_mon + 1,
                    tm.tm_mday, 
                    tm.tm_hour,
                    tm.tm_min,
                    tm.tm_sec,
                    bytes
            );
        }
    }
    printf("Send handler exited normaly..\n");
    pthread_exit(0);
}

void *recv_handler(void *args) {
    thr_args *recv_args = args;
    ssize_t bytes = 0;
    printf("Recv handler start..\n");
    while(1) {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        // returns number of bytes received, or -1 if error
        bytes = recv(recv_args->sock, recv_args->buffer, sizeof(recv_args->buffer), 0);
        if(bytes > 0) {
            if(recv_args->buffer[bytes -1] == '\n'){
                recv_args->buffer[bytes -1] = '\0';
            }
            if(strncmp(recv_args->buffer,"-sf",3) == 0) {
                printf("Recieving file..\n");
                if(recv__file(recv_args->sock) == false) {
                    perror("recv__file() error");
                    break;
                }
                memset(recv_args->buffer, 0, MAX_BUFFER);
                continue;
            }
            printf("%d-%02d-%02d %02d:%02d:%02d: Server : %s\n",
                tm.tm_year + 1900,
                tm.tm_mon + 1,
                tm.tm_mday,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec,
                recv_args->buffer
            );
            memset(recv_args->buffer, 0, MAX_BUFFER);
        }
        else if(bytes == 0){
            printf("No data from server..\n");
            break;
        }
        else {
            perror("recv() error");
            break;
        }
    }
    printf("Recv handler exited normaly..\n");
    pthread_exit(0);
}

bool isValidIp(const char *ip){
    struct sockaddr_in test;
    return inet_pton(AF_INET, ip, &(test.sin_addr)) == 1;
}

int main(int argc, const char **argv) {
    struct sockaddr_in serv_addr;  /* dest */
    int i, conv_addr, port = 0;    /* converted addr */
    char input_addr [15];
    memset(&serv_addr, '\0', sizeof(serv_addr));
    memset(input_addr, '\0', sizeof(input_addr));
    if(argc < 3) {
        printf("Enter ip/port.\nUsage: ./client <ip_addr> <port>\n");
        return 1;
    }
    if(!isValidIp(argv[1])) {
        printf("Invalid ip.\n");
        return 1;
    }
    strncpy(input_addr, argv[1], strlen(argv[1]));
    for(i=0; i<strlen(argv[2]); i++) {
        if(!isdigit(argv[2][i])) {
            printf("Invalid port.\n");
            return 1;
        }
    }
    if((port = atoi(argv[2])) > 65535 && port < 0) {
        printf("Port number not in range.\n");
        return 1;
    }
    thr_args *args = malloc(sizeof *args);
    if((args->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket() create error");
        return 1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(input_addr);
    serv_addr.sin_port = htons(port);
    conv_addr = inet_pton(AF_INET, input_addr, &serv_addr.sin_addr);
    if(conv_addr <= 0) {
        perror("invalid | not supported address.");
        return 1;
    }
    // sockId, foreignAddr, addrLen
    if(connect(args->sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect() failed.");
        return 1;
    }
    printf("Connected to host [%s:%d]\n", input_addr, port);
    pthread_t send_handler_id;
    pthread_t recv_handler_id;
    if(pthread_create(&recv_handler_id, NULL, recv_handler, args) != 0) {
        perror("pthread_create() recv_handler error");
        return 1;
    }
    if(pthread_create(&send_handler_id, NULL, send_handler, args) != 0) {
        perror("pthread_create() send_handler error");
        return 1;
    }
    if(pthread_join(send_handler_id,NULL) == 0 && pthread_join(recv_handler_id,NULL) == 0) {
        printf("Worker disconnected normaly..\n");
    }
    else { 
        printf("Worker disconnected with error..\n");
    }
    close(args->sock);
    free(args);
    return 0;
}