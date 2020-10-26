#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>

#define MAX_BUFFER 1024
#define THRNUM 100 // 50 connections

/*
    compile => gcc -(l)pthread -o server tcp_server.c (-lm)
    - The math library must be linked in when building the executable. How to do this varies by environment, but in Linux/Unix, just add -lm to the command:
    - used to fetch filesize

    OUTPUT convert to Network Byte Order 
    INPUT  convert to Host    Byte Order

    // presentation to network
    inet_pton(AF_INET, ip_addr, &(sa.sin_addr)); // IPv4
    // network to presentation
    inet_ntop(AF_INET, &(sa.sin_addr), ip_addr, INET_ADDRSTRLEN);

    struct sockaddr_in {
        short             int sin_family;
        unsigned short    int sin_port;
        struct   in_addr      sin_addr;
        unsigned char         sin_zero[8];
    };

    struct in_addr {
        uint32_t s_addr; // 4 bytes
    };

    int getaddrinfo(const char *node,          // e.g. "www.example.com" or IP
                    const char *service,       // e.g. "http" or port number
                    const struct addrinfo *hints,
                    struct addrinfo **res);
*/

typedef struct{
    char send_msg[MAX_BUFFER];
    char buffer[MAX_BUFFER];
    char converted_addr[16];
    int sock;
    unsigned int port;
} thr_args;


bool send__file(int sockfd)
{
    FILE *fp;
    char *end_seq = "--sf--end";
    char fpath[MAX_BUFFER];
    ssize_t bytes, overall_bytes, filesize = 0;
    ssize_t overall_fbytes, fbytes, bytesleft;
    char *metadata, *strfilesize, *data;
    int n_digits_fsize = 0;

    printf("Enter file path: ");
    if(fgets(fpath, sizeof(fpath), stdin) != NULL)
    {
        size_t len = strlen(fpath);
        if(len > MAX_BUFFER)
            return false;

        if(fpath[len -1] == '\n')
            fpath[len - 1] = '\0';
    }

    if ((fp = fopen(fpath, "rb")) == NULL){
        fprintf(stderr, "Error fopen() rb\n");
        return false;
    }

    // get file size
    fseek(fp, 0L, SEEK_END); /* jump to end of file */
    filesize = ftell(fp);    /* current byte of file == filesize */
    rewind(fp);              /* jump to beginning of file */
    printf("File size: %ld\n", filesize);

    // convert filesize to string
    n_digits_fsize = floor(log10(abs(filesize))) + 1;
    metadata = malloc(strlen(fpath) +1 + n_digits_fsize);
    strfilesize = malloc(strlen(metadata) +1);

    if(metadata == NULL || strfilesize == NULL){
        perror("metadata or strfilesize malloc failed.");
        return false;
    }

    // convert to string & concat metadata
    sprintf(strfilesize, "%ld", filesize);
    strcat(metadata, strfilesize);
    strcat(metadata, fpath);

    // send file metadata
    printf("Sending file metadata..\n");
    if((bytes = send(sockfd, metadata, strlen(metadata), 0)) == -1){
        perror("send file metadata failed.");
        return false;
    }

    free(metadata);
    free(strfilesize);

    bytesleft = filesize;

    printf("Sending data..\n");    
    data = malloc(filesize); // allocate filesize memory for data
    if(data == NULL){
        perror("data malloc failed.");
        return false;
    }

    while((fbytes = fread(data, sizeof(char), filesize, fp)) > 0)
    {
        overall_fbytes += fbytes;
        
        if((bytes = send(sockfd, data, fbytes, 0)) == -1){
            perror("send data failed.");
            return false;
        }
        memset(data, 0, filesize);

        overall_bytes += bytes;
        bytesleft -= bytes;
    }

    // send seq as flag indicating EOF
    printf("Sending end-seq..\n");
    sleep(1); // useless??
    if((bytes = send(sockfd, end_seq, strlen(end_seq), 0)) == -1){
        perror("send end-seq. failed.");
        return false;
    }
    if(overall_bytes != filesize){
        printf("Data loss: fsize: %ld / sent: %ld / bytes-left: %ld", filesize, overall_bytes, bytesleft);
    } else {
        printf("Success!\nRead from file: [%ld]\nSent: [%ld]b\n", overall_fbytes, overall_bytes);
    }
    free(data);
    fclose(fp);
    return true;
}



void *send_handler(void *args) // passing multiple args by pointer to struct
{ 
    thr_args *send_args = args;
    ssize_t bytes = 0;

    printf("Send handler %ld start..\n", pthread_self());

    while(1)
    {    
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);

        memset(send_args->send_msg, '\0', sizeof(send_args->send_msg));
        fgets(send_args->send_msg, sizeof(send_args->send_msg), stdin);

        send_args->send_msg[strlen(send_args->send_msg) -1] = '\0';

        if(send_args->send_msg[0] == '\0'){
            printf("Enter msg..\n");
        }
        else if(!strncmp(send_args->send_msg,"x",1)){
            printf("[!] Exit.\n");
            exit(0);
        }
        else if(!strncmp(send_args->send_msg,"-sf",3)){
            printf("Sending file..\n");
            // send '-sf' seq as flag indicating incoming file
            send(send_args->sock, send_args->send_msg, strlen(send_args->send_msg), 0);
            
            if(!send__file(send_args->sock))
                printf("Error sending file.\n");
        }
        else {
            // check if client is connected
            if(recv(send_args->sock,NULL,1, MSG_PEEK | MSG_DONTWAIT) == 0)
            {
                printf("%d-%02d-%02d %02d:%02d:%02d: Client disconnected\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec
                );
                break;
            }

            bytes = send(send_args->sock, send_args->send_msg, strlen(send_args->send_msg), 0);
            if(bytes > 0){
                printf("%d-%02d-%02d %02d:%02d:%02d: Sent to: %s:%d \tlen: %ld\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                    tm.tm_sec, send_args->converted_addr, send_args->port, bytes
                );  
            }
        }
    }

    close(send_args->sock);
    free(send_args);

    printf("Send handler %ld exited normaly..\n", pthread_self());
    pthread_exit(0);
}

void *recv_handler(void *args)
{
    thr_args *recv_args = args;
    ssize_t bytes = 0;

    printf("Recv handler %ld start..\n", pthread_self());

    while(1)
    {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);

        bytes = recv(recv_args->sock, recv_args->buffer, sizeof(recv_args->buffer), 0);

        if(bytes > 0){
            printf("%d-%02d-%02d %02d:%02d:%02d: Client [%s:%d] : %s\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, recv_args->converted_addr, recv_args->port, recv_args->buffer
            );
            memset(recv_args->buffer, 0, MAX_BUFFER);
        }

        else if(bytes == 0){
            printf("No data from [%s:%d]\n",recv_args->converted_addr, recv_args->port);
            break;
        }

        else perror("recv");
    }
    printf("Recv handler %ld exited normaly..\n", pthread_self());
    pthread_exit(0);
}

bool isValidIp(const char *ip){
    struct sockaddr_in test;
    int result = inet_pton(AF_INET, ip, &(test.sin_addr));
    return result == 1;
}

int main(int argc, const char **argv) {
    struct sockaddr_in client_addr;           // client addr
    int server_fd;                            
    int opt = 1;                              // sock option
    int addrlen = sizeof(client_addr);        // length of client addr
    char buffer[1024];                        // recv from clients
    int port = 0;
    int i;

    memset(&client_addr, '\0', sizeof(client_addr));
    memset(buffer, '\0', sizeof(buffer));

    if(argc < 3)
    {
        printf("Enter ip/port.\nUsage: ./client <ip_addr> <port>\n");
        return 1;
    }

    if(!isValidIp(argv[1])){
        printf("Invalid ip.\n");
        return 1;
    }

    for(i=0; i<strlen(argv[2]); i++)
    {
        if(!isdigit(argv[2][i])) {
            printf("Invalid port.\n");
            return 1;
        }
    }

    if((port = atoi(argv[2])) > 65535 && port < 0){
        printf("Port number not in range.\n");
        return 1;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {   // in socket() function, 0 is default protocol [TCP]
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    client_addr.sin_family      = AF_INET;       
    client_addr.sin_addr.s_addr = INADDR_ANY;    // bind to all interfaces
    client_addr.sin_port        = htons(port); 

    if (bind(server_fd, (struct sockaddr *)&client_addr, sizeof(client_addr))<0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // THREAD_NUM queue limit for handlers
    printf("Listening for incomming connections on port %d..\n", port);
    if (listen(server_fd, THRNUM) < 0) {                
        perror("listen");
        exit(EXIT_FAILURE);
    }

    pthread_t send_handler_id[THRNUM];
    pthread_t recv_handler_id[THRNUM];

    for(i=1;i<=THRNUM;i++)
    {

        thr_args *args = malloc(sizeof *args);       
        char clientAddr [16] = "";     // displaying ips

        if ((args->sock = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t*)&addrlen))<0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        // convert addr from network format to text format
        inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, clientAddr, sizeof(clientAddr));
        printf("Client [%s:%d] connected!\n",clientAddr, client_addr.sin_port);

        args->port = client_addr.sin_port;   // copy client port -> handler parameter

        // handler param
        strncpy(args->converted_addr, clientAddr, strlen(clientAddr));

        if(pthread_create(&send_handler_id[i], NULL, send_handler, args) != 0){ 
            close(args->sock);
            printf("Error creating [%ld] send handler..",send_handler_id[i]);
        }

        if(pthread_create(&recv_handler_id[i], NULL, recv_handler, args) != 0){
            close(args->sock);
            printf("Error creating [%ld] recv handler...",recv_handler_id[i]);
        }

        printf("Created %d threads..\n",(i * 2));
    }


    for(i=1;i<=THRNUM;i++)
    {
        if(pthread_join(send_handler_id[i],NULL) == 0 
         && pthread_join(recv_handler_id[i],NULL) == 0){
            printf("Worker %d disconnected normaly..\n",i);
        }
        else {
            printf("Worker %d disconnected with error..\n",i);
        }
    }

    close(server_fd);
    return 0;
}
