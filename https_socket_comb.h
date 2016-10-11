/*  You need to initialize
 *
 */
#ifndef HTTPS_SOCKET_COMB_H
#define HTTPS_SOCKET_COMB_H

/* OpenSSL headers */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/select.h>

#define HTTP 0
#define HTTPS 1
#define HTTPS_CLIENT_SIZE 1024
SSL_CTX * ssl_context;
fd_set  https_client_list;
SSL * https_client_contexts[HTTPS_CLIENT_SIZE];
void comb_init(char * private_key_file, char * public_key_file);
int comb_create_sock(int port, int is_secure);
int comb_accept(int is_secure, int sever_sock);
int comb_read(int client_sock, void * buf, int size);
int comb_write(int client_sock, void * buf, int size);
void comb_free();

#endif