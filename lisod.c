/*
 * This server can accept at most 5 connections at the same time.
 * It always keep the connection alive even though it may response
 * "Connection: Close". It closed the connection when the client
 * closed the connection.
 */

#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/select.h>


/* The functions and variables you provide */
#include "lisod.h"
#include "param.h"

/* The functions other files provide*/
#include "logger.h"
#include "response.h"
#include "daemonize.h"
#include "https_socket_comb.h"


#define BUF_SIZE         81920   /* NOTE: WE assuming it can hold 10 pipelined requests*/
#define BLOCK_SIZE       8192    /* This is # of bytes each time we write to a client*/

/***************Global Varibles Used to Keep Track of Clients*************/
int sock, s_sock;
fd_set read_list, write_list;
fd_set client_close_list;
fd_set cgi_read_list;
fd_set fd_used_to_write_to_cgi;
fd_set fd_used_to_read_from_cgi;
int cgi_to_client[40];
int client_to_cgi[40];
Request* cgi_to_request[40];
/* We use it to keep track of all the responses that we need to write back*/
Response_sending_status* responses_to_be_write[20];

int close_socket(int sock){
    if (close(sock)) {
        LOG_PRINT("Failed closing socket.\n");
        return 1;
    }
    return 0;
}
/********************Functions Prototypes************************************/

void liso_shutdown(int status);
void clean_up_client(int client_sock);
void clean_up_cgi(int cgi_fd);

/*****************************Implementation *******************************/
int main(int argc, char * argv[]){
    /* setting the default values for arguments */
    HTTP_PORT = 8808;
    HTTPS_PORT = 4403;
    LOCK_FILE = "lock.txt";
    LOG_FILE = "log.txt";
    WWW_FOLDER = "./www";
    CGI_SCRIPT_PATH = "./CGI/cgi_script.py";
    PRIVATE_KEY_FILE = "./private/yangjuns.key";
    CERTIFICATE_FILE = "./certs/yangjuns.crt";

    ssize_t readret;
    char buf[BUF_SIZE];
    memset(cgi_to_client, 0,1024);
    fd_set read_fd_set, write_fd_set;

    /* passin the arguments*/
    switch(argc){
      case 9:
        CERTIFICATE_FILE = argv[8];
      case 8:
        PRIVATE_KEY_FILE = argv[7];
      case 7:
        CGI_SCRIPT_PATH = argv[6];
      case 6:
        WWW_FOLDER = argv[5];
      case 5:
        LOCK_FILE = argv[4];
      case 4:
        LOG_FILE = argv[3];
      case 3:
        HTTPS_PORT = atoi(argv[2]);
      case 2:
        HTTP_PORT = atoi(argv[1]);
      case 1:
        break;
      default:
        printf("USAGE: \n");
        break;
    }

    daemonize(LOCK_FILE);
    fprintf(stdout, "------ Liso Server -----\n");

    /* SSL initialization */
    comb_init(PRIVATE_KEY_FILE, CERTIFICATE_FILE);

    /* create server sockets, one for HTTP, one for HTTPS*/
    sock = comb_create_sock(HTTP_PORT, HTTP);
    s_sock = comb_create_sock(HTTPS_PORT, HTTPS);

    /* initialize the set of active sockets*/
    FD_ZERO(&read_list);
    FD_ZERO(&write_list);
    FD_ZERO(&client_close_list);
    FD_ZERO(&cgi_read_list);
    FD_SET(sock, &read_list);
    FD_SET(s_sock, &read_list);
    memset(client_to_cgi, 0, 40);
    memset(cgi_to_client, 0 , 40);
    memset(cgi_to_request, 0, 40);
    /* used to deal with the case when client only write one byte to test
     * connection, in this case, we save this byte. We save client i's one byte
     * at holding_requests[i]. Since we have 2 severs, 0,1,2 fds are open and we
     * only accept at most 5 connections, the first client starts with 6, so 20
     * is enough to hold every client_fd.
     */
    char *holding_requests[20];
    memset(holding_requests, 0, 20);
    /* finally, loop waiting for input and then write it back */
    while (1) {
        /* Block until input arrives on one or more active sockets*/
        read_fd_set = read_list;
        write_fd_set = write_list;
        if (select(1024, &read_fd_set, &write_fd_set, NULL, NULL) < 0) {
            LOG_PRINT("Failed using select. Shutting down server...");
            liso_shutdown(EXIT_FAILURE);
        }

        /* Serive all the sockets with input pending*/
        for (int i = 0; i < 1024; i++) {
            if (FD_ISSET(i, &read_fd_set)) {
                if (i == sock || i == s_sock) {
                    /*HTTP or HTTPS connection comes in*/
                    int is_secure = (i == sock) ? HTTP : HTTPS;
                    int client_sock = comb_accept(is_secure, i);
                    FD_SET(client_sock, &read_list);
                    LOG_PRINT("Add client %d to read list.", client_sock);
                }else if(FD_ISSET(i, &cgi_read_list)){
                    /* some cgi has data to read */
                    if(cgi_to_client[i]>0){
                      /* cgi pipe is ready ti read*/
                      LOG_PRINT("CGI fd %d is ready to read", i);
                      while((readret = read(i, buf, BUF_SIZE-1)) > 0){
                          buf[readret] = '\0'; /* nul-terminate string */
                          /* NOTE: we assume that client can handle the speed we write data */
                          if(comb_write(cgi_to_client[i], buf, readret) < 0){
                            LOG_PRINT("Failed writing cgi respones back to client.");
                          }
                          LOG_PRINT("CGI let me write : %s", buf);
                      }
                      if (readret == 0){
                          LOG_PRINT("Finished serving client %d. Closing connected", cgi_to_client[i]);
                          clean_up_cgi(i);
                          clean_up_client(cgi_to_client[i]);
                      }else{
                        LOG_PRINT("Failed readding CGI");
                      }
                      memset(buf,0, BUF_SIZE);
                    }
                }else{
                    /*Data arriving from client socket*/
                    LOG_PRINT("Client %d is ready to read.",i);
                    /* NOTE: here we assume that bufsize if big enough to hold 10 piped requests
                     * so that we don't need to read from the client agian unless the client
                     * write to us multiple times
                     */
                    readret = comb_read(i, buf, BUF_SIZE);
                    if (readret > 0) {
                        /* client sent some data */
                        /* NOTE: I hard-coded it to deal with the case that client send
                         * to us multiple times to complete a single request. And I'm assuming
                         * that 1) A client writes at most twice to complete a single request and
                         * 2) the first time it only writes one byte to test connection
                         */
                        if(readret < 2){
                          char client_buf [10];
                          strcpy(client_buf, buf);
                          holding_requests[i] = client_buf;
                          memset(buf, 0, BUF_SIZE);
                          continue;
                        }else{
                          if(holding_requests[i]!=NULL){
                            char temp[BUF_SIZE];
                            sprintf(temp,"%s%s",holding_requests[i],buf);
                            strcpy(buf,temp);
                            readret = strlen(holding_requests[i])+readret;
                            holding_requests[i] = NULL;
                          }
                        }
                        /* now the client is an canadiate that we wants to write
                          to and we no longer need to read from it. */
                        FD_CLR(i, &read_list);

                        /* Since we keep connection alive, we might still want
                         * to read from the client*/
                        FD_SET(i, &read_list);
                        LOG_PRINT("client %d's request:\n%s",i, buf);
                        int num; // The number of requests that the client wrote to us.
                        Request ** requests = parse(buf, readret, i, &num);
                        memset(buf, 0, BUF_SIZE);
                        /* Generate all the responses we need to write back to client*/
                        Response ** responses = respond(requests, num, i);
                        /* We've used request, now we can free it */
                        for(int j =0;j<10; j++){
                          if(requests[j]!= NULL) free(requests[j]);
                        }
                        free(requests);

                        printf("num : %d\n", num);
                        for(int j =0;j<num; j++){
                          printf("%s\n", responses[j]->status_and_headers);
                        }
                        /* We keep a list of clients and it's response we need to write
                         * If responses_to_be_write[i] is not NULL, it means that we still have
                         * things that we need to write to clients
                         */
                        Response_sending_status * responses_status = malloc(sizeof(Response_sending_status));
                        responses_status->responses = responses;
                        responses_status->num = num;
                        responses_status->index = 0;
                        responses_status->is_body = 0;
                        responses_status->offset = 0;
                        responses_status->is_done = 0;
                        responses_to_be_write[i] = responses_status;

                        /* We add the client to the write list. When it's ready to write,
                         * we write to it.
                         */
                        if(client_to_cgi[i]>0){
                          /* In this case, client needs to wait fro CGI to complete, so
                           * we would not add client to the write_list*/
                        }else{
                          FD_SET(i, &write_list);
                        }
                    }else{
                      if (readret == 0){
                        /* client closed TCP connection, we clean up eveything that's about the client */
                        LOG_PRINT("Client %d closed connection. Closing connection and clean it up.",
                          i);
                        clean_up_client(i);
                      }else{
                        /* readerror */
                        LOG_PRINT("Fail reading message from client %d. Closing connection and remove it from read list. Closing connection..",
                          i);
                        clean_up_client(i);
                      }
                    }
                    memset(buf, 0, BUF_SIZE);
                }
            }else {
              if (FD_ISSET(i, &write_list)){
                if(FD_ISSET(i, &fd_used_to_write_to_cgi)){
                  LOG_PRINT("CGI fd %d is waiting for my input", i);
                  Request* request = cgi_to_request[i];
                  char* post_body  = request->request_body;
                  LOG_PRINT("post_body passed to the cgi: %s", post_body);
                  if (write(i, (void *)post_body, request->body_size)< 0){
                      LOG_PRINT("Error writing to spawned CGI program.");
                      /* TODO: need to handle this case */
                  }
                  /* finished writing to spawn */
                  cgi_to_request[i] = NULL;
                  FD_CLR(i, &write_list);
                  FD_CLR(i, &fd_used_to_write_to_cgi);
                  close(i);
                }else{
                  LOG_PRINT("Client %d is ready to write. Write to %d",i, i);
                  if(FD_ISSET(client_to_cgi[i], &cgi_read_list)){
                    LOG_PRINT("But the cgi %d is still working, please wait",i);
                  }else{
                    int status;
                    if((status = provide_data(i)) == 0){
                      LOG_PRINT("Finish serving client %d",i);
                      FD_CLR(i, &write_list);
                      if(FD_ISSET(i, &client_close_list)){
                        LOG_PRINT("Client %d asked for close. We close connection and clean it up.",i);
                        clean_up_client(i);
                      }
                    }
                    if(status < 0){
                      LOG_PRINT("Error Occur when writing to client %d. Closing connection",i);
                      clean_up_client(i);
                    }
                  }
                }
              }
            }
        }
    }
    liso_shutdown(EXIT_SUCCESS);
    return EXIT_SUCCESS;
} /* main */


void liso_shutdown(int status){
    LOG_PRINT("Closing Server...");
    comb_free();
    close_socket(sock);
    close_socket(s_sock);
    exit(status);
}

void clean_up_cgi(int cgi_fd){
  FD_CLR(cgi_fd, &read_list);
  FD_CLR(cgi_fd, &cgi_read_list);
  cgi_to_client[cgi_fd] = 0;
}

void clean_up_client(int client_sock){
  /* Need to free the responses_to_be_write list */

  /* Clear read, write list */
  FD_CLR(client_sock, &read_list);
  FD_CLR(client_sock, &write_list);
  /* Clear https_client_list */
  FD_CLR(client_sock, &https_client_list);
  /* Need to free https_client_contexts list*/

  /* Need to make client_to_cgi -1*/
  client_to_cgi[client_sock] = -1;

  /* Need to clear client_close_list*/
  FD_CLR(client_sock, &client_close_list);
  close_socket(client_sock);
}