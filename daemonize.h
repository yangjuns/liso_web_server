#ifndef DAEMONIZE_H
#define DAEMONIZE_H

/* It returns it daemonization is successfully. It exit the program if not*/
int daemonize(char *lock_file);

#endif