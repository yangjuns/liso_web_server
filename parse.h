#ifndef PARSE_H
#define PARSE_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SUCCESS 0

//Header field
typedef struct
{
	char header_name[4096];
	char header_value[4096];
} Request_header;

//HTTP Request Header
typedef struct
{
	char http_version[50];
	char http_method[50];
	char http_uri[4096];
	Request_header **headers;
	int header_count;
	char* request_body;
	int body_size;
} Request;

Request** parse(char *buffer, int size,int socketFd, int* num);
char* get_header_value(Request * request, char * header_name);

#endif