#ifndef  MAINSERVER_H
#define MAINSERVER_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h> // For sockets
#include <sys/socket.h> // For sockets
#include <netinet/in.h> // For Internet sockets
#include <netdb.h> // For gethostbyaddr
#include <stdio.h> // For I/O
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "tls_server.h"
#define THREADS 2
#define perror2(s,e) fprintf(stderr,"%s: %s\n",s,strerror(e))

void * workerFunction();
void mainThreadFunction();
void runCommands(char * path_str);

#endif
