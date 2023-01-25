/*
gcc -c mainserver.c -lssl -lcrypto
gcc -c multithrserver.c -lssl -lcrypto
gcc -c tls_server.c -lssl -lcrypto
gcc -o server mainserver.o multithrserver.o -lpthread 
./server

rm server
rm *.o
gcc -c tls_server.c -lssl -lcrypto
gcc -c multithrserver.c -lssl -lcrypto -lpthread
gcc -c mainserver.c -lssl -lcrypto -lpthread
gcc -o server mainserver.o tls_server.o multiserver.o -lpthread -lcrypto -lssl
./server
*/
#include "mainserver.h"


//global Variables
int tcp_q;
int threads;
char *home_dir;
int port;

//*******************************************************************************

/**
*
* @brief funtion that takes a string and returns the numeric value in it
*
* @param str the input string
* @return the numeric value in the string
*/
int extract_number(char *str){

char *p = str;

while (*p) { // While there are more characters to process...
    if ( isdigit(*p) || ( (*p=='-'||*p=='+') && isdigit(*(p+1)) )) {
        // Found a number
        long val = strtol(p, &p, 10); // Read number
        return val; // and print it.
    } else {
        // Otherwise, move on to the next character.
        p++;
    }
}
return 0;
}


//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------

/**
*
* @brief funtion to read the configurstion file
*
*
*/
void read_conf(){

home_dir = malloc(65);
FILE* file = fopen("params.conf", "r");

if(!file){
printf("\n Unable to open : params.conf");
exit(-1);
}

char line[128];
char sub[16];

while (fgets(line, sizeof(line), file)) {

    memcpy(sub,&line[0],4);

    if(strcmp(sub,"THRE")==0)
        threads = extract_number(line);

    if(strcmp(sub,"PORT")==0)
        port = extract_number(line);

    if(strcmp(sub,"QUEU")==0)
        tcp_q = extract_number(line);

    if(strcmp(sub,"HOME")==0)
        memcpy(home_dir,&line[5],64);

}
fclose(file);

}


int main(int argc, char * argv []){

    read_conf();
    init();
    // reading the configuration file and loading the information
    


    int idPeople[threads];
    pthread_t tid[threads];
	  int i,err;
    for (i=0;i<threads;i++){
        idPeople[i] = i;
        if (err = pthread_create(&tid[i],NULL,&workerFunction, idPeople[i])){
            perror2("pthread_create",err);
            exit(1);
        }
    }

	mainThreadFunction(tcp_q,port,threads,home_dir);

	return 0;
}

//check gia minimata elexou

/*
https://nachtimwald.com/2019/04/12/thread-pool-in-c/
https://code-vault.net/lesson/j62v2novkv:1609958966824
https://programmer.group/c-simple-thread-pool-based-on-pthread-implementation.html
https://code-vault.net/lesson/tlu0jq32v9:1609364042686
*/
