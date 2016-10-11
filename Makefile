################################################################################
# Makefile                                                                     #
#                                                                              #
# Description: This file contains the make rules for Recitation 1.             #
#                                                                              #
# Authors: Athula Balachandran <abalacha@cs.cmu.edu>,                          #
#          Wolf Richter <wolf@cs.cmu.edu>                                      #
#                                                                              #
################################################################################

default: liso_server echo_client

liso_server: lisod.c response.c
	@gcc lisod.c logger.c y.tab.o lex.yy.o parse.c response.c daemonize.c https_socket_comb.c get_CGI_response.c -std=gnu99 -o lisod -lssl -Wall

echo_client: echo_client.c
	@gcc echo_client.c -o echo_client -Wall -Werror

clean:
	@rm lisod echo_client
