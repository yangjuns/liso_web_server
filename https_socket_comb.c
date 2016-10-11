#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/select.h>

/* OpenSSL headers */
#include <openssl/ssl.h>
#include <openssl/err.h>

/* functions provide by others*/
#include "logger.h"
#include "lisod.h"
#include "param.h"

/* functions you provide */
#include "https_socket_comb.h"

char * client_ip_addr[20];
fd_set  https_client_list;

void comb_init(char * private_key_file, char * public_key_file){
  SSL_load_error_strings();
  SSL_library_init();
  FD_ZERO(&https_client_list);

  /* we want to use TLSv1 only */
  if ((ssl_context = SSL_CTX_new(TLSv1_server_method())) == NULL){
      LOG_PRINT("Error creating SSL context.\n");
      exit(EXIT_FAILURE);
  }

  /* register private key */
  if (SSL_CTX_use_PrivateKey_file(ssl_context, private_key_file,
                                  SSL_FILETYPE_PEM) == 0){
      SSL_CTX_free(ssl_context);
      LOG_PRINT("Error associating private key.\n");
      exit(EXIT_FAILURE);
  }

  /* register public key (certificate) */
  if (SSL_CTX_use_certificate_file(ssl_context, public_key_file,
                                   SSL_FILETYPE_PEM) == 0){
      SSL_CTX_free(ssl_context);
      LOG_PRINT("Error associating certificate.\n");
      exit(EXIT_FAILURE);
  }
}

int comb_create_sock(int http_port, int is_secure){
  /* all networked programs must create a socket */
  int sock;
  struct sockaddr_in addr;
  if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
      LOG_PRINT( "Failed creating socket.\n");
      if(is_secure == HTTPS) SSL_CTX_free(ssl_context);
      LOG_PRINT("Failed creating the sever. Shutting down...");
      exit(EXIT_FAILURE);
  }
  LOG_PRINT("Sever created successfully");
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons(http_port);
  addr.sin_addr.s_addr = INADDR_ANY;

  /* servers bind sockets to ports---notify the OS they accept connections */
  if (bind(sock, (struct sockaddr *) &addr, sizeof(addr))) {
      close_socket(sock);
      LOG_PRINT( "Failed binding socket.\n");
      if(is_secure == HTTPS) SSL_CTX_free(ssl_context);
      LOG_PRINT("Failed binding sever to port %d. Shutting down...",
        http_port);
      exit(EXIT_FAILURE);
  }
  LOG_PRINT("Sever bind to port %d successfully", http_port);

  if (listen(sock, 5)) {
      close_socket(sock);
      LOG_PRINT( "Error listening on socket.\n");
      if(is_secure == HTTPS) SSL_CTX_free(ssl_context);
      LOG_PRINT("Failed listening on clients. Shutting down...");
      exit(EXIT_FAILURE);
  }
  LOG_PRINT("Sever %d listening on port %d ...", sock, http_port);
  return sock;
}

int comb_accept(int is_secure, int sock){
  socklen_t cli_size;
  struct sockaddr_in cli_addr;
  int client_sock;
  SSL *client_context;
  cli_size = sizeof(cli_addr);
  if ((client_sock = accept(sock, (struct sockaddr *) &cli_addr,
       &cli_size)) == -1){
      close(sock);
      if(is_secure) SSL_CTX_free(ssl_context);
      LOG_PRINT( "Error accepting connection.\n");
      return EXIT_FAILURE;
  }
  char* client_ip = malloc(40);   //need to free in the future

  sprintf(client_ip, "%d.%d.%d.%d",
    (int)cli_addr.sin_addr.s_addr&0xFF,
    (int)(cli_addr.sin_addr.s_addr&0xFF00)>>8,
    (int)(cli_addr.sin_addr.s_addr&0xFF0000)>>16,
    (int)(cli_addr.sin_addr.s_addr&0xFF000000)>>24);
  client_ip_addr[client_sock] = client_ip;
  /************ WRAP SOCKET WITH SSL if it's HTTPS connection ************/
  if(is_secure){
    LOG_PRINT("Server accepted a HTTPS connection from client %d.",
      client_sock);
    if ((client_context = SSL_new(ssl_context)) == NULL){
        LOG_PRINT( "Error creating client SSL context.\n");
        return EXIT_FAILURE;
    }

    if (SSL_set_fd(client_context, client_sock) == 0){
        SSL_free(client_context);
        LOG_PRINT("Error creating client SSL context.\n");
        return EXIT_FAILURE;
    }

    if (SSL_accept(client_context) <= 0){
        SSL_free(client_context);
        LOG_PRINT("Error accepting (handshake) client SSL context.\n");
        return EXIT_FAILURE;
    }
    /************ END WRAP SOCKET WITH SSL ************/
    LOG_PRINT("Add client %d to the https_client_list", client_sock);
    FD_SET(client_sock, &https_client_list);
    //record clients_context for future reference;
    https_client_contexts[client_sock] = client_context;
  }else{
    LOG_PRINT("Server accepted a HTTP connection from client %d.",
      client_sock);
  }
  return client_sock;
}

int comb_read(int client_sock, void * buf, int size){
  int readret;
  if(FD_ISSET(client_sock, &https_client_list)){
    if((readret = SSL_read(https_client_contexts[client_sock], buf, size)) == -1){
      SSL * client_context = https_client_contexts[client_sock];
      SSL_shutdown(client_context);
      SSL_free(client_context);
      https_client_contexts[client_sock] = NULL;
      FD_CLR(client_sock, &https_client_list);
    }
    LOG_PRINT("read %d bytes", readret);
    return readret;
  }else{
    readret = recv(client_sock, buf, size, 0);
    LOG_PRINT("read %d bytes", readret);
    return readret;
  }
}

int comb_write(int client_sock, void * buf, int size){
  int readret;
  if(FD_ISSET(client_sock, &https_client_list)){
    if((readret = SSL_write(https_client_contexts[client_sock], buf, size)) == -1){
      SSL * client_context = https_client_contexts[client_sock];
      SSL_shutdown(client_context);
      SSL_free(client_context);
      https_client_contexts[client_sock] = NULL;
      FD_CLR(client_sock, &https_client_list);
    }
    return readret;
  }else{
    return send(client_sock, buf, size, 0);
  }
}

void comb_free(){
  for(int i = 0 ;i< HTTPS_CLIENT_SIZE; i++){
    if(https_client_contexts[i]){
      SSL_shutdown(https_client_contexts[i]);
      SSL_free(https_client_contexts[i]);
    }
  }
  SSL_CTX_free(ssl_context);
}