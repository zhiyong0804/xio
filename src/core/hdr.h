#ifndef _HPIO_HDR_
#define _HPIO_HDR_

#include <errno.h>
#include <uuid/uuid.h>
#include "ds/list.h"
#include "hash/crc.h"
#include "os/memory.h"

struct pio_rt {
    uuid_t uuid;
    uint8_t ip[4];
    uint16_t port;
    uint16_t begin[2];
    uint16_t cost[2];
    uint16_t stay[2];
};
#define PIORTLEN sizeof(struct pio_rt)

struct pio_hdr {
    uint8_t version;
    uint16_t ttl:4;
    uint16_t end_ttl:4;
    uint16_t go:1;
    uint32_t size;
    uint16_t timeout;
    uint16_t checksum;
    uint64_t seqid;
    int64_t sendstamp;
};
#define PIOHDRLEN sizeof(struct pio_hdr)

static inline uint32_t pio_rt_size(const struct pio_hdr *h) {
    uint32_t ttl = h->go ? h->ttl : h->end_ttl;
    return ttl * PIORTLEN;
}

static inline uint32_t pio_pkg_size(const struct pio_hdr *h) {
    return PIOHDRLEN + h->size + pio_rt_size(h);
}

static inline int ph_timeout(const struct pio_hdr *h, int64_t now) {
    return h->timeout && (h->sendstamp + h->timeout < now);
}

static inline int ph_validate(const struct pio_hdr *h) {
    struct pio_hdr copyheader = *h;
    int ok;
    copyheader.checksum = 0;
    if (!(ok = (crc16((char *)&copyheader, PIOHDRLEN) == h->checksum)))
	errno = EPROTO;
    return ok;
}

static inline void ph_makechksum(struct pio_hdr *hdr) {
    struct pio_hdr copyheader = *hdr;
    copyheader.checksum = 0;
    hdr->checksum = crc16((char *)&copyheader, PIOHDRLEN);
}


typedef struct pio_msg {
    struct pio_hdr hdr;
    char *data;
    struct pio_rt *rt;
    struct list_head mq_link;
} pio_msg_t;
#define PIOMSGLEN sizeof(struct pio_msg)

#define list_for_each_msg_safe(pos, tmp, head)				\
    list_for_each_entry_safe(pos, tmp, head, struct pio_msg, mq_link)

static inline uint32_t pio_msg_size(pio_msg_t *msg) {
    uint32_t ttl = msg->hdr.go ? msg->hdr.ttl : msg->hdr.end_ttl;
    return PIOMSGLEN + msg->hdr.size + ttl * PIORTLEN;
}

#define pio_msg_currt(msg) (&(msg)->rt[(msg)->hdr.ttl - 1])
#define pio_msg_prevrt(msg) (&(msg)->rt[(msg)->hdr.ttl - 2])

static inline void rt_go_cost(pio_msg_t *msg, int64_t now) {
    struct pio_hdr *h = &msg->hdr;
    struct pio_rt *rt = pio_msg_currt(msg);
    rt->stay[0] = (uint16_t)(now - h->sendstamp - rt->begin[0]);
}

static inline void rt_back_cost(pio_msg_t *msg, int64_t now) {
    struct pio_hdr *h = &msg->hdr;
    struct pio_rt *rt = pio_msg_currt(msg);
    rt->cost[1] = (uint16_t)(now - h->sendstamp - rt->begin[1]);
}

static inline int rt_append_and_go(pio_msg_t *msg, struct pio_rt *rt,
					 int64_t now) {
    struct pio_rt *crt;
    struct pio_hdr *h = &msg->hdr;
    uint32_t new_sz = pio_rt_size(h) + PIORTLEN;
    if (!(crt = (struct pio_rt *)mem_realloc(msg->rt, new_sz)))
	return false;
    msg->rt = crt;
    crt = pio_msg_currt(msg);
    crt->stay[0] = (uint16_t)(now - h->sendstamp - crt->begin[0]);
    h->ttl++;
    rt->begin[0] = (uint16_t)(now - h->sendstamp);
    memcpy(pio_msg_currt(msg), rt, PIORTLEN);
    ph_makechksum(h);
    return true;
}

static inline void rt_shrink_and_back(pio_msg_t *msg, int64_t now) {
    struct pio_rt *crt = pio_msg_currt(msg);
    struct pio_hdr *h = &msg->hdr;
    crt->stay[1] = (uint16_t)(now - h->sendstamp - crt->begin[1] - crt->cost[1]);
    msg->hdr.ttl--;
    ph_makechksum(&msg->hdr);
    crt = pio_msg_currt(msg);
    crt->begin[1] = (uint16_t)(now - msg->hdr.sendstamp);
}


static inline void pio_msg_free_data_and_rt(pio_msg_t *msg) {
    mem_free(msg->data, msg->hdr.size);
    mem_free(msg->rt, pio_rt_size(&msg->hdr));
}



#define PROXYNAME_MAX 128

enum {
    PIO_RCVER = 1,
    PIO_SNDER,
};

typedef struct pio_rgh {
    uint32_t type;
    uuid_t id;
    char proxyname[PROXYNAME_MAX];
} pio_rgh_t;
#define PIORGHLEN sizeof(struct pio_rgh)



#endif
