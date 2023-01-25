# FTP-server

###Implementation of an FTP server with the following features:
+ Programming language and libraries: C language - TLS/SSL protocol - openssl library
+ Parallelism: multithreading process
+ Security: TLS channels and sockets
+ Comparible with FileZilla 

###How to use this software:
1. Install Filezilla ftp client.
To download filezilla follow the link: https://filezilla-project.org/download.php

2. First, the configuration file must be filled with the parameters needed.
In file params.conf there are 4 parameters that must be configured or left 
as they are with the default values: Number of threads -> THREADS (10) , 
port number -> PORT (2525), the maximum size in the tcp queue requests ->
QUEUE (15) , home directory where all the clients files will be stored ->
HOME (./ftphome);

3. Once Filezilla is loaded and the configuration file is set, in the connection bar
at the top in Filezilla in the Host section fill the domain name or the ip address 
of the machine that the server is running on, in the username and password sections
fill the ftp credentials for the user and in the port section the port number that 
the server is listening to. Then press quick connect and the connection is now 
established ready to transfer files.



--- Attention: ---

** Step 2 : In parenthesis are all the default values.

** Step 2 : For the port choice we choose the port 2525 and not the default 21 
since it is a reserved port and root access to the machine needed to use port 
21. Thus, the server uses a non reserved port by default to prevent failures.
