################################################################################
# README                                                                       #
#                                                                              #
# Description: This file describes the work flow of the lisod server            #
#                                                                              #
# Authors: Yangjun Sheng(yangjuns@andrew.cmu.edu)                              #
#                                                                              #
################################################################################

This version of web server lisod uses select() function to listens to incoming
reading and writing streams:
    It maintains a read_list and a write_list:
        read_list:  HTTP server sock,
                    HTTPS server sock,
                    client socks,
                    CGI socks

        write_list: client_socks
                    CGI socks

    1) When a HTTP or HTTPS connection arrives, the sever accepts the connection
       and add the client socket to the read_list so that when client sends a
       request the server knows.

    2) When a client sock is ready to read, it reads from the sock, parses
       it into a request (or requests if it's pipelined requests). If requests
       doesn't involve using CGI, we put the client sock into write_list. In the
       meantime, the sever generates responses.
       NOTE: When we read 0 bytes from the client, it means that the client side
       closed connection, so we clean_up_client and close connection.

    3) When a client is ready to write, we find the responses that we need to
       write, and write BLOCK_SIZE bytes to the client every time until we write
       everything. At that point, we close connection and remove it from the
       write list. Otherwise, we just wait for the next time when the client is
       ready to write to.

    4) When a CGI sock is ready to read, it means that the CGI has generated
       the responses. We find out who (which client) the responses are for and
       write them to the client. After we've done that, we clean up CGI sock,
       remove it from the read list.

    5) When a CGI sock is ready to write, it means that the CGI program is
       waiting for some input(the post body) to generate response. So we find
       out what post body is it looking for and write it to the CGI sock and
       then close the sock and remove it from the write_list.
