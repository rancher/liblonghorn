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

static int write_header(int fd, struct Message *msg, uint8_t *header) {
        uint16_t MagicVersion = htole16(msg->MagicVersion);
	uint32_t Seq = htole32(msg->Seq);
	uint32_t Type = htole32(msg->Type);
	uint64_t Offset = htole64(*((uint64_t *)(&msg->Offset)));
	uint32_t Size = htole32(msg->Size);
	uint32_t DataLength = htole32(msg->DataLength);

        int offset = 0;

        memcpy(header, &MagicVersion, sizeof(MagicVersion));
        offset += sizeof(MagicVersion);

        memcpy(header + offset, &Seq, sizeof(Seq));
        offset += sizeof(Seq);

        memcpy(header + offset, &Type, sizeof(Type));
        offset += sizeof(Type);

        memcpy(header + offset, &Offset, sizeof(Offset));
        offset += sizeof(Offset);

        memcpy(header + offset, &Size, sizeof(Size));
        offset += sizeof(Size);

        memcpy(header + offset, &DataLength, sizeof(DataLength));
        offset += sizeof(DataLength);

        return write_full(fd, header, offset);
}

int send_msg(int fd, struct Message *msg, uint8_t *header, int header_size) {
        ssize_t n = 0;

        msg->MagicVersion = MAGIC_VERSION;

        n = write_header(fd, msg, header);
        if (n != header_size) {
                errorf("fail to write header\n");
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

static int read_header(int fd, struct Message *msg, uint8_t *header, int header_size) {
        uint64_t Offset;
        int offset = 0, n = 0;

        n = read_full(fd, header, header_size);
        if (n != header_size) {
                errorf("fail to read header\n");
		return -EINVAL;
        }

        msg->MagicVersion = le16toh(*((uint16_t *)(header)));
        offset += sizeof(msg->MagicVersion);

        if (msg->MagicVersion != MAGIC_VERSION) {
                errorf("wrong magic version 0x%x, expected 0x%x\n",
                                msg->MagicVersion, MAGIC_VERSION);
                return -EINVAL;
        }

        msg->Seq = le32toh(*((uint32_t *)(header + offset)));
        offset += sizeof(msg->Seq);

        msg->Type = le32toh(*((uint32_t *)(header + offset)));
        offset += sizeof(msg->Type);

        Offset = le64toh(*((uint64_t *)(header + offset)));
        msg->Offset = *( (int64_t *) &Offset);
        offset += sizeof(msg->Offset);

        msg->Size = le32toh(*((uint32_t *)(header + offset)));
        offset += sizeof(msg->Size);

        msg->DataLength = le32toh(*((uint32_t *)(header + offset)));
        offset += sizeof(msg->DataLength);

        return offset;
}

// Caller needs to release msg->Data
int receive_msg(int fd, struct Message *msg, uint8_t *header, int header_size) {
	ssize_t n;

        bzero(msg, sizeof(struct Message));

        // There is only one thread reading the response, and socket is
        // full-duplex, so no need to lock
        n = read_header(fd, msg, header, header_size);
        if (n != header_size) {
                errorf("fail to read header\n");
                return -EINVAL;
        }

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
