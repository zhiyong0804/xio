/*
  Copyright (c) 2013-2014 Dong Fang. All rights reserved.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom
  the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/

#include "req_ep.h"

static struct epbase* reqep_alloc() {
    struct reqep* reqep = mem_zalloc(sizeof(struct reqep));

    if (reqep) {
        epbase_init(&reqep->base);
        reqep->lbs = rrbin_vfptr->new(reqep);
        reqep->peer = 0;
        return &reqep->base;
    }

    return 0;
}

static void reqep_destroy(struct epbase* ep) {
    struct reqep* reqep = cont_of(ep, struct reqep, base);
    BUG_ON(!reqep);
    reqep->lbs->free(reqep->lbs);
    epbase_exit(ep);
    mem_free(reqep, sizeof(*reqep));
}

static int reqep_send(struct epbase* ep, char* ubuf) {
    int rc = -1;
    struct reqep* reqep = cont_of(ep, struct reqep, base);
    struct rrhdr* pg = 0;
    struct rtentry rt = {};
    struct req_tgtd* go = 0;

    mutex_lock(&ep->lock);

    if ((go = reqep->lbs->select(reqep->lbs, ubuf))) {
        tgtd_mstats_incr(&go->tg.stats, TG_SEND);
    }

    mutex_unlock(&ep->lock);

    if (!go) {
        return -1;
    }

    uuid_copy(rt.uuid, go->uuid);
    pg = new_rrhdr(&rt);
    uctl_add(ubuf, (char*) pg);
    DEBUG_OFF("ep %d send req %10.10s to socket %d", ep->eid, ubuf, go->tg.fd);
    rc = xsend(go->tg.fd, ubuf);
    return rc;
}

static int reqep_add(struct epbase* ep, struct tgtd* tg, char* ubuf) {
    struct rrhdr* pg = get_rrhdr(ubuf);

    if (!pg) {
        return -1;
    }

    pg->ttl--;
    DEBUG_OFF("ep %d recv resp %10.10s from socket %d", ep->eid, ubuf, tg->fd);
    mutex_lock(&ep->lock);
    msgbuf_head_in(&ep->rcv, ubuf);
    BUG_ON(ep->rcv.waiters < 0);

    if (ep->rcv.waiters) {
        condition_broadcast(&ep->cond);
    }

    tgtd_mstats_incr(&tg->stats, TG_RECV);
    mutex_unlock(&ep->lock);
    return 0;
}

static int reqep_rm(struct epbase* ep, struct tgtd* tg, char** ubuf) {
    int rc = -1;

    if (tg->pollfd.events & XPOLLOUT) {
        sg_update_tg(tg, tg->pollfd.events & ~XPOLLOUT);
    }

    return rc;
}

static void reqep_term(struct epbase* ep, struct tgtd* tg) {
    struct reqep* req_ep = cont_of(ep, struct reqep, base);

    /* Only connector need to participate load balance */
    if (get_socktype(tg->fd) == XCONNECTOR) {
        mutex_lock(&ep->lock);
        req_ep->lbs->rm(req_ep->lbs, get_req_tgtd(tg));
        mutex_unlock(&ep->lock);
    }

    mem_free(cont_of(tg, struct req_tgtd, tg), sizeof(struct req_tgtd));
}

static struct tgtd* reqep_join(struct epbase* ep, int fd) {
    struct reqep* req_ep = cont_of(ep, struct reqep, base);
    struct req_tgtd* req_tg = mem_zalloc(sizeof(struct req_tgtd));

    if (!req_tg) {
        return 0;
    }

    ZERO(req_tg->lbs_ent);
    msgbuf_head_init(&req_tg->ls_head, SP_SNDWND);
    uuid_generate(req_tg->uuid);
    generic_tgtd_init(ep, &req_tg->tg, fd);
    mutex_lock(&ep->lock);

    /* Only connector need to participate load balance */
    if (get_socktype(fd) == XCONNECTOR) {
        req_ep->lbs->add(req_ep->lbs, req_tg);
    }

    mutex_unlock(&ep->lock);
    return &req_tg->tg;
}


static int set_proxyto(struct epbase* ep, void* optval, int optlen) {
    int rc;
    int front_eid = * (int*) optval;
    struct epbase* peer = eid_get(front_eid);

    if (!peer) {
        ERRNO_RETURN(EBADF);
    }

    rc = epbase_proxyto(peer, ep);
    eid_put(front_eid);
    return rc;
}

static int set_lbs(struct epbase* ep, void* optval, int optlen) {
    struct reqep* req_ep = cont_of(ep, struct reqep, base);
    struct loadbalance_vfptr* lbs_new = 0;

    switch (* (int*) optval) {
    case SP_REQ_RRBIN:
        lbs_new = rrbin_vfptr;
        break;

    case SP_REQ_ULHASH:
        lbs_new = ulhash_vfptr;
        break;

    default:
        BUG_ON(1);
    }

    mutex_lock(&ep->lock);

    if (req_ep->lbs) {
        req_ep->lbs->free(req_ep->lbs);
    }

    req_ep->lbs = lbs_new->new(req_ep);
    mutex_unlock(&ep->lock);
    return 0;
}

static int set_rrbin_weight(struct epbase* ep, void* optval, int optlen) {
    struct reqep* req_ep = cont_of(ep, struct reqep, base);
    struct rrbin_attr* ra = (struct rrbin_attr*) optval;
    struct tgtd* tg;
    struct req_tgtd* req_tg;

    mutex_lock(&ep->lock);

    if (req_ep->lbs->type != SP_REQ_RRBIN) {
        mutex_unlock(&ep->lock);
        ERRNO_RETURN(EINVAL);
    }

    tg = get_tgtd_if(tg, &ep->connectors, (tg->fd == ra->fd));

    if (tg) {
        req_tg = cont_of(tg, struct req_tgtd, tg);
        req_tg->lbs_ent.rrbin.origin_weight = ra->weight;
    }

    mutex_unlock(&ep->lock);
    return 0;
}


static const ep_setopt setopt_vfptr[] = {
    set_proxyto,
    set_lbs,
    set_rrbin_weight,
};

static const ep_getopt getopt_vfptr[] = {
    0,
    0,
    0,
};

static int reqep_setopt(struct epbase* ep, int opt, void* optval, int optlen) {
    int rc;

    if (opt < 0 || opt >= NELEM(setopt_vfptr) || !setopt_vfptr[opt]) {
        ERRNO_RETURN(EINVAL);
    }

    rc = setopt_vfptr[opt](ep, optval, optlen);
    return rc;
}

static int reqep_getopt(struct epbase* ep, int opt, void* optval, int* optlen) {
    int rc;

    if (opt < 0 || opt >= NELEM(getopt_vfptr) || !getopt_vfptr[opt]) {
        ERRNO_RETURN(EINVAL);
    }

    rc = getopt_vfptr[opt](ep, optval, optlen);
    return rc;
}

struct epbase_vfptr reqep = {
    .sp_family = SP_REQREP,
    .sp_type = SP_REQ,
    .alloc = reqep_alloc,
    .destroy = reqep_destroy,
    .send = reqep_send,
    .add = reqep_add,
    .rm = reqep_rm,
    .join = reqep_join,
    .term = reqep_term,
    .setopt = reqep_setopt,
    .getopt = reqep_getopt,
};

struct epbase_vfptr* reqep_vfptr = &reqep;

