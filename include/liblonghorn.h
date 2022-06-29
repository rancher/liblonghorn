#ifndef LIBLONGHORN_HEADER
#define LIBLONGHORN_HEADER

struct lh_client_conn *lh_client_allocate_conn();
void lh_client_free_conn(struct lh_client_conn *conn);
int lh_client_open_conn(struct lh_client_conn *conn, char *socket_path);
int lh_client_close_conn(struct lh_client_conn *conn);

int lh_client_read_at(struct lh_client_conn *conn, void *buf, size_t count, off_t offset);
int lh_client_write_at(struct lh_client_conn *conn, void *buf, size_t count, off_t offset);
int lh_client_unmap(struct lh_client_conn *conn, void *buf, size_t count, off_t offset);
#endif
