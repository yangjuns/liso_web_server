#ifndef PARAM_H
#define PARAM_H

#include "response.h"
/* arguments passed in command line when start the server*/
char* LOCK_FILE;
char* LOG_FILE;
int HTTP_PORT;
int HTTPS_PORT;
char* WWW_FOLDER;
char* CGI_SCRIPT_PATH;
char* PRIVATE_KEY_FILE;
char* CERTIFICATE_FILE;

/* Global varibales that we used to keep track the clients status*/
extern fd_set read_list;
extern fd_set write_list;
extern fd_set https_client_list;

/* cgi_read_list[i] is set means that cgi_fd i has not been read yet,
 * so we cannot response to client at the moment*/
extern fd_set cgi_read_list;

/* This is the set of client who specifically asked sever to close connection */
extern fd_set client_close_list;

extern fd_set fd_used_to_write_to_cgi;
extern fd_set fd_used_to_read_from_cgi;

extern Request* cgi_to_request[40];


extern char* client_ip_addr[20];
extern int cgi_to_client[40];
extern int client_to_cgi[40];

extern Response_sending_status* responses_to_be_write[20];

#endif
