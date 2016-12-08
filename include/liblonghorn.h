#ifndef LIBLONGHORN_HEADER
#define LIBLONGHORN_HEADER

struct client_connection *new_client_connection(char *socket_path);
int shutdown_client_connection(struct client_connection *conn);

int read_at(struct client_connection *conn, void *buf, size_t count, off_t offset);
int write_at(struct client_connection *conn, void *buf, size_t count, off_t offset);

void start_response_processing(struct client_connection *conn);

#endif
