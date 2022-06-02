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
	uint16_t MagicVersion = htole16(MAGIC_VERSION);
	uint32_t Seq = htole32(msg->Seq);
	uint32_t Type = htole32(msg->Type);
	uint64_t Offset = htole64(*((uint64_t *)(&msg->Offset)));
	uint32_t Size = htole32(msg->Size);
	uint32_t DataLength = htole32(msg->DataLength);

        msg->MagicVersion = MAGIC_VERSION;
        n = write_full(fd, &MagicVersion, sizeof(MagicVersion));
        if (n != sizeof(MagicVersion)) {
                errorf("fail to write magic version\n");
                return -EINVAL;
        }
        n = write_full(fd, &Seq, sizeof(Seq));
        if (n != sizeof(Seq)) {
                errorf("fail to write seq\n");
                return -EINVAL;
        }
        n = write_full(fd, &Type, sizeof(Type));
        if (n != sizeof(Type)) {
                errorf("fail to write type\n");
                return -EINVAL;
        }
        n = write_full(fd, &Offset, sizeof(Offset));
        if (n != sizeof(Offset)) {
                errorf("fail to write offset\n");
                return -EINVAL;
        }
        n = write_full(fd, &Size, sizeof(Size));
        if (n != sizeof(Size)) {
                errorf("fail to write size\n");
                return -EINVAL;
        }
        n = write_full(fd, &DataLength, sizeof(DataLength));
        if (n != sizeof(DataLength)) {
                errorf("fail to write datalength\n");
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
	uint64_t Offset;

        bzero(msg, sizeof(struct Message));

        // There is only one thread reading the response, and socket is
        // full-duplex, so no need to lock
	n = read_full(fd, &msg->MagicVersion, sizeof(msg->MagicVersion));
        if (n != sizeof(msg->MagicVersion)) {
                errorf("fail to read magic version\n");
		return -EINVAL;
        }

	msg->MagicVersion = le16toh(msg->MagicVersion);

        if (msg->MagicVersion != MAGIC_VERSION) {
                errorf("wrong magic version 0x%x, expected 0x%x\n",
                                msg->MagicVersion, MAGIC_VERSION);
                return -EINVAL;
        }

	n = read_full(fd, &msg->Seq, sizeof(msg->Seq));
        if (n != sizeof(msg->Seq)) {
                errorf("fail to read seq\n");
		return -EINVAL;
        }

	msg->Seq = le32toh(msg->Seq);

        n = read_full(fd, &msg->Type, sizeof(msg->Type));
        if (n != sizeof(msg->Type)) {
                errorf("fail to read type\n");
		return -EINVAL;
        }

	msg->Type = le32toh(msg->Type);

        n = read_full(fd, &Offset, sizeof(Offset));
        if (n != sizeof(Offset)) {
                errorf("fail to read offset\n");
		return -EINVAL;
        }


	Offset = le64toh(Offset);
	msg->Offset = *( (int64_t *) &Offset);

        n = read_full(fd, &msg->Size, sizeof(msg->Size));
        if (n != sizeof(msg->Size)) {
                errorf("fail to read size\n");
		return -EINVAL;
        }

	msg->Size = le32toh(msg->Size);

        n = read_full(fd, &msg->DataLength, sizeof(msg->DataLength));
        if (n != sizeof(msg->DataLength)) {
                errorf("fail to read datalength\n");
		return -EINVAL;
        }

	msg->DataLength = le32toh(msg->DataLength);

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
