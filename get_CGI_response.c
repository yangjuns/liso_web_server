#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Implment the functions you provide*/
#include "get_CGI_response.h"

/* Functions and variables provided by others*/
#include "parse.h"
#include "param.h"
#include "https_socket_comb.h"
#include "logger.h"

/**************** BEGIN CONSTANTS ***************/
/* note: null terminated arrays (null pointer) */
char* ENVP[30];

/**************** END CONSTANTS ***************/

void execve_error_handler();

/* Need to be free after using it*/
char* gen_env_var(Request * request,char* prefix,  char* header){
  char* value = get_header_value(request, header);
  int size = strlen(header)+strlen(value);
  char* result = (char*) malloc(size+10);
  sprintf(result, "%s=%s", prefix, value);
  return result;
}
/* Need to be freed after using it*/
char* my_concat(char* str1, char* str2){
  if(str1 == NULL) str1 = "";
  if(str2 == NULL) str2 = "";
  int size = strlen(str1) + strlen(str2);
  char * result = (char *)malloc(size+10);
  sprintf(result, "%s%s", str1, str2);
  return result;
}

int fill_in(Request * request, int client_fd){
  /* Fill in Environment */
  ENVP[0] = gen_env_var(request, "CONTENT_LENGTH","content-length");
  ENVP[1] = gen_env_var(request, "CONTENT_TYPE", "content-type");
  ENVP[2] = "GATEWAY_INTERFACE=CGI/1.1";
  char* URI = request->http_uri;
  char* token = strtok(URI, "?");
  char request_uri[80];
  memset(request_uri, 0 ,80);
  strcpy(request_uri, token);
  char* temp = strstr(URI,"?");
  char* query_str;
  if(temp == NULL)
    query_str = "";
  else{
    query_str = (char *)malloc(80);
    strcpy(query_str, temp);
  }
  /* at this point query_str cannot be null */
  ENVP[3] = my_concat("PATH_INFO=", &request_uri[4]);
  ENVP[4] = my_concat("QUERY_STRING=", query_str);
  ENVP[5] = my_concat("REMOTE_ADDR=", client_ip_addr[client_fd]);
  ENVP[6] = my_concat("REQUEST_METHOD=", request->http_method);
  ENVP[7] = my_concat("REQUEST_URI=", request_uri);
  ENVP[8] = "SCRIPT_NAME=/CGI";
  char port[6];
  if(FD_ISSET(client_fd, &https_client_list)){
    sprintf(port,"%d", HTTPS_PORT);
  }
  else{
    sprintf(port,"%d", HTTP_PORT);
  }
  ENVP[9] = my_concat("SERVER_PORT=", port);
  ENVP[10] = "SERVER_PROTOCOL=HTTP/1.1";
  ENVP[11] = "SERVER_SOFTWARE=Liso/1.0";
  ENVP[12] = gen_env_var(request, "HTTP_ACCEPT", "accept");
  ENVP[13] = gen_env_var(request, "HTTP_REFERER", "referer");
  ENVP[14] = gen_env_var(request, "HTTP_ACCEPT_ENCODING", "accept-encoding");
  ENVP[15] = gen_env_var(request, "HTTP_ACCEPT_LANGUAGE", "accept-language");
  ENVP[16] = gen_env_var(request, "HTTP_ACCEPT_CHARSET", "accept-charset");
  ENVP[17] = gen_env_var(request, "HTTP_HOST", "host");
  ENVP[18] = gen_env_var(request, "HTTP_COOKIE", "cookie");
  ENVP[19] = gen_env_var(request, "HTTP_USER_AGENT", "user-agent");
  ENVP[20] = gen_env_var(request, "HTTP_CONNECTION", "connection");
  ENVP[21] = NULL;
  return 22;
}

void get_CGI_response(Request * request, int client_fd)
{
    /*************** BEGIN VARIABLE DECLARATIONS **************/
    pid_t pid;
    int parent_to_child[2];
    int child_to_parent[2];
    /*************** END VARIABLE DECLARATIONS **************/
    fill_in(request, client_fd);
    // for(int i =0;i< length-1;i++){
    //   LOG_PRINT(ENVP[i]);
    // }
    /*************** BEGIN PIPE **************/
    /* 0 can be read from, 1 can be written to */
    if (pipe(parent_to_child) < 0){
        LOG_PRINT("Error piping for stdin.\n");
        return;
    }
    if (pipe(child_to_parent) < 0){
        LOG_PRINT("Error piping for stdout.\n");
        return;
    }
    /*************** END PIPE **************/

    /*************** BEGIN FORK **************/
    pid = fork();
    /* not good */
    if (pid < 0){
        LOG_PRINT("Something really bad happened when fork()ing.\n");
        return;
    }

    /* child, setup environment, execve */
    if (pid == 0){
        /*************** BEGIN EXECVE ****************/
        close(parent_to_child[1]); //don't need to write to this pipe;
        close(child_to_parent[0]); //don't need to read this pipe;
        dup2(parent_to_child[0], fileno(stdin));
        dup2(child_to_parent[1], fileno(stdout));
        /* you should probably do something with stderr */

        /* pretty much no matter what, if it returns bad things happened... */
        char* ARGV[2];
        ARGV[0] = CGI_SCRIPT_PATH;
        ARGV[1] = NULL;

        if (execve(CGI_SCRIPT_PATH, ARGV, ENVP)){
            execve_error_handler();
            LOG_PRINT("Error executing execve syscall.\n");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
        /*************** END EXECVE ****************/
    }

    if (pid > 0){
        close(parent_to_child[0]); // dont need to read this pipe
        close(child_to_parent[1]); // dont need to write to this pipe

        /* Now listen to the child CGI fd, add it to the read_list*/
        LOG_PRINT("Start listening to CGI fd %d", child_to_parent[0]);
        FD_SET(child_to_parent[0], &read_list);
        FD_SET(child_to_parent[0], &cgi_read_list);
        /* This is used so that when we read from the CGI, we know who to
         * send these data */
        cgi_to_client[child_to_parent[0]] = client_fd;

        /* Also, we want to write to CGI the body input, if there is any */
        if(request->request_body != NULL){
          LOG_PRINT("Start writing to CGI fd %d", parent_to_child[1]);
          FD_SET(parent_to_child[1], &write_list);
          FD_SET(parent_to_child[1], &fd_used_to_write_to_cgi);
          /* This is used so that when we want to write to the cgi, we know
           * what is the request body so that we know what to send*/
          cgi_to_request[parent_to_child[1]] = request;
        }

        /* TODO: the purpose of the following is unclear */
        client_to_cgi[client_fd] = child_to_parent[0];
        return;
    }
    /*************** END FORK **************/
}

/**************** BEGIN UTILITY FUNCTIONS ***************/
/* error messages stolen from: http://linux.die.net/man/2/execve */
void execve_error_handler()
{
    switch (errno)
    {
        case E2BIG:
            LOG_PRINT("The total number of bytes in the environment \
(envp) and argument list (argv) is too large.\n");
            return;
        case EACCES:
            LOG_PRINT("Execute permission is denied for the file or a \
script or ELF interpreter.\n");
            return;
        case EFAULT:
            LOG_PRINT("filename points outside your accessible address \
space.\n");
            return;
        case EINVAL:
            LOG_PRINT("An ELF executable had more than one PT_INTERP \
segment (i.e., tried to name more than one \
interpreter).\n");
            return;
        case EIO:
            LOG_PRINT("An I/O error occurred.\n");
            return;
        case EISDIR:
            LOG_PRINT("An ELF interpreter was a directory.\n");
            return;
        case ELIBBAD:
            LOG_PRINT("An ELF interpreter was not in a recognised \
format.\n");
            return;
        case ELOOP:
            LOG_PRINT("Too many symbolic links were encountered in \
resolving filename or the name of a script \
or ELF interpreter.\n");
            return;
        case EMFILE:
            LOG_PRINT("The process has the maximum number of files \
open.\n");
            return;
        case ENAMETOOLONG:
            LOG_PRINT("filename is too long.\n");
            return;
        case ENFILE:
            LOG_PRINT("The system limit on the total number of open \
files has been reached.\n");
            return;
        case ENOENT:
            LOG_PRINT("The file filename or a script or ELF interpreter \
does not exist, or a shared library needed for \
file or interpreter cannot be found.\n");
            return;
        case ENOEXEC:
            LOG_PRINT("An executable is not in a recognised format, is \
for the wrong architecture, or has some other \
format error that means it cannot be \
executed.\n");
            return;
        case ENOMEM:
            LOG_PRINT("Insufficient kernel memory was available.\n");
            return;
        case ENOTDIR:
            LOG_PRINT("A component of the path prefix of filename or a \
script or ELF interpreter is not a directory.\n");
            return;
        case EPERM:
            LOG_PRINT("The file system is mounted nosuid, the user is \
not the superuser, and the file has an SUID or \
SGID bit set.\n");
            return;
        case ETXTBSY:
            LOG_PRINT("Executable was open for writing by one or more \
processes.\n");
            return;
        default:
            LOG_PRINT("Unkown error occurred with execve().\n");
            return;
    }
}
/**************** END UTILITY FUNCTIONS ***************/