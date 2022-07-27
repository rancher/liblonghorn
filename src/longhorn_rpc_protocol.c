#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>

#include "log.h"
#include "longhorn_rpc_protocol.h"

static ssize_t read_full(int fd, void *buf, ssize_t len) {
        ssize_t nread = 0;
        ssize_t ret;

        while (nread < len) {
                ret = read(fd, buf + nread, len - nread);
                if (ret < 0) {
                        if (errno == EINTR) {
                                continue;
                        }
                        return ret;
                } else if (ret == 0) {
                        return nread;
                }
                nread += ret;
        }

        return nread;
}

static ssize_t write_full(int fd, void *buf, ssize_t len) {
        ssize_t nwrote = 0;
        ssize_t ret;

        while (nwrote < len) {
                ret = write(fd, buf + nwrote, len - nwrote);
                if (ret < 0) {
                        if (errno == EINTR) {
                                continue;
                        }
                        return ret;
                }
                nwrote += ret;
        }

        return nwrote;
}

int send_msg(int fd, struct Message *msg) {
        ssize_t n = 0;
	struct MessageHeader header = {'\0'};

	header.MagicVersion = htole16(MAGIC_VERSION);
	header.Seq = htole32(msg->Seq);
	header.Type = htole32(msg->Type);
	header.Offset = htole64(*((uint64_t *)(&msg->Offset)));
	header.Size = htole32(msg->Size);
	header.DataLength = htole32(msg->DataLength);

        n = write_full(fd, &header, sizeof(header));
        if (n != sizeof(header)) {
                errorf("fail to write message header\n");
                return -EINVAL;
        }

	if (msg->DataLength != 0) {
		n = write_full(fd, msg->Data, msg->DataLength);
		if (n != msg->DataLength) {
                        if (n < 0)
                                perror("fail writing data");
			errorf("fail to write data, wrote %zd; expected %u\n",
                                        n, msg->DataLength);
                        return -EINVAL;
		}
	}
        return 0;
}

// Caller needs to release msg->Data
int receive_msg(int fd, struct Message *msg) {
	ssize_t n;
	struct MessageHeader header = {'\0'};

        bzero(msg, sizeof(struct Message));

        // There is only one thread reading the response, and socket is
        // full-duplex, so no need to lock
	n = read_full(fd, &header, sizeof(header));
        if (n != sizeof(header)) {
                errorf("fail to read message header\n");
		return -EINVAL;
        }

	msg->MagicVersion = le16toh(header.MagicVersion);

        if (msg->MagicVersion != MAGIC_VERSION) {
                errorf("wrong magic version 0x%x, expected 0x%x\n",
                                msg->MagicVersion, MAGIC_VERSION);
                return -EINVAL;
        }

	msg->Seq = le32toh(header.Seq);
	msg->Type = le32toh(header.Type);

	header.Offset = le64toh(header.Offset);
	msg->Offset = *( (int64_t *) &header.Offset);

	msg->Size = le32toh(header.Size);
	msg->DataLength = le32toh(header.DataLength);

	if (msg->DataLength > 0) {
		msg->Data = malloc(msg->DataLength);
                if (msg->Data == NULL) {
                        perror("cannot allocate memory for data");
                        return -EINVAL;
                }
		n = read_full(fd, msg->Data, msg->DataLength);
		if (n != msg->DataLength) {
                        errorf("Cannot read full from fd, %u vs %zd\n",
                                msg->DataLength, n);
			free(msg->Data);
			return -EINVAL;
		}
	}
	return 0;
}
