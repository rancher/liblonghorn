CC=gcc
CFLAGS=-c -Wall
LDFLAGS=
OBJECTS=longhorn_rpc_client.o longhorn_rpc_protocol.o

OUTPUT_FILE=liblonghorn.a
HEADER_FILE=liblonghorn.h
HEADER_LOCAL_DIR=include/
INSTALL_LIB_DIR=/usr/local/lib
INSTALL_HEADER_DIR=/usr/local/include

CLEANEXTS=o a

.PHONY: all
all: $(OUTPUT_FILE)

$(OUTPUT_FILE): $(OBJECTS)
	ar r $@ $^
	ranlib $@

.c.o:
	$(CC) $(CFLAGS)

longhorn_rpc_client.o: src/longhorn_rpc_client.c src/longhorn_rpc_client.h \
	src/log.h src/longhorn_rpc_protocol.h \
	src/uthash.h src/utlist.h
	$(CC) $(CFLAGS) src/longhorn_rpc_client.c

longhorn_rpc_protocol.o: src/longhorn_rpc_protocol.c src/log.h \
	src/longhorn_rpc_protocol.h \
	src/uthash.h src/utlist.h
	$(CC) $(CFLAGS) src/longhorn_rpc_protocol.c

clean:
	rm -f $(OBJECTS) $(OUTPUT_FILE)

install:
	mkdir -p $(INSTALL_LIB_DIR)
	cp -p $(OUTPUT_FILE) $(INSTALL_LIB_DIR)
	mkdir -p $(INSTALL_HEADER_DIR)
	cp -p $(HEADER_LOCAL_DIR)/$(HEADER_FILE) $(INSTALL_HEADER_DIR)

uninstall:
	rm -f $(INSTALL_LIB_DIR)/$(OUTPUT_FILE) $(INSTALL_HEADER_DIR)/$(HEADER_FILE)

cscope:
	find -name '*.[ch]' > cscope.files
	cscope -bq
