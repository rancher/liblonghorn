#ifndef LONGHORN_RPC_CLIENT_HEADER
#define LONGHORN_RPC_CLIENT_HEADER

#include <pthread.h>

#include "longhorn_rpc_protocol.h"

struct lh_client_conn {
        int seq;  // must be atomic
        int fd;
        int notify_fd;
        int timeout_fd;
        int state;
        pthread_mutex_t mutex;

        pthread_t response_thread;
        pthread_t timeout_thread;

        struct Message *msg_hashtable;
        struct Message *msg_list;
        pthread_mutex_t msg_mutex;

        uint8_t *request_header;
        uint8_t *response_header;
        int header_size;
};

enum {
        CLIENT_CONN_STATE_OPEN = 0,
        CLIENT_CONN_STATE_CLOSE,
};

#endif
