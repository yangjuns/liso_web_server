default: liso_server example_client

liso_server: lisod.c response.c
	@gcc lisod.c logger.c y.tab.o lex.yy.o parse.c response.c daemonize.c https_socket_comb.c get_CGI_response.c -std=gnu99 -o lisod -lssl -Wall

example_client: example_client.c
	@gcc example_client.c -o example_client -Wall -Werror

clean:
	@rm lisod example_client
