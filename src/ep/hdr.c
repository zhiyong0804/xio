#include <errno.h>
#include <uuid/uuid.h>
#include <os/alloc.h>
#include <channel/channel.h>
#include "hdr.h"
#include "ds/list.h"
#include "hash/crc.h"


void ep_msg_init(struct ep_msg *s, char *payload) {
    INIT_LIST_HEAD(&s->link);
    if (payload) {
	s->payload = payload;
	s->h = (struct ep_hdr *)payload;
	s->r = (struct ep_rt *)(payload + sizeof(*s->h) + s->h->size);
    }
}

struct ep_msg *ep_msg_new(char *payload) {
    struct ep_msg *s = (struct ep_msg *)mem_zalloc(sizeof(*s));
    if (s)
	ep_msg_init(s, payload);
    return s;
}

void ep_msg_free(struct ep_msg *s) {
    if (s->payload)
	channel_freemsg(s->payload);
    mem_free(s, sizeof(struct ep_msg));
}

int ep_msg_validate(struct ep_msg *s) {
    struct ep_hdr *h = s->h;
    struct ep_hdr copyheader = *h;
    int ok;

    copyheader.checksum = 0;
    if (!(ok = (crc16((char *)&copyheader, sizeof(*h)) == h->checksum)))
	errno = EPROTO;
    return ok;
}

void ep_msg_gensum(struct ep_msg *s) {
    struct ep_hdr *h = s->h;
    struct ep_hdr copyheader = *h;

    copyheader.checksum = 0;
    h->checksum = crc16((char *)&copyheader, sizeof(*h));
}

int rt_append_and_go(struct ep_msg *s, struct ep_rt *r, i64 now) {
    struct ep_rt *cr;
    struct ep_hdr *h = s->h;
    char *new_payload, *payload_end;
    
    cr = rt_cur(s);
    cr->stay[0] = (u16)(now - h->sendstamp - cr->begin[0]);
    new_payload = channel_allocmsg(channel_msglen(s->payload) + sizeof(*r));
    if (!new_payload)
	return -1;
    memcpy(new_payload, s->payload, channel_msglen(s->payload));
    channel_freemsg(s->payload);
    s->payload = new_payload;

    /* The new header and route */
    h = s->h = (struct ep_hdr *)s->payload;
    payload_end = s->payload + channel_msglen(s->payload);
    h->ttl++;
    s->r = (struct ep_rt *)(payload_end - rt_size(s->h));

    /* Copy the new route info */
    cr = rt_cur(s);
    *cr = *r;
    cr->begin[0] = (u16)(now - h->sendstamp);
    ep_msg_gensum(s);
    return 0;
}

void rt_shrink_and_back(struct ep_msg *s, i64 now) {
    struct ep_rt *r = rt_cur(s);
    struct ep_hdr *h = s->h;
    r->stay[1] = (now - h->sendstamp - r->begin[1] - r->cost[1]);
    h->ttl--;
    ep_msg_gensum(s);
    r = rt_cur(s);
    r->begin[1] = (u16)(now - h->sendstamp);
}