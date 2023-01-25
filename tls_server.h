#ifndef TLS_SERVER   /* This is an "include guard" */
#define TLS_SERVER 

int create_socket(int port);
void init_openssl();
void cleanup_openssl();
SSL_CTX *create_context();
void configure_context(SSL_CTX *ctx);

#endif 