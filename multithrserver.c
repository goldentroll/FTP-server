#include "mainserver.h"
#include "tls_server.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <stdbool.h>
#include <libgen.h>
#include <time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>


//****************************************************************************************
// Defining all the messages that will be returned
//****************************************************************************************
#define MSG150 "150 File status okay; about to open data connection.\r\n"
#define MSG200 "200 Command okay.\r\n" //[Ακολουθεί σωστή εκτέλεση μιας εντολής όπως CWD, CDUP, LIST]
#define MSG220 "220 Service ready for new user.\r\n" // [Επιστρέφεται όταν ο πελάτης συνδεθεί επιτυχώς με τονεξυπηρετητή]
#define MSG221 "221 Service closing control connection.\r\n" //Logged out if appropriate.
#define MSG226 "226 Closing data connection. Requested file action successful.\r\n" //[Ακολουθεί και επιβεβαιώνει τη σωστή αποστολή ή λήψη αρχείου από το data connection]
#define MSG227 "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2).\r\n"// [Ακολουθεί την εντολή PASV]
#define MSG230 "230 User logged in, proceed.\r\n"// [Ακολουθεί την εντολή PASS όταν αυτή είναι επιτυχής]
#define MSG234 "234 Security data exchange complete.\r\n"
#define MSG250 "250 Requested file action okay, completed.\r\n"// [Ακολουθεί σωστή εκτέλεση της εντολής STOR]
#define MSG257 "257 PATHNAME created.\r\n" // [Ακολουθεί την εντολή MKD]
#define MSG331 "331 User name okay, need password.\r\n" // [Ακολουθεί την εντολή USER όταν αυτή είναι επιτυχής]
#define MSG332 "332 Need account for login.\r\n" //[Όταν το USER σταλεί χωρίς username]
#define MSG426 "426 Connection closed; transfer aborted.\r\n"// [Όταν διακόπτεται η μετάδοση δεδομένων απότομα]
#define MSG451 "451 Requested action aborted: local error in processing.\r\n" //[Μήνυμα λάθους για πολλές εντολές όπως CWD, CDUP, MKD, STOR, PASV, LIST, STAT κ.α.]
#define MSG500 "500 Syntax error, command unrecognized.\r\n"// [Όταν ή εντολή είναι λεκτικά ορθή αλλά συντακτικά λάθος π.χ. filename STOR αντί ανάποδα]
#define MSG502 "502 Command not implemented.\r\n"//[Όταν η εντολή που δίδεται δεν έχει υλοποιηθεί π.χ. STAR αντί STOR]
#define MSG503 "503 Bad sequence of commands.\r\n" //[Όταν δοθεί λανθασμένη εντολή που δεν αναμένεται π.χ. μετά το USER να δοθεί STOR αντί PASS]
#define MSG530 "530 Not logged in.\r\n" //[Όταν δίνονται εντολές πριν δοθεί το USER]
#define MSG550 "550 Requested action not taken. File unavailable.\r\n"// [Όταν δεν υπάρχει το ζητούμενο αρχείο π.χ. μετά την εντολή RETR]

//****************************************************************************************


//******************************************************************************************
// QUEUE CODE
//******************************************************************************************

// struct node represents ftp requests
typedef struct node{
    int control_port;                       // thread's control port e.g. 2121...
    struct node * next;
}NODE;

// struct queue is where every request is located until a thread comes
typedef struct {
    NODE * head;
    NODE * tail;
    int length;
}QUEUE;

//*******************************************************************************
// Global Variables and function declaration
//*******************************************************************************
static QUEUE * server_queue;
static pthread_mutex_t queueLock;
static pthread_cond_t emptyCV;
int count=1;
int tcp_q;
int threads;
char *home_dir;
int port;
int sock, newsock, serverlen;//, clientlen;
socklen_t clientlen;
char buf[256];
struct sockaddr_in server, client;
struct sockaddr *serverptr, *clientptr;
struct hostent *rem;
SSL_CTX *ctx;
SSL_CTX **controlctx;
SSL_CTX **datactx;
int *port_pool;
int *open_data_sockets;
int passize=6;

//----------------------------------------------------------------------------------------
//----------------------------------------------------------------------------------------

// enqueue port number => 0 for success and -1 for failure
int enqueue(int port, QUEUE * q){
    NODE * p = NULL;
    if (q == NULL){
        return -1;
    }
    p = (NODE *)malloc(sizeof(NODE));
    if (p == NULL){
        printf("System out of memory . . . \n");
        return -1;
    }
    p->control_port = port;
    p->next = NULL;
    if (q->length == 0){
        q->head = p;
        q->tail = p;
    }else{
        q->tail->next = p;
        q->tail = p;
    }
    q->length = q->length + 1;
    return 0;
}

// dequeue port number => integer for success and -1 for failure
int dequeue(QUEUE * q){          //change that
    NODE * p = NULL;
    if ((q == NULL) || (q->head == NULL)){
        printf("Sorry queue is empty . . .\n");
        return -1;
    }
    p = q->head;
    int port = q->head->control_port;
    q->head = q->head->next;
    free(p);
    q->length = q->length - 1;
    if (q->length == 0){
    	q->tail = NULL;
    }
    return port;
}

void printQueueInfo(QUEUE * q){
	printf("MESSAGE QUEUE -------------------------------------\n");
	printf("head = %d\n",q->head->control_port);
	printf("tail = %d\n",q->tail->control_port);
	printf("length = %d\n",q->length);
	printf("MESSAGE QUEUE -------------------------------------\n");
}

extern void init(){
    int i=0;
    server_queue = (QUEUE *)malloc(sizeof(QUEUE));
    server_queue->head = NULL;
    server_queue->tail = NULL;
    server_queue->length = 0;
    pthread_mutex_init(&queueLock, NULL);
    pthread_cond_init(&emptyCV, NULL);

    //init the port pool based on the number of threads
    port_pool = (int *)malloc((threads+1)*sizeof(int));
    open_data_sockets = (int *)malloc((threads+1)*sizeof(int));

    for(i=2121; i<=2121+threads; i++){
        port_pool[i-2121] = i;
    }

    datactx = malloc(threads);
    controlctx = malloc(threads);
    for(i=0; i<threads; i++){
        datactx[i] = malloc(sizeof(SSL_CTX *));
        controlctx[i] = malloc(sizeof(SSL_CTX *));
        
    }

    char dir[] = "./ftphome";

    DIR* direc = opendir(dir);
    if (direc) {
        /* Directory exists. */
        closedir(direc);
    } else if (ENOENT == errno) {
        
        struct stat st = {0};
        char *new = malloc(PATH_MAX);
        strcpy(new,dir);

        if (!(new[strlen(dir)-1] == '/')){
         strcat(new,"/");
        }

        if (stat(new, &st) == -1) {
            mkdir(new, 0700);
        }
    }
    else {
        struct stat st = {0};
        char *new = malloc(PATH_MAX);
        strcpy(new,dir);

        if (!(new[strlen(dir)-1] == '/')){
         strcat(new,"/");
        }

        if (stat(new, &st) == -1) {
            mkdir(new, 0700);
        }
    }
    
}

//******************************************************************************************
// QUEUE CODE
//******************************************************************************************


//******************************************************************************************
// PASV CODE
//******************************************************************************************

// Returns hostname for the local computer
void checkHostName(int hostname){
    if (hostname == -1){
        perror("gethostname");
        exit(1);
    }
}
  
// Returns host information corresponding to host name
void checkHostEntry(struct hostent * hostentry){
    if (hostentry == NULL){
        perror("gethostbyname");
        exit(1);
    }
}
  
// Converts space-delimited IPv4 addresses to dotted-decimal format
void checkIPbuffer(char *IPbuffer){
    if (NULL == IPbuffer){
        perror("inet_ntoa");
        exit(1);
    }
}

char* replaceWord(const char* s, const char* oldW, const char* newW){
    char* result;
    int i, cnt = 0;
    int newWlen = strlen(newW);
    int oldWlen = strlen(oldW);
  
    // Counting the number of times old word
    // occur in the string
    for (i = 0; s[i] != '\0'; i++) {
        if (strstr(&s[i], oldW) == &s[i]) {
            cnt++;
  
            // Jumping to index after the old word.
            i += oldWlen - 1;
        }
    }
  
    // Making new string of enough length
    result = (char*)malloc(i + cnt * (newWlen - oldWlen) + 1);
  
    i = 0;
    while (*s) {
        // compare the substring with the result
        if (strstr(s, oldW) == s) {
            strcpy(&result[i], newW);
            i += newWlen;
            s += oldWlen;
        }
        else
            result[i++] = *s++;
    }
  
    result[i] = '\0';
    return result;
}

int find_p_parameter(char * eight_bit_str){
    int eight_bit_int = atoi(eight_bit_str);
    int decimal_num = 0, base = 1, rem; 
    while ( eight_bit_int > 0){  
        rem = eight_bit_int % 10; /* divide the binary number by 10 and store the remainder in rem variable. */  
        decimal_num = decimal_num + rem * base;  
        eight_bit_int = eight_bit_int / 10; // divide the number with quotient  
        base = base * 2;  
    }
    return decimal_num; 
}

char* calculatePort(char port_output[],int port){
    // step 1: decimal port to 16-bit binary
    int a[16],i;      
    char *final = malloc(32);
    for(i=0;port>0;i++){    
        a[i]=port%2;    
        port=port/2;    
    }  

    // add zeros
    int j; 
    for(j=i;j<16;j++){
        a[j]=0;
    }


    // step 2: split 16-bit binary to two 8-bit binary and then transform them to two decimals
    char eight_bit_str[8] ;
    int pos=0;
    for (i=15;i>=8;i--) {
        pos += sprintf(&eight_bit_str[pos], "%d", a[i]);
    }

    int p1 = find_p_parameter(eight_bit_str);
    
    pos=0;
    for (i=7;i>=0;i--) {
        pos += sprintf(&eight_bit_str[pos], "%d", a[i]);
    }

    int p2 = find_p_parameter(eight_bit_str);

    sprintf(final,"%d,%d",p1,p2);

    return final;
}



//******************************************************************************************
// PASV CODE
//******************************************************************************************



//******************************************************************************************
// COMMANDS
//******************************************************************************************

int ACCOUNTS = 3;
char usernames_table[][10] = {"cgeorg08","ahadji08","AH"};
char passwords_table[][10] = {"123456","789012", "AHAHAH"};
char cwd[PATH_MAX];
SSL *connection;

int userFunction(char * username, SSL *connection){
    int i;
    //printf("username: %s\n",username);
    for (i=0;i<ACCOUNTS;i++){
        if (strcmp(username,usernames_table[i]) == 0){
            SSL_write(connection, MSG331, strlen(MSG331));
            return i;
        }
    }
    SSL_write(connection, MSG332, strlen(MSG332));
    return -1;
}


char* cwdFunction(char *curdir, char *dir, SSL *connection){

    char *temp = malloc(PATH_MAX);
    strcpy(temp,curdir);


    if (dir[0] != '/'){
        if(curdir[strlen(curdir)-1] == '/')
            strcat(curdir,dir);
        else{
            strcat(curdir,"/");
            strcat(curdir,dir);
    }
    }
    else
        strcpy(curdir,dir);


    //printf("dir out: %s \n",curdir);

    DIR* direc = opendir(curdir);
    if (direc) {
        /* Directory exists. */
        closedir(direc);
    } else if (ENOENT == errno) {
        /* Directory does not exist. */
        SSL_write(connection, MSG550, strlen(MSG550));
        return temp;
    } else {
        /* opendir() failed for some other reason. */
        SSL_write(connection, MSG550, strlen(MSG550));
        return temp;
    }

    SSL_write(connection, MSG200, strlen(MSG200));
    return curdir; 
}

char* cdupFunction(char *dir, SSL *connection){
    char path [PATH_MAX];
    char * parent_name = dirname(dir);     // get the parent directory
    SSL_write(connection, MSG200, strlen(MSG200));
    return parent_name;
}


char* enterPassive(int tid, SSL *ssl){

    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    int hostname;
  
    // To retrieve hostname
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    checkHostName(hostname);
  
    // To retrieve host information
    host_entry = gethostbyname(hostbuffer);
    checkHostEntry(host_entry);
  
    // To convert an Internet network address into ASCII string
    IPbuffer = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));
  
    //printf("Hostname: %s\n", hostbuffer);
    printf("Host IP: %s\n", IPbuffer);


    // port calculating
    int port = port_pool[tid];
    char *port_output = calculatePort(port_output,port);

   // printf("port: %d   tid:%d port_output: %s \n", port,tid,port_output);


    //form output
    char ipForm [100];
    strcpy(ipForm,replaceWord(IPbuffer, ".", ","));

    char *output = malloc(128);
    //printf("ip: %s         port: %s\n", ipForm, port_output);
    //sprintf(output,"227 Entering Passive Mode (%s,%s).",ipForm,port_output);
    sprintf(output,"227 Entering Passive Mode (%s,%s).\r\n",ipForm,port_output);

    //printf("%s\n", output);

    SSL_write(ssl, output, strlen(output));

    
    return output;

}


char* readSSL(SSL *ssl){

    int flag = 1;
    char *buf = malloc(512000);

    while(flag ){

        int maxlen = PATH_MAX + 6;
        int read =  SSL_read(ssl, buf, maxlen);

        //printf("return vlue: %d", read);
        //printf("buf %s\n",buf);

        if(read < 0)
            continue;
        else 
            flag = 0;
    
    }

    return buf;
}


void storeFunction(char *dir, char *pathname, SSL *connection, SSL *data){

    char cur_path [PATH_MAX];
    
    if (pathname[0] != '/'){
        strcpy(cur_path,dir);
        if(dir[strlen(dir)-1] == '/')
            strcat(cur_path,pathname);
        else{
            strcat(cur_path,"/");
            strcat(cur_path,pathname);
    }
    }
    else{
        strcpy(cur_path,pathname);
    }

    FILE * fp = NULL;
    if ((fp = fopen(cur_path,"w")) == NULL){
        printf("Error: unable to open %s\n",cur_path);
        exit(-1);
    }

    char *mystr = readSSL(data);
    //printf("%s \n", mystr);
    fprintf(fp, "%s", mystr); // write to file

    SSL_write(connection, MSG250, strlen(MSG250));
    SSL_write(connection, MSG226, strlen(MSG226));
    if (fclose(fp)){
        perror(cur_path);
        return;
    }

}

void retrFunction(char* dir, char * pathname, SSL *connection, SSL *data){

    char cur_path [PATH_MAX];
    char line[512] = {0};
    unsigned int line_count = 0;

    if (pathname[0] != '/'){
        strcpy(cur_path,dir);
        if(dir[strlen(dir)-1] == '/')
            strcat(cur_path,pathname);
        else{
            strcat(cur_path,"/");
            strcat(cur_path,pathname);
    }
    }
    else{
        strcpy(cur_path,pathname);
    }

    FILE * fp = NULL;
    if ((fp = fopen(cur_path,"r")) == NULL){
        printf("Error: unable to open %s\n",cur_path);
        exit(-1);
    }

    while (fgets(line, 512, fp)){
        /* Print each line */
       SSL_write(data, line, strlen(line));
       //printf("%s \n",line);
    }

    SSL_write(connection, MSG250, strlen(MSG250));
    SSL_write(connection, MSG226, strlen(MSG226));

    if (fclose(fp)){
        perror(cur_path);
        return;
    }   

}


void listFunction(char* dir, SSL *connection, SSL *data){
	DIR * dp;
	struct dirent *entry;
	struct stat statbuf;

	if((dp = opendir(dir)) == NULL) {
		perror(dir);
		return;
	}

	while((entry = readdir(dp)) != NULL) {

        char* path = malloc(PATH_MAX);

        strcpy(path,dir);;
        strcat(path,"/");
        strcat(path,entry->d_name);

        //printf("directory: %s\n",path);

        //if (!(strcmp(entry->d_name,".") == 0 || strcmp(entry->d_name,"..") == 0))  {
	    if (stat(path,&statbuf) < 0){printf("error : %s\n", entry->d_name);}
        //    printf("error : %s\n", entry->d_name);
		//	perror("lstat error");

      
        // 1. type
        char type [2] = {0};
        if (S_ISBLK(statbuf.st_mode))
            strcpy(type,"b");
        else if (S_ISCHR(statbuf.st_mode))
            strcpy(type,"c");
        else if (S_ISFIFO(statbuf.st_mode))
            strcpy(type,"p");
        else if (S_ISREG(statbuf.st_mode))
            strcpy(type,"-");
        else if (S_ISLNK(statbuf.st_mode))
            strcpy(type,"l");
        else if (S_ISSOCK(statbuf.st_mode))
            strcpy(type,"s");
        else if (S_ISDIR(statbuf.st_mode))
            strcpy(type,"d");


       // printf("mode: %s \n",type);

        // 2. permissions
        char permissions [9] = {0};
        (statbuf.st_mode & S_IRUSR) ? strcat(permissions,"r") : strcat(permissions,"-");
        (statbuf.st_mode & S_IWUSR) ? strcat(permissions,"w") : strcat(permissions,"-");
        (statbuf.st_mode & S_IXUSR) ? strcat(permissions,"x") : strcat(permissions,"-");
        (statbuf.st_mode & S_IRGRP) ? strcat(permissions,"r") : strcat(permissions,"-");
        (statbuf.st_mode & S_IWGRP) ? strcat(permissions,"w") : strcat(permissions,"-");
        (statbuf.st_mode & S_IXGRP) ? strcat(permissions,"x") : strcat(permissions,"-");
        (statbuf.st_mode & S_IROTH) ? strcat(permissions,"r") : strcat(permissions,"-");
        (statbuf.st_mode & S_IWOTH) ? strcat(permissions,"w") : strcat(permissions,"-");
        (statbuf.st_mode & S_IXOTH) ? strcat(permissions,"x") : strcat(permissions,"-");

        // 3. number of links
        // get the number of links with statbuf.st_nlink

        // 4. owner
        struct passwd *pw = getpwuid(statbuf.st_uid); //get the user name with pw->pw_name

        // 5. group
        struct group  *gr = getgrgid(statbuf.st_gid); //get the group name with gr->gr_name

        // 6. size
        // get the file size with statbuf.st_size


        char date[5][100] = {0};
        int i=0;
        char * token = strtok(ctime(&statbuf.st_mtime), " ");
        // loop through the string to extract all other tokens
        while( token != NULL ) {
        strcpy(date[i],token);
        token = strtok(NULL, " ");
        i++;
        }

        char *hour = malloc(16);
        strncpy(hour,date[3],5);
        hour[5] = '\0';

        // 7. + 8. modification time and date
        // get the modification time and date with ctime(&statbuf.st_mtime);
        // 9. name
        //entry->d_name
        char *out = malloc(PATH_MAX);
   
        sprintf(out,"%s%s %d %s %s %d %s %s %s %s",type,permissions,statbuf.st_nlink,pw->pw_name,gr->gr_name,statbuf.st_size,date[1],date[2],hour,entry->d_name);
        char *new_output = malloc(PATH_MAX);
        strcpy(new_output,replaceWord(out, "\n", ""));
        strcat(new_output,"\r\n");
        //printf("%s\n", new_output);
        SSL_write(data, new_output, strlen(new_output));

    }

    SSL_write(connection, MSG200, strlen(MSG200));
    SSL_write(connection, MSG226, strlen(MSG226));

	closedir(dp);

}


void statFunction(char* dir, SSL *connection){
	DIR * dp;
	struct dirent *entry;
	struct stat statbuf;

	if((dp = opendir(dir)) == NULL) {
		perror(dir);
		return;
	}


	while((entry = readdir(dp)) != NULL) {

		if (lstat(entry->d_name,&statbuf) < 0){}
			//perror("lstat error");
		//}

        // 1. type
        char type [1];
        if (S_ISBLK(statbuf.st_mode))
            strcpy(type,"b");
        else if (S_ISCHR(statbuf.st_mode))
            strcpy(type,"c");
        else if (S_ISDIR(statbuf.st_mode))
            strcpy(type,"d");
        else if (S_ISFIFO(statbuf.st_mode))
            strcpy(type,"p");
        else if (S_ISREG(statbuf.st_mode))
            strcpy(type,"-");
        else if (S_ISLNK(statbuf.st_mode))
            strcpy(type,"l");
        else if (S_ISSOCK(statbuf.st_mode))
            strcpy(type,"s");

        // 2. permissions
        char permissions [9] = "";
        (statbuf.st_mode & S_IRUSR) ? strcat(permissions,"r") : strcat(permissions,"-");
        (statbuf.st_mode & S_IWUSR) ? strcat(permissions,"w") : strcat(permissions,"-");
        (statbuf.st_mode & S_IXUSR) ? strcat(permissions,"x") : strcat(permissions,"-");
        (statbuf.st_mode & S_IRGRP) ? strcat(permissions,"r") : strcat(permissions,"-");
        (statbuf.st_mode & S_IWGRP) ? strcat(permissions,"w") : strcat(permissions,"-");
        (statbuf.st_mode & S_IXGRP) ? strcat(permissions,"x") : strcat(permissions,"-");
        (statbuf.st_mode & S_IROTH) ? strcat(permissions,"r") : strcat(permissions,"-");
        (statbuf.st_mode & S_IWOTH) ? strcat(permissions,"w") : strcat(permissions,"-");
        (statbuf.st_mode & S_IXOTH) ? strcat(permissions,"x") : strcat(permissions,"-");

        // 3. number of links
        // get the number of links with statbuf.st_nlink

        // 4. owner
        struct passwd *pw = getpwuid(statbuf.st_uid); //get the user name with pw->pw_name

        // 5. group
        struct group  *gr = getgrgid(statbuf.st_gid); //get the group name with gr->gr_name

        // 6. size
        // get the file size with statbuf.st_size

        // 7. + 8. modification time and date
        // get the modification time and date with ctime(&statbuf.st_mtime);

        // 9. name
        //entry->d_name
        char *out = malloc(100);
        sprintf(out,"%s%s %d %s %s %d %s %s",type,permissions,statbuf.st_nlink,pw->pw_name,gr->gr_name,statbuf.st_size,ctime(&statbuf.st_mtime),entry->d_name);
        char *new_output= malloc(100);
        strcpy(new_output,replaceWord(out, "\n", ""));
        //printf("%s\n", new_output);
        SSL_write(connection, new_output, strlen(new_output));

    }

    SSL_write(connection, MSG200, strlen(MSG200));

	closedir(dp);

}

void mkdFunction(char *dir , char * name, SSL *connection){
    struct stat st = {0};
    char *new = malloc(PATH_MAX);
    strcpy(new,dir);

    if (!(new[strlen(dir)-1] == '/')){
        strcat(new,"/");
    }
    
    strcat(new,name);

    if (stat(new, &st) == -1) {
        mkdir(new, 0700);
    }

     SSL_write(connection, MSG257, strlen(MSG257));

}


//******************************************************************************************
// DATA SOCKET
//******************************************************************************************



/**
*
* @brief funtion to run the program
*
* This function initializes and creates a TLS socket to send and receive
* data from the client.
*
* @param port the port that the data socket will listen to
*
*/
SSL* data_socket(int tid, SSL *ssl){

int flag = 1;
int port = port_pool[tid];
/* initialize OpenSSL */
init_openssl();



/* setting up algorithms needed by TLS */
datactx[tid] = create_context();

/* specify the certificate and private key to use */
configure_context(datactx[tid]);
 /* The AF_INET address family is the address family for IPv4, AF_INET6 for IPv6, AF_BLUETOOTH for Bluetooth */
 /* IPPROTO_TCP is chosen for the TCP protocol if the type was set to SOCK_STREAM */
 /* IPPROTO_UDP is chosen for the UDP protocol if the type was set to SOCK_DGRAM */
 if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
     /* Create socket */
     perror("socket");
     exit(999);
 }

 server.sin_family = AF_INET; /* Internet domain */
 /* When INADDR_ANY (0.0.0.0) is specified, the socket will be bound to all local interfaces */
 /* INADDR_LOOPBACK (127.0.0.1) always refers to the local host via the loopback device */
 server.sin_addr.s_addr = htonl(INADDR_ANY); /* My Internet address */
 server.sin_port = htons(port); /* a random port from the pool */
 serverptr = (struct sockaddr *) &server;
 serverlen = sizeof server;


 if (bind(sock, serverptr, serverlen) < 0){
     /* Bind socket to an address */
     perror("bind");
     exit(1);
    }


 /* Waiting queue for incoming TCP connections (second param is capacity) */
 if (listen(sock, 1) < 0)
    { /* Listen for connections */
     perror("listen");
     exit(1);
    }

 printf("Listening for data on port %d\n", port);

 //port is open and send the data to the client
 char *msg = enterPassive(tid,ssl);

 //printf("message: %s\n",msg);
 //SSL_write(ssl, msg, strlen(msg));

 /* Handle connections */
        while(flag) {
            struct sockaddr_in addr;
            uint len = sizeof(addr);
            SSL *data;

    	/* Server accepts a new connection on a socket.
             * Server extracts the first connection on the queue
             * of pending connections, create a new socket with the same
             * socket type protocol and address family as the specified
             * socket, and allocate a new file descriptor for that socket.
             */
            int client = accept(sock, (struct sockaddr*)&addr, &len);
            open_data_sockets[tid]=client;
            if (client < 0) {
                perror("Unable to accept");
                exit(EXIT_FAILURE);
            }

            /* creates a new SSL structure which is needed to hold the data
             * for a TLS/SSL connection
             */
            data = SSL_new(datactx[tid]);
            SSL_set_fd(data, client);

            /* wait for a TLS/SSL client to initiate a TLS/SSL handshake */
            if (SSL_accept(data) <= 0) {
                ERR_print_errors_fp(stderr);
            }
            /* if TLS/SSL handshake was successfully completed, a TLS/SSL
             * connection has been established
             */
            else {
                
                flag = 0;
            }

           
            return data;
            /* free an allocated SSL structure */
            SSL_free(data);
            close(client);
        }

        //close(sock);
        //SSL_CTX_free(ctx);
        //cleanup_openssl();

    // Main server is listening to requests
}


//******************************************************************************************
// DATA SOCKET
//******************************************************************************************





//******************************************************************************************
// COMMANDS
//******************************************************************************************



void mainThreadFunction(int q,int p,int th,char *hm){

    tcp_q = q;
    port = p;
    threads = th;
    home_dir = hm;
    char *out = malloc(128);


    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    //Creating the main socket in the given port


    // initialize OpenSSL 
    init_openssl();

    /* setting up algorithms needed by TLS */
    ctx = create_context();

    /* specify the certificate and private key to use */
    configure_context(ctx);


    

     /* The AF_INET address family is the address family for IPv4, AF_INET6 for IPv6, AF_BLUETOOTH for Bluetooth */
     /* IPPROTO_TCP is chosen for the TCP protocol if the type was set to SOCK_STREAM */
     /* IPPROTO_UDP is chosen for the UDP protocol if the type was set to SOCK_DGRAM */
     if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
         /* Create socket */
         perror("socket");
         exit(1);
     }

     server.sin_family = AF_INET; /* Internet domain */
     /* When INADDR_ANY (0.0.0.0) is specified, the socket will be bound to all local interfaces */
     /* INADDR_LOOPBACK (127.0.0.1) always refers to the local host via the loopback device */
     server.sin_addr.s_addr = htonl(INADDR_ANY); /* My Internet address */
     server.sin_port = htons(port); /* The given port */
     serverptr = (struct sockaddr *) &server;
     serverlen = sizeof server;

     if (bind(sock, serverptr, serverlen) < 0)
        { /* Bind socket to an address */
         perror("bind");
         exit(1);
        }


     /* Waiting queue for incoming TCP connections (second param is capacity) */
     if (listen(sock, tcp_q) < 0)
        { /* Listen for connections */
         perror("listen");
         exit(1);
        }

     printf("Listening for connections to port %d\n", port);

    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    int con = 0;
    /* Handle connections */
        while(1) {


            con++;
            struct sockaddr_in addr;
            uint len = sizeof(addr);


    	/* Server accepts a new connection on a socket.
             * Server extracts the first connection on the queue
             * of pending connections, create a new socket with the same
             * socket type protocol and address family as the specified
             * socket, and allocate a new file descriptor for that socket.
             */
            int client = accept(sock, (struct sockaddr*)&addr, &len);

            if (client < 0) {
                perror("Unable to accept");
                exit(EXIT_FAILURE);
            }

            send(client, MSG220, strlen(MSG220), 0);
            send(client, MSG234, strlen(MSG234), 0);


            char *buffer = malloc(1024);

            int valread = read(client, buffer, 1024);

            while (!(valread>0)){
                int valread = read(client, buffer, 1024);
                //printf("buffer return value: \n");
            }
            
            

            //printf("buffer return value: %d \n", valread);
            
            if(valread>0)
                printf("buffer: %s \n", buffer);


            // queue
                pthread_mutex_lock(&queueLock);

                    sprintf(out,"MAIN BIRNGS ITEM! QUEUE LENGTH = %d \n" ,server_queue->length);
                    printf("%s",out);
                    enqueue(client,server_queue);    // HADJIS : port (replace count with number port)

                    
                    fflush(stdin);
                    
                    sprintf(out,"MAIN PUTS ITEM! QUEUE LENGTH = %d \n\n" ,server_queue->length);
                    printf("%s",out);
                    pthread_cond_signal(&emptyCV);
                
                pthread_mutex_unlock(&queueLock);

                printf("Listening for connections to port %d\n", port);

        }

        close(sock);
        SSL_CTX_free(ctx);
        cleanup_openssl();    

}


char* checkpass(char *str){

    int e=0;

    char *temp = malloc(64);

    //printf("string: %s  len: %d\n", str, strlen(str));


    if(str == NULL)
        return 0;
        
    if (strlen(str)==0)
        return 0;

    for (int j = 0; j < strlen(str); j++){
        int u = isprint(str[j]);
            if(u==0){
                e=1;
                break;
            }
        strncat(temp,&str[j],1);
    }
    
//printf("string out: %s\n", temp);
return temp;
}

void closeConnection(SSL *data, int tid){

        SSL_shutdown(data);
        SSL_free(data);
        SSL_CTX_free(datactx[tid]);
        close(open_data_sockets[tid]);
        port_pool[tid] = port_pool[tid]+threads+2;

        printf("data socket closed!\n");

}


void * workerFunction(void *threadid){

    char *dir = malloc(PATH_MAX);
    int flag = 1;
    int exit = 0;
    int args = 0;
    int current_account = -1;
    int tid;
    tid = (int)threadid;
    SSL *data;
    char *out = malloc(128);
    char cwd[PATH_MAX];

    //printf("tid is: %d\n",tid);


    while(1){

            // queue
            pthread_mutex_lock(&queueLock);


            while(server_queue->length == 0){
                sprintf(out,"WORKER THREAD BLOCKED --> %u\n",pthread_self());
                printf("%s",out);
                pthread_cond_wait(&emptyCV , &queueLock);
            }

            sprintf(out,"WORKER THREAD %u! QUEUE LENGTH = %d \n" ,pthread_self(),server_queue->length);
            printf("%s",out);
            int client = dequeue(server_queue);

            sprintf(out,"WORKER THREAD %u TAKES ITEM %d! QUEUE LENGTH = %d \n\n" ,pthread_self(),client,server_queue->length);
            printf("%s",out);

            pthread_mutex_unlock(&queueLock);

            SSL *ssl;

            /* creates a new SSL structure which is needed to hold the data
            * for a TLS/SSL connection
            */
            ssl = SSL_new(ctx);
            SSL_set_fd(ssl, client);

            /* wait for a TLS/SSL client to initiate a TLS/SSL handshake */
            if (SSL_accept(ssl) <= 0) {
                ERR_print_errors_fp(stderr);
                flag=0;
                exit = 1;
            }
            /* if TLS/SSL handshake was successfully completed, a TLS/SSL
             * connection has been established
             */

            while (flag){
                
            int maxlen = PATH_MAX + 6;
            char buf[4102] = {0};
            int read =  SSL_read(ssl, buf, maxlen);


            if (buf == NULL || strlen(buf) == 0){
 
                SSL_shutdown(ssl);
                SSL_free(ssl);
                close(client);
                flag = 0;
                break;          
            }


            char new[128] = {0};
            
            for (size_t i = 0; i < strlen(buf); i++){

                  if((buf[i] != '\r') && (buf[i] != '\n') && ((int)buf[i] >0) )
                    strncat(new,&buf[i],1);
                
            }
            strcat(new,"\0");


            if(read < 0)
                continue;

            int check =0;
            int delchar = 0;

            //printf("buf\n");
            for (size_t i = 0; i < strlen(new); i++){
                if((int)new[i]==32){
                    check =1;
                    break;
                }
            }

            char * command = malloc(PATH_MAX);
            char *value = malloc(PATH_MAX);
            
            if(check){
            // 1. tokenize the input
                command = strtok(new," ");  // get the first token
                command[strcspn(command, "\n")] = 0;    // removing \n from the command string
            }
            else{
                command = strtok(new, "\r"); 

                for (size_t i = 0; i < strlen(command); i++){

                if((int)command[i]==127){
                    delchar =i;
                    break;
                }
            }
                if(delchar){
                    command[delchar] = '\0';
                }
   
            }


            if (!((strcmp(command,"CDUP") == 0) || (strcmp(command,"QUIT") == 0) || (strcmp(command,"PWD") == 0)|| (strcmp(command,"FEAT") == 0) ||(strcmp(command,"SYST") == 0) ||(strcmp(command,"STAT") == 0) || (strcmp(command,"PASV") == 0) || (strcmp(command,"LIST") == 0))){
                value = strtok(NULL, " ");          // get the second token
                value[strcspn(value, "\n")] = 0;    // removing \n from the value string
            }
            

            char val[128] = {0};

            if(value != NULL && strlen(value)>0){
            if((strlen(value)-1) == 0){
                strncpy(val,value, 1);
                val[1] = '\0';

            }else if(strcmp(command,"CWD") == 0){
                strncpy(val,value, strlen(value));
                val[strlen(value)] = '\0';
            
            }else if(strcmp(command,"RETR") == 0 || strcmp(command,"STOR") == 0 ||  strcmp(command,"MKD") == 0 || strcmp(command,"USER") == 0 ||  strcmp(command,"PASS") == 0){
                strcpy(val,value);
            }
            else{
                strncpy(val,value, strlen(value)-1);
                val[strlen(value)-1] = '\0';
            }
            
            }
            printf("command is %s  value is %s \n", command,val);

            // 2. do the action according to the command
            if (strcmp(command,"USER") == 0){
                current_account = userFunction(val,ssl);

                if (current_account >=0){   
                  getcwd(cwd, sizeof(cwd));
                  strcpy(dir, cwd);
                  strcat(dir, "/ftphome/"); 
                  strcat(dir, val);  

                DIR* direc = opendir(dir);
                if (direc) {
                    /* Directory exists. */
                    closedir(direc);
                } else if (ENOENT == errno) {
                    
                    struct stat st = {0};
                    char *new = malloc(PATH_MAX);
                    strcpy(new,dir);

                    if (!(new[strlen(dir)-1] == '/')){
                    strcat(new,"/");
                    }

                    if (stat(new, &st) == -1) {
                        mkdir(new, 0700);
                    }
                }
                else {
                    struct stat st = {0};
                    char *new = malloc(PATH_MAX);
                    strcpy(new,dir);

                    if (!(new[strlen(dir)-1] == '/')){
                    strcat(new,"/");
                    }

                    if (stat(new, &st) == -1) {
                        mkdir(new, 0700);
                    }
                }

                }

            }

            else if (strcmp(command,"PASS") == 0){

                char *ps= malloc(passize+1);
                strncpy(ps,val,passize);
                ps[passize]='\0';
               if (strcmp(ps,passwords_table[current_account]) == 0){
                    SSL_write(ssl, MSG230, strlen(MSG230));
                }else{
                    SSL_write(ssl, MSG530, strlen(MSG530));
                }
            }

            else if (strcmp(command,"CWD") == 0){
                strcpy(dir,cwdFunction(dir,val,ssl));

            }

            else if (strcmp(command,"CDUP") == 0){
                strcpy(dir,cdupFunction(dir,ssl));
            }

            else if (strcmp(command,"QUIT") == 0){
                exit = 1;
            }

            else if (strcmp(command,"PASV") == 0){ 
               data = data_socket(tid,ssl);           
                
            }

            else if (strcmp(command,"RETR") == 0){
                retrFunction(dir,val,ssl,data);
                closeConnection(data,tid);
                
            }

            else if (strcmp(command,"STOR") == 0){
                storeFunction(dir,val, ssl, data);
                closeConnection(data,tid);
            }

            else if (strcmp(command,"LIST") == 0){
                listFunction(dir,ssl,data);
                closeConnection(data,tid);
            }

            else if (strcmp(command,"MKD") == 0){
                mkdFunction(dir,val,ssl);
            }

            else if (strcmp(command,"STAT") == 0){
                statFunction(dir, ssl);
            }
        
    //*******************************************************************
            else if(strcmp(command,"PBSZ") == 0){
                SSL_write(ssl, "200 PBSZ set to 0.\r\n", strlen("200 PBSZ set to 0.\r\n"));
            }
            else if(strcmp(command,"PROT") == 0){
                SSL_write(ssl, "200 PROT now Private.\r\n", strlen("200 PROT now Private.\r\n"));
            }
            
            else if (strcmp(command,"FEAT") == 0){

                SSL_write(ssl, "211-Extensions supported.\r\n", strlen("211-Extensions supported.\r\n"));
                SSL_write(ssl, "AUTH TLS.\r\n", strlen("AUTH TLS.\r\n"));
                SSL_write(ssl, "PBSZ.\r\n", strlen("PBSZ.\r\n"));
                SSL_write(ssl, "PROT.\r\n", strlen("PROT.\r\n"));
                SSL_write(ssl, "211 END.\r\n", strlen("211 END.\r\n"));
            }
                
            else if (strcmp(command,"SYST") == 0){ 
                SSL_write(ssl, "UNIX\r\n", strlen("UNIX\r\n"));
            }

            else if (strcmp(command,"TYPE") == 0){ 
                SSL_write(ssl, MSG200, strlen(MSG200));
            }
            
            else if (strcmp(command,"PWD") == 0){ 
                char ans[PATH_MAX]={0};
                strcat(ans,"257 \"");
                strcat(ans,dir);
                strcat(ans,"\" created.\r\n");
                //strcat(ans,"\r\n");

                SSL_write(ssl, ans, strlen(ans));
            }
    //********************************************************************
            else {
                SSL_write(ssl, MSG502, strlen(MSG502));
            }

                    
            if (exit){
                flag = 0;
                /* free an allocated SSL structure */
                SSL_free(ssl);
                close(client);
            }

         }
        

        }

        

}
