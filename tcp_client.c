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
    compile => gcc -(l)pthread -o tcp_server tcp_server.c
*/

typedef struct {
    char send_msg[MAX_BUFFER];
    char buffer[MAX_BUFFER];
    int sock;
} thr_args;



void recv__file(int sockfd)
{
    FILE *fp;
    char buff[MAX_BUFFER];
    char fpath[MAX_BUFFER];
    ssize_t bytes;

    if(recv(sockfd, fpath, MAX_BUFFER -1, 0) <= 0){
        printf("Failed to recieve file extension.\n");
        return;
    }

    printf("FPATH %s\n", fpath);

    if(realpath(fpath, NULL) != NULL)
        if(remove(fpath) == 0)
            printf("File deleted. Creating new file.\n");

    if((fp = fopen(fpath, "w")) == NULL){
        fprintf(stderr, "Error fopen()\n");
        return;
    }

    int cnt=0;
    int overall_b=0;
    while(1)
    {
        cnt++;
        bytes = 0;
        memset(buff, '\0', sizeof(buff));

        if((bytes = recv(sockfd, buff, MAX_BUFFER -1, 0)) > 0){

            if(buff[bytes -1] == '\n')
                buff[bytes -1] = '\0';

            overall_b += bytes;

            printf("Iteration [%d] - Bytes [%ld]", cnt, bytes);
            printf("\n----------------------------\n");
           // printf("%s", buff);
            printf("\n----------------------------\n");

            // close after spqcial sequence recieved => EOF
            if(!strcmp(strrchr(buff, '\0') -7, "-sf-end")){
                printf("End of transmission..\n");
                break;
            }

            fprintf(fp, "%s", buff);
        }

        else if(bytes == 0){
            printf("Server disconnected.\n");
            break;
        }

        else perror("recv");
    }

    printf("Recieved: [%d]b\nPath: %s\n", overall_b, realpath(fpath, NULL));
    fclose(fp);
    return;
}


void *send_handler(void *args) // passing multiple args by pointer to struct
{ 
    thr_args *send_args = args;
    ssize_t bytes = 0;

    printf("Send handler start..\n");

    while(1)
    {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);

        memset(send_args->send_msg, '\0', sizeof(send_args->send_msg));
        fgets(send_args->send_msg, sizeof(send_args->send_msg), stdin);

        if(send_args->send_msg[strlen(send_args->send_msg) -1] == '\n')
            send_args->send_msg[strlen(send_args->send_msg) -1] = '\0';

        if(send_args->send_msg[0] == '\0'){
            printf("Enter msg..\n");
        }

        else if(!strncmp(send_args->send_msg,"x",1))
        {
            printf("[!] Exit.\n");
            exit(0);
        }

        else
        {
            // check if client is connected
            if(recv(send_args->sock,NULL,1, MSG_PEEK | MSG_DONTWAIT) == 0)
            {
                printf("Client disconnected.\n");
                break;
            }

            if((bytes = send(send_args->sock, send_args->send_msg, strlen(send_args->send_msg), 0)) <= 0){
                printf("Send error\n");
            }

            printf("%d-%02d-%02d %02d:%02d:%02d: Sent to server! \tbytes: %ld\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                    tm.tm_min, tm.tm_sec, bytes
            );
        }
    }
    printf("Send handler exited normaly..\n");
    pthread_exit(0);
}


void *recv_handler(void *args){

    thr_args *recv_args = args;
    ssize_t bytes = 0;

    printf("Recv handler start..\n");

    while(1)
    {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);

        // returns number of bytes received, or -1 if error
        bytes = recv(recv_args->sock, recv_args->buffer, sizeof(recv_args->buffer), 0);

        if(bytes > 0){
            if(!strncmp(recv_args->buffer,"-sf",3))
            {
                printf("Recieving file..\n");
                recv__file(recv_args->sock);
                memset(recv_args->buffer, 0, MAX_BUFFER);
                continue;
            }

            printf("%d-%02d-%02d %02d:%02d:%02d: Server : %s\n",tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, recv_args->buffer);
            memset(recv_args->buffer, 0, MAX_BUFFER);
        }

        else if(bytes == 0){
            printf("No data from server..\n");
            break;
        }

        else perror("recv");
    }

    printf("Recv handler exited normaly..\n");
    pthread_exit(0);
}


bool isValidIp(const char *ip){
    struct sockaddr_in test;
    int result = inet_pton(AF_INET, ip, &(test.sin_addr));
    return result == 1;
}



int main(int argc, const char **argv)
{
    struct sockaddr_in serv_addr;  // dest
    char input_addr [15];
    int conv_addr = 0;             // converted addr
    int port = 0;
    int i;

    memset(&serv_addr, '\0', sizeof(serv_addr));
    memset(input_addr, '\0', sizeof(input_addr));

    if(argc < 3)
    {
        printf("Enter ip/port.\nUsage: ./client <ip_addr> <port>\n");
        return 1;
    }

    if(!isValidIp(argv[1])){
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

    if((port = atoi(argv[2])) > 65535 && port < 0){
        printf("Port number not in range.\n");
        return 1;
    }

    thr_args *args = malloc(sizeof *args);

    if((args->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\nSocket creation error..\n");
        return -1;
    }

    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(input_addr);
    serv_addr.sin_port        = htons(port);

    conv_addr = inet_pton(AF_INET, input_addr, &serv_addr.sin_addr);

    if(conv_addr <= 0) {
        printf("\nInvalid address/Address not supported..\n");
        return -1;
    }


    // sockId, foreignAddr, addrLen
    if (connect(args->sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    printf("Connected to host [%s:%d]\n", input_addr, port);

    pthread_t send_handler_id;
    pthread_t recv_handler_id;

    if(pthread_create(&recv_handler_id, NULL, recv_handler, args) != 0){
        printf("Error creating recv handler..");
    }
    
    if(pthread_create(&send_handler_id, NULL, send_handler, args) != 0){
        printf("Error creating send handler..");
    }


    printf("Joining 2 threads..\n");

    if(pthread_join(send_handler_id,NULL) == 0 
     && pthread_join(recv_handler_id,NULL) == 0){
        printf("Worker disconnected normaly..\n");
    }
    else {
        printf("Worker disconnected with error..\n");
    }

    close(args->sock);
    free(args);
    return 0;
}