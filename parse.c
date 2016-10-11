#include "parse.h"
#include "logger.h"
/**
 * Given a char buffer returns the parsed request headers
 * Assuming if clients send pipelined requests, there will be no PUT request
 *
 * Our sever has a limit of 10 pipeliend requests.
 * Return a list of all the requests in order, which needs to be freed after using it.
 */
Request ** parse(char * buffer, int size, int socketFd, int* num)
{
    /* assuming buffer has included all the piped requests */

    // Differant states in the state machine
    enum {
        STATE_START = 0, STATE_CR, STATE_CRLF, STATE_CRLFCR, STATE_CRLFCRLF
    };

    int i         = 0, state;
    size_t offset = 0;
    char ch;
    char buf[8192];
    Request** all_requests = (Request**) malloc(sizeof(Request *)*10);
    for(int j =0;j<10;j++){
      all_requests[j] = NULL;
    }


    int index = 0;
    int is_POST = 0;
    int post_offset = 0;
    /* parsing the request header(or headers if there are pipelined requests) */
    while(i!=size){
      offset = 0;
      memset(buf, 0, 8192);
      state = STATE_START;
      while (state != STATE_CRLFCRLF) {
          char expected = 0;
          if(i == size) break;
          ch = buffer[i++];
          if(offset < 8192){
            buf[offset++] = ch;
          }else{
            offset++;
          }

          switch (state) {
              case STATE_START:
              case STATE_CRLF:
                  expected = '\r';
                  break;
              case STATE_CR:
              case STATE_CRLFCR:
                  expected = '\n';
                  break;
              default:
                  state = STATE_START;
                  continue;
          }

          if (ch == expected)
              state++;
          else
              state = STATE_START;
      }
      /* request header is too big*/
      if(offset >= 8192){
        all_requests[index] = NULL;
      }
      // Valid End State
      if (state == STATE_CRLFCRLF) {
          Request * request = (Request *) malloc(sizeof(Request));
          request->header_count = 0;
          request->headers = (Request_header **) malloc(
            sizeof(Request_header *) * 1024);
          /* initialize the body part to be NULL*/
          request->request_body = NULL;
          request->body_size = 0;
          set_parsing_options(buf, i, request);

          if (yyparse() == SUCCESS) {
              all_requests[index] = request;
              /* If the request method is POST, then, we are done with parsing
               * request headers because we assume that POST => no pipeline */
              if(strcmp(request->http_method, "POST") == 0){
                if(index != 0){
                  /* pipelined request which included POST request, it's not allowed.
                   * The following is set up to return a BAD REQUEST response*/
                  *num = 1;
                  all_requests[0] = NULL;
                  return all_requests;
                }
                LOG_PRINT("it's a POST request, we are done with parsing");
                index++; // we need to count this one.
                post_offset = i; //the offset to read the post body
                i = size;  // this is used to break the outer loop
                is_POST = 1; // set the flag to indicate that it's a POST request
                break;
              }
          }
      }else{
        /* Malformed Request */
        all_requests[index] = NULL;
      }
      index++;
    }

    /* If it's a single POST request, we need to store the post body */
    if(is_POST){
      /* NOTE: we assume here that a request_body can be fittef into 8192 bytes */
      int body_size = size - post_offset;
      all_requests[0]->body_size = body_size;
      char* body = malloc(body_size);
      char* pointer = &buffer[post_offset];
      for(int i =0 ;i< body_size;i++){
        body[i] = pointer[i];
      }
      all_requests[0]->request_body = body;
    }
    *num = index;
    return all_requests;
} /* parse */


char* get_header_value(Request * request, char * header_name){
  if(request == NULL) return "";
  Request_header ** list = request->headers;
  int count = request->header_count;
  for(int i = 0;i < count; i++){
    if(strcasecmp(list[i]->header_name, header_name) == 0){
      return list[i]->header_value;
    }
  }
  return "";
}