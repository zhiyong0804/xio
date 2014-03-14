#ifndef _HPIO_PROXYIO_
#define _HPIO_PROXYIO_

#include "errno.h"
#include "os/memory.h"
#include "io.h"
#include "bufio/bio.h"
#include "proto.h"
#include "net/socket.h"

typedef struct proxyio {
    int sockfd;
    int64_t seqid;
    struct bio in;
    struct bio out;
    pio_rgh_t rgh;
    struct io sock_ops;
} proxyio_t;

static inline int64_t proxyio_sock_read(struct io *sock, char *buff, int64_t sz) {
    proxyio_t *io = container_of(sock, proxyio_t, sock_ops);
    sz = sk_read(io->sockfd, buff, sz);
    return sz;
}

static inline int64_t proxyio_sock_write(struct io *sock, char *buff, int64_t sz) {
    proxyio_t *io = container_of(sock, proxyio_t, sock_ops);
    sz = sk_write(io->sockfd, buff, sz);
    return sz;
}


static inline void proxyio_init(proxyio_t *io) {
    ZERO(*io);
    bio_init(&io->in);
    bio_init(&io->out);
    io->sock_ops.read = proxyio_sock_read;
    io->sock_ops.write = proxyio_sock_write;
}

static inline void proxyio_destroy(proxyio_t *io) {
    bio_destroy(&io->in);
    bio_destroy(&io->out);
}

static inline proxyio_t *proxyio_new() {
    proxyio_t *io = (proxyio_t *)mem_zalloc(sizeof(*io));
    if (io)
	proxyio_init(io);
    return io;
}

int proxyio_ps_rgs(proxyio_t *io);
int proxyio_at_rgs(proxyio_t *io);

int proxyio_bread(proxyio_t *io, struct pio_hdr *h, char **data, char **rt);
int proxyio_bwrite(proxyio_t *io,
		   const struct pio_hdr *h, const char *data, const char *rt);

static inline int proxyio_one_ready(proxyio_t *io) {
    struct pio_hdr h = {};
    struct bio *b = &io->in;

    if ((b->bsize >= sizeof(h)) && ({ bio_copy(b, (char *)&h, sizeof(h));
		b->bsize >= pio_pkg_size(&h);}))
	return true;
    return false;
}

static inline int proxyio_prefetch(proxyio_t *io) {
    return bio_prefetch(&io->in, &io->sock_ops);
}

static inline int proxyio_flush(proxyio_t *io) {
    while (!bio_empty(&io->out))
	if (bio_flush(&io->out, &io->sock_ops) < 0)
	    return -1;
    return 0;
}


#endif