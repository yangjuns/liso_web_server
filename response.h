#ifndef RESPONSE_H
#define RESPONSE_H

#include "parse.h"

typedef struct {
    char header_name[4096];
    char header_value[4096];
} Response_header;

typedef struct Response {
    char* status_and_headers;  //need to be freed
    char* file_url;            //need to be freed
      //it is null if no body needs to be returned
    int   is_CGI;         // it's 1 if the response is generate by CGI program, 0 otherwise
    char* CGI_response;    // it's only used when is_CGI = 1
}Response;

typedef struct {
    Response ** responses;  //all the responeses list
    int num;                // the number of responeses we need to send
    int index;              // now we are sending which index;
    int is_body;          // 1 indicates we are sending body, 0 indecates we are sending the content;
    int offset;            //if we are sending the body, where should we start? the offset
    int is_done;           // 1 if we write eveything, 0 otherwise
} Response_sending_status;

/**
 *  returns 1 if there is message body needed to provide in the future, 0 if done, -1 if error occurs
 */
Response** respond(Request ** requests, int num, int client_sock);

/*
 *  It returns 1 if client will receive all the data after this round, 0 if there are still data to write, -1 if error occur writing to the client;
 */
int provide_data(int client_fd);

#endif
