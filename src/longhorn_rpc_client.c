/* 
 * Lock Sequence:
 * conn->msg_mutex
 * msg->mutex
 * conn->mutex
 * */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/timerfd.h>
#include <sys/time.h>
#include <time.h>
#include <poll.h>

#include "log.h"
#include "longhorn_rpc_client.h"

int retry_interval = 5;
int retry_counts = 5;
int request_timeout_period = 15; //seconds

int send_request(struct lh_client_conn *conn, struct Message *req) {
        int rc = 0;

        pthread_mutex_lock(&conn->mutex);
        rc = send_msg(conn->fd, req, conn->request_header, conn->header_size);
        pthread_mutex_unlock(&conn->mutex);
        return rc;
}

int receive_response(struct lh_client_conn *conn, struct Message *resp) {
        return receive_msg(conn->fd, resp, conn->response_header, conn->header_size);
}

// Must be called with conn->msg_mutex hold
void update_timeout_timer(struct lh_client_conn *conn) {
        struct Message *head_msg = conn->msg_list;
        struct itimerspec its;
        its.it_value.tv_sec = 0;
        its.it_value.tv_nsec = 0;
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;

        // When there are messages on the queue, make timer that expires
        // request_timeout_period seconds from now.
        if (head_msg != NULL) {
                if (clock_gettime(CLOCK_MONOTONIC, &its.it_value) < 0) {
                        perror("Fail to get current time");
                        return;
                }

                its.it_value.tv_sec += request_timeout_period;

                // Arm
                if (timerfd_settime(conn->timeout_fd,
                                TFD_TIMER_ABSTIME,
                                &its, NULL) < 0) {
                        errorf("BUG: Fail to set new timer\n");
                }
        } else {
                // When there are no more messages on the queue, we disarm
                // the timer.  This is because we rearm the timer for every
                // response received; when there are no more messages on the 
                // queue, there is no response to be received.

                // Disarm
                if (timerfd_settime(conn->timeout_fd,
                                TFD_TIMER_ABSTIME,
                                &its, NULL) < 0) {
                        errorf("BUG: Fail to disarm timer\n");
                }
        }
}

void add_request_in_queue(struct lh_client_conn *conn, struct Message *req) {
        int start_timer = 0;

        pthread_mutex_lock(&conn->msg_mutex);

        // Arm timer on this message if there are no other in progress messages.
        // We start the timer when there are no messages in the queue
        // This allows us to have a timeout in case the there is only one
        // message on the queue and we don't receive a response.
        start_timer = (conn->msg_list == NULL);

        HASH_ADD_INT(conn->msg_hashtable, Seq, req);
        DL_APPEND(conn->msg_list, req);

        if (start_timer) {
                update_timeout_timer(conn);
        }

        pthread_mutex_unlock(&conn->msg_mutex);
}

struct Message *find_and_remove_request_from_queue(struct lh_client_conn *conn,
                int seq) {
        struct Message *req = NULL;

        pthread_mutex_lock(&conn->msg_mutex);
        HASH_FIND_INT(conn->msg_hashtable, &seq, req);
        if (req != NULL) {
                HASH_DEL(conn->msg_hashtable, req);
                DL_DELETE(conn->msg_list, req);
                // When we find a message on the queue, we will arm the timer
                // for request_timeout_period seconds from now.  This ensures
                // that when messages are on the queue, we should receive some
                // kind of response to one of the messages on the queue every
                // request_timeout_period seconds.
                //
                // Also, update_timeout_timer will also disarm the timer when
                // there are no more messages on the queue because the replica
                // shouldn't response in this case.
                update_timeout_timer(conn);
        }
        pthread_mutex_unlock(&conn->msg_mutex);
        return req;
}

int lh_client_close_conn(struct lh_client_conn *conn) {
        struct Message *req, *tmp;

        if (conn == NULL) {
                return 0;
        }

        errorf("Closing connection\n");

        pthread_mutex_lock(&conn->mutex);
        if  (conn->state == CLIENT_CONN_STATE_CLOSE) {
                pthread_mutex_unlock(&conn->mutex);
                return 0;
        }

        // Prevent future requests
        conn->state = CLIENT_CONN_STATE_CLOSE;
        close(conn->timeout_fd);
        close(conn->fd);
        pthread_mutex_unlock(&conn->mutex);

        pthread_mutex_lock(&conn->msg_mutex);
        // Clean up and fail all pending requests
        HASH_ITER(hh, conn->msg_hashtable, req, tmp) {
                HASH_DEL(conn->msg_hashtable, req);
                DL_DELETE(conn->msg_list, req);

                pthread_mutex_lock(&req->mutex);
                req->Type = TypeError;
                errorf("Cancel request %d due to disconnection\n", req->Seq);
                pthread_mutex_unlock(&req->mutex);
                pthread_cond_signal(&req->cond);
        }
        pthread_mutex_unlock(&conn->msg_mutex);

        if (pthread_cancel(conn->timeout_thread) < 0) {
                errorf("Cannot cancel timeout thread\n");
        }
        if (pthread_cancel(conn->response_thread) < 0) {
                errorf("Cannot cancel timeout thread\n");
        }
        if (pthread_join(conn->timeout_thread, NULL) < 0) {
                errorf("Cannot wait for timeout thread\n");
        }
        if (pthread_join(conn->response_thread, NULL) < 0) {
                errorf("Cannot wait timeout thread\n");
        }
        errorf("Connection close complete\n");
        return 0;
}

void* response_process(void *arg) {
        struct lh_client_conn *conn = arg;
        struct Message *req, *resp;
        int ret = 0;

	resp = malloc(sizeof(struct Message));
        if (resp == NULL) {
            perror("cannot allocate memory for resp");
            return NULL;
        }

        while (1) {
                ret = receive_response(conn, resp);
                if (ret != 0) {
                        break;
                }

                if (resp->Type == TypeClose) {
                        errorf("Receive close message, about to end the connection\n");
                        break;
                }

                switch (resp->Type) {
                case TypeRead:
                case TypeWrite:
                case TypeUnmap:
                        errorf("Wrong type for response %d of seq %d\n",
                                        resp->Type, resp->Seq);
                        continue;
                case TypeError:
                        errorf("Receive error for response %d of seq %d\n",
					resp->Type, resp->Seq);
                        /* fall through so we can response to caller */
                case TypeEOF:
                case TypeResponse:
                        break;
                default:
                        errorf("Unknown message type %d\n", resp->Type);
                }

                req = find_and_remove_request_from_queue(conn, resp->Seq);
                if (req == NULL) {
                        errorf("Unknown response sequence %d\n", resp->Seq);
                        free(resp->Data);
                        continue;
                }

                pthread_mutex_lock(&req->mutex);

                if (resp->Type == TypeResponse || resp->Type == TypeEOF) {
			req->Size = resp->Size;
			req->DataLength = resp->DataLength;
			if (resp->DataLength != 0) {
				memcpy(req->Data, resp->Data, resp->DataLength);
			}
                } else if (resp->Type == TypeError) {
                        req->Type = TypeError;
                }
                free(resp->Data);

                pthread_mutex_unlock(&req->mutex);

                pthread_cond_signal(&req->cond);
        }
        free(resp);
        if (ret != 0) {
                errorf("Receive response returned error\n");
        }
        lh_client_close_conn(conn);
        return NULL;
}

void *timeout_handler(void *arg) {
	struct lh_client_conn *conn = arg;
        int ret;
        int nfds = 1;
        struct pollfd *fds = malloc(sizeof(struct pollfd) * nfds);
        struct Message *req;
        struct timespec now;

        fds[0].fd = conn->timeout_fd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        while (1) {
                ret = poll(fds, nfds, -1);
                if (ret < 0) {
                        perror("Fail to poll timeout fd");
                        break;
                }
                if (fds[0].revents == POLLHUP) {
                        errorf("Timeout fd closed\n");
                        break;
                }
                if (ret != 1 || fds[0].revents != POLLIN) {
                        errorf("BUG: Timeout fd polling have unexpected result\n");
                        break;
                }

                if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
                        errorf("BUG: Fail to get current time\n");
                        break;
                }

                pthread_mutex_lock(&conn->msg_mutex);
                DL_FOREACH(conn->msg_list, req) {
                        HASH_DEL(conn->msg_hashtable, req);
                        DL_DELETE(conn->msg_list, req);

                        pthread_mutex_lock(&req->mutex);
                        req->Type = TypeError;
                        errorf("Timeout request %d due to disconnection\n",
                                        req->Seq);
                        pthread_mutex_unlock(&req->mutex);
                        pthread_cond_signal(&req->cond);
                }
                pthread_mutex_unlock(&conn->msg_mutex);

        }
        free(fds);
	return NULL;
}

int start_process(struct lh_client_conn *conn) {
        int rc;

        conn->timeout_fd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (conn->timeout_fd < 0) {
                perror("Fail to create timerfd");
                return -EFAULT;
        }
        rc = pthread_create(&conn->timeout_thread, NULL, &timeout_handler, conn);
        if (rc < 0) {
                perror("Fail to create response thread");
                return -EFAULT;
        }
        rc = pthread_create(&conn->response_thread, NULL, &response_process, conn);
        if (rc < 0) {
                perror("Fail to create response thread");
                return -EFAULT;
        }
        return 0;
}

int new_seq(struct lh_client_conn *conn) {
        return __sync_fetch_and_add(&conn->seq, 1);
}

int process_request(struct lh_client_conn *conn, void *buf, size_t count, off_t offset,
                uint32_t type) {
        struct Message *req = malloc(sizeof(struct Message));
        int rc = 0;

        pthread_mutex_lock(&conn->mutex);
        if (conn->state != CLIENT_CONN_STATE_OPEN) {
                errorf("Cannot queue in more request. Connection is not open\n");
                pthread_mutex_unlock(&conn->mutex);
                return -EFAULT;
        }
        pthread_mutex_unlock(&conn->mutex);

        if (req == NULL) {
                perror("cannot allocate memory for req");
                return -EINVAL;
        }

        if (type != TypeRead && type != TypeWrite && type != TypeUnmap) {
                errorf("BUG: Invalid type for process_request %d\n", type);
                rc = -EFAULT;
                goto free;
        }
        req->Seq = new_seq(conn);
	req->Type = type;
	req->Offset = offset;
	req->Size = count;
	req->Data = buf;
	req->DataLength = 0;

	// We only going to transfer data on wire if it's write request
	if (req->Type == TypeWrite) {
		req->DataLength = count;
	}

        rc = pthread_cond_init(&req->cond, NULL);
        if (rc < 0) {
                perror("Fail to init phread_cond");
                rc = -EFAULT;
                goto free;
        }
        rc = pthread_mutex_init(&req->mutex, NULL);
        if (rc < 0) {
                perror("Fail to init phread_mutex");
                rc = -EFAULT;
                goto free;
        }

        add_request_in_queue(conn, req);

        pthread_mutex_lock(&req->mutex);
        rc = send_request(conn, req);
        if (rc < 0) {
                goto out;
        }

        pthread_cond_wait(&req->cond, &req->mutex);

        if (req->Type == TypeError) {
                rc = -EFAULT;
        }
out:
        pthread_mutex_unlock(&req->mutex);
        // need to clean up in case send_request() failed
        find_and_remove_request_from_queue(conn, req->Seq);
free:
        free(req);
        return rc;
}

int lh_client_read_at(struct lh_client_conn *conn, void *buf, size_t count, off_t offset) {
        return process_request(conn, buf, count, offset, TypeRead);
}

int lh_client_write_at(struct lh_client_conn *conn, void *buf, size_t count, off_t offset) {
        return process_request(conn, buf, count, offset, TypeWrite);
}

int lh_client_unmap(struct lh_client_conn *conn, void *buf, size_t count, off_t offset) {
        return process_request(conn, buf, count, offset, TypeUnmap);
}

int lh_client_open_conn(struct lh_client_conn *conn, char *socket_path) {
        struct sockaddr_un addr;
        int fd, rc = 0;
        int i, connected = 0;

        if (conn == NULL) {
                return -EINVAL;
        }

        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd == -1) {
                perror("socket error");
                return -EFAULT;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (strlen(socket_path) >= 108) {
                errorf("socket path is too long, more than 108 characters\n");
                return -EINVAL;
        }

        strncpy(addr.sun_path, socket_path, strlen(socket_path));

        for (i = 0; i < retry_counts; i ++) {
                if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                        connected = 1;
                        break;
		}

                perror("Cannot connect, retrying");
                sleep(retry_interval);
        }
        if (!connected) {
                perror("connection error");
                return -EFAULT;
        }

        conn->fd = fd;
        conn->seq = 0;
        conn->msg_hashtable = NULL;
        conn->msg_list = NULL;

        rc = pthread_mutex_init(&conn->mutex, NULL);
        if (rc < 0) {
                perror("fail to init conn->mutex");
                return -EFAULT;
        }

        rc = pthread_mutex_init(&conn->msg_mutex, NULL);
        if (rc < 0) {
                perror("fail to init conn->mutex");
                return -EFAULT;
        }

        conn->state = CLIENT_CONN_STATE_OPEN;

        return start_process(conn);
}

struct lh_client_conn *lh_client_allocate_conn() {
        struct lh_client_conn *conn = malloc(sizeof(struct lh_client_conn));
        if (conn == NULL) {
                return NULL;
        }
        bzero(conn, sizeof(struct lh_client_conn));

        conn->header_size = sizeof(struct MessageHeader);
        conn->request_header = malloc(conn->header_size);
        conn->response_header = malloc(conn->header_size);
        if (!conn->request_header || !conn->response_header) {
                free(conn->request_header);
                free(conn->response_header);
                free(conn);
                return NULL;
        }

        return conn;
}

void lh_client_free_conn(struct lh_client_conn *conn) {
        if (conn) {
                free(conn->request_header);
                free(conn->response_header);
                free(conn);
        }
}
