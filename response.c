#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

/* functions you provide */
#include "response.h"

/* functions provide by others */
#include "https_socket_comb.h"
#include "get_CGI_response.h"
#include "parse.h"
#include "logger.h"

//prototypes
Response*respond_GET(Request * request, int client_sock);
Response* respond_HEAD(Request * request, int client_sock);
Response* respond_POST(Request * request, int client_sock);
Response* error_response(char * status, char * info);
char* build_default_headers(Request * request);
char* build_additional_headers(int fd, char* mime, char* mtime);
char* build_status_line(char* version, char * status, char * text);
char* get_MIME(char * file);
char* get_mtime(const char *path);
char *get_filename_ext(char *filename);


/**********************Implementation **************/

/* The responses need to be freed after using it */
Response ** respond(Request ** requests, int num, int client_sock){
  Response ** responses = malloc(sizeof(Response*) *num);

  LOG_PRINT("Need to negerate %d responses", num);
  for(int i = 0;i< num; i++){
    /* if the request ask for close connection, we will close it after serving it*/
    if(strcasecmp(get_header_value(requests[i],"Connection"), "close")== 0){
      FD_SET(client_sock, &client_close_list);
    }
    if(requests[i] == NULL){
      responses[i] = error_response("400", "Bad Request");
      continue;
    }
    if(strcmp(requests[i]->http_version, "HTTP/1.1")!=0){
      responses[i] = error_response("503", "HTTP_VERSION_NOT_SUPPORTED");
      continue;
    }

    if(strcmp(requests[i]->http_method, "GET")==0){
      responses[i] = respond_GET(requests[i], client_sock);
      continue;
    }
    if(strcmp(requests[i]->http_method, "HEAD")==0){
      responses[i] = respond_HEAD(requests[i], client_sock);
      continue;
    }
    if(strcmp(requests[i]->http_method, "POST")==0){
      responses[i] = respond_POST(requests[i], client_sock);
      continue;
    }
    LOG_PRINT("woops, the meathod is not implemented");
    responses[i] = error_response("501", "Not Implemented");
  }
  return responses;
}

/**
 *    GET method functions
 */
Response *respond_GET(Request * request, int client_sock){
  printf("-------user used GET method -------\n");
  if((strstr(request->http_uri, "/cgi"))){
    /* CGI */
    LOG_PRINT("using CGI~~~");
    /* The wait for CGI to be */
    Response * response = malloc(sizeof(Response));
    response->is_CGI = 1;
    response->CGI_response = NULL;
    get_CGI_response(request, client_sock);
    return response;
  }
  int fd;
  char* status_line;
  char* default_headers, * additional_headers;
  char* result  = (char *)malloc(8192);
  Response * response = malloc(sizeof(Response));
  response->is_CGI = 0;
  /**********************get file_url******************************/
  char * request_file = request->http_uri[0] == '/'? &(request->http_uri[1]):request->http_uri;
  /* NOTE: I assume here that file_url is less than 99 characters*/
  char * file_url = malloc(100);
  sprintf(file_url,"%s/%s", WWW_FOLDER, request_file);
  if ((fd = open(file_url, O_RDONLY)) < 0) {
      LOG_PRINT("cannot file %s", file_url);
      free(response);
      free(file_url);
      free(result);
      return error_response("404", "Not Found");
  }

  response->file_url = file_url;


  /*****************generate status_line and headers***************/
  status_line = build_status_line("HTTP/1.1", "200", "OK");
  default_headers = build_default_headers(request);
  additional_headers = build_additional_headers(fd, get_MIME(request_file), get_mtime(file_url));
  sprintf(result, "%s%s%s\r\n", status_line, default_headers, additional_headers);
  free(status_line);
  free(default_headers);
  free(additional_headers);
  printf("%s\n", result);
  response->status_and_headers = result;
  close(fd);
  return response;
}

/**
 *    HEAD method functions
 */
Response* respond_HEAD(Request * request, int client_sock){
  Response* response = respond_GET(request, client_sock);
  response->file_url = NULL;
  return response;
}

/**
 *    POST method functions
 */
Response* respond_POST(Request * request, int client_sock){
  if(atoi(get_header_value(request,"Content-Length"))<0){
    return error_response("400", "Bad Request");
  }
  if((strstr(request->http_uri, "/cgi"))){
    /* CGI */
    LOG_PRINT("using CGI~~~");
    /* The wait for CGI to be */
    Response * response = malloc(sizeof(Response));
    response->is_CGI = 1;
    response->CGI_response = NULL;
    get_CGI_response(request, client_sock);
    return response;
  }
  return 0;
}

/* Both Response and msg need to be freed */
Response * error_response(char * status, char * info){
  LOG_PRINT("NOW it's an error, need to send error response");
  Response * response = malloc(sizeof(Response));
  char* msg = (char *)malloc(8192);
  sprintf(msg, "HTTP/1.1 %s %s\r\n%s\r\n", status, info, build_default_headers(NULL));
  response->status_and_headers = msg;
  response->file_url = NULL;
  response->is_CGI = 0;
  return response;
}

/* Return 0 if provide everything, return 1 if there is still more to write
 * return -1 if error occur.
 */
int provide_data(int client_sock){
  Response_sending_status* status = responses_to_be_write[client_sock];
  if(status->is_done) return 0;
  Response* response = status->responses[status->index];
  if(status->is_body == 0){
    printf("header to write %s\n", response->status_and_headers);

    if(comb_write(client_sock, response->status_and_headers,
        strlen(response->status_and_headers)) < 0){
      LOG_PRINT("Fail to write headers to client %d", client_sock);
    }
    LOG_PRINT("write to client %d : %s", client_sock, response->status_and_headers);
    status->is_body = 1;
    if(response->file_url == NULL && (status->index)+1 == status->num) return 0;
  }else{
    if(response->file_url == NULL){
      status->is_body = 0;
      status->index++;
      if(status->index == status->num){
        status->is_done = 1;
        return 0;
      }
    }else{
      char buff[8192];
      memset(buff, 0, 8192);
      int fd = open(response->file_url, O_RDONLY);  //error handling
      if(fd < 0) return -1;
      lseek(fd, status->offset, SEEK_SET);
      int readret = read(fd, buff, 8192); //error handling
      if(readret < 0) return -1;
      if(comb_write(client_sock, (void *)buff, readret) < 0) return -1;
      LOG_PRINT("write to client %d : %s", client_sock, buff);
      printf("write to client: %s\n", buff);
      status->offset += readret;
      close(fd);
      if(readret != 8192){
          //meaning it's the last time
          status->index++;
          status->is_body = 0;
          LOG_PRINT("total sent: %d bytes", status->offset);
          status->offset = 0;
          if(status->index == status->num){
            status->is_done = 1;
            return 0;
          }
      }
    }
  }
  return 1;
}

/*********************ultility helper functions *******************/
char* get_mtime(const char *path){
    struct stat statbuf;
    struct tm *info;
    char* date = malloc(80);
    if (stat(path, &statbuf) == -1) {
        printf("cannot find path: %s\n", path);
        return NULL;
    }
    time_t raw_time = statbuf.st_mtime;
    info = localtime(&raw_time);

    strftime(date,80,"%a, %d %b %Y %T %Z", info);
    return date;
}

char *get_filename_ext(char *filename){
  char * ext = strrchr(filename, '.');
  return ext + 1;
}

char* get_MIME(char * file){
  char* ext = get_filename_ext(file);
  if(strcmp(ext,"png")==0){
    return "image/png";
  }
  if(strcmp(ext,"css")==0){
    return "text/css";
  }
  if(strcmp(ext,"html")==0){
    return "text/html";
  }
  if(strcmp(ext,"jpg")==0){
    return "imgae/jpeg";
  }
  if(strcmp(ext,"gif")==0){
    return "image/gif";
  }
  return "application/octet-stream";
}

Response_header * build_response_header(char * name, char * value){
    Response_header * result = (Response_header *)malloc(sizeof(Response_header));
    strcpy(result->header_name, name);
    strcpy(result->header_value,value);
    return result;
}

char * headers_to_str(Response_header ** headers, int num){
    char * result = malloc(10);
    result[0]='\0';
    char * new_result;
    char * header;
    for (int i = 0; i < num; i++) {
        header = malloc(strlen(headers[i]->header_name)
          + strlen(headers[i]->header_value) + 10);
        sprintf(header, "%s: %s\r\n", headers[i]->header_name,
          headers[i]->header_value);
        new_result = (char *)malloc(strlen(result)+strlen(header)+10);
        sprintf(new_result, "%s%s",result, header);
        free(result);
        free(header);
        free(headers[i]);
        result = new_result;
    }
    return result;
}

/*****************ultility functions ******************/
char * build_status_line(char* version, char * status, char * text){
    char * line;
    line = (char *) malloc(strlen(version) + strlen(status) + strlen(text)
                        + 10);
    if (sprintf(line, "%s %s %s\r\n", version, status, text) < 0) {
        printf("Failed building status line \n");
    }
    return line;
}

char* build_default_headers(Request * request){
  char date[80];
  time_t rawtime;
  struct tm *info;
  time( &rawtime );
  info = localtime( &rawtime );
  strftime(date,80,"%a, %d %b %Y %T %Z", info);

  Response_header * connection_h;
  if(strcasecmp(get_header_value(request, "Connection"), "Close") == 0){
    connection_h = build_response_header("Connection", "Close");
  }else{
    connection_h = build_response_header("Connection", "keep-alive");
  }
  Response_header * date_h = build_response_header("Date", date);
  Response_header * server_h = build_response_header("Server", "Liso/1.0");

  Response_header * headers[3];
  headers[0] = server_h;
  headers[1] = date_h;
  headers[2] = connection_h;
  char * default_headers;
  default_headers = headers_to_str(headers, 3);
  return default_headers;
}

char * build_additional_headers(int fd, char* mime, char* mtime){
  //calc length
  char len[10];
  char buff[4096];
  int readRet;
  int length = 0;
  while((readRet = read(fd, buff, 4096))>0){
    length+=readRet;
  }
  sprintf(len,"%d",length);

  Response_header * cont_len_h = build_response_header("Content-Length",len);
  Response_header * cont_type_h = build_response_header("Content-Type",mime);
  Response_header * last_h = build_response_header("Last-Modified",mtime);
  free(mtime);

  Response_header * headers[3];
  headers[0] = cont_len_h;
  headers[1] = cont_type_h;
  headers[2] = last_h;
  char * additional_headers;
  additional_headers = headers_to_str(headers, 3);
  return additional_headers;
}





/*unused functions*/
void send_message(int client_fd, char * result){
  int total_len = strlen(result);
  LOG_PRINT("sent headers: %s\n", result);
  if ((comb_write(client_fd, result, total_len)) <= 0)
  {
      close(client_fd);
      fprintf(stderr, "Error sending status line to client.\n");
      return;
  }
}
