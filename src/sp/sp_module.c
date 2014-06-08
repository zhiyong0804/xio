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

#include <utils/waitgroup.h>
#include "sp_module.h"

struct sp_global sg;

void sg_add_tg (struct tgtd *tg)
{
	int rc;
	rc = xpoll_ctl (sg.pollid, XPOLL_ADD, &tg->ent);
	BUG_ON (rc);
}

void sg_update_tg (struct tgtd *tg, u32 ev)
{
	int rc;
	tg->ent.events = ev;
	rc = xpoll_ctl (sg.pollid, XPOLL_MOD, &tg->ent);
	BUG_ON (rc);
}

static void connector_event_hndl (struct tgtd *tg)
{
	struct epbase *ep = tg->owner;
	int rc;
	char *ubuf = 0;
	int happened = tg->ent.happened;

	if (happened & XPOLLIN) {
		DEBUG_OFF ("ep %d socket %d recv begin", ep->eid, tg->fd);
		if ( (rc = xrecv (tg->fd, &ubuf) ) == 0) {
			DEBUG_OFF ("ep %d socket %d recv ok", ep->eid, tg->fd);
			if ( (rc = ep->vfptr.add (ep, tg, ubuf) ) < 0) {
				xfreeubuf (ubuf);
				DEBUG_OFF ("ep %d drop msg from socket %d of can't back",
				           ep->eid, tg->fd);
			}
		} else if (errno != EAGAIN)
			happened |= XPOLLERR;
	}
	if (happened & XPOLLOUT) {
		if ( (rc = ep->vfptr.rm (ep, tg, &ubuf) ) == 0) {
			DEBUG_OFF ("ep %d socket %d send begin", ep->eid, tg->fd);
			if ( (rc = xsend (tg->fd, ubuf) ) < 0) {
				xfreeubuf (ubuf);
				if (errno != EAGAIN)
					happened |= XPOLLERR;
				DEBUG_OFF ("ep %d socket %d send with errno %d", ep->eid,
				           tg->fd, errno);
			} else
				DEBUG_OFF ("ep %d socket %d send ok", ep->eid, tg->fd);
		}
	}
	if (happened & XPOLLERR) {
		DEBUG_OFF ("ep %d connector %d epipe", ep->eid, tg->fd);
		sp_generic_term_by_tgtd (ep, tg);
	}
}

static void listener_event_hndl (struct tgtd *tg)
{
	int rc, fd, happened;
	int on, optlen = sizeof (on);
	struct epbase *ep = tg->owner;
	struct tgtd *ntg;

	happened = tg->ent.happened;
	if (happened & XPOLLIN) {
		while ( (fd = xaccept (tg->fd) ) >= 0) {
			rc = xsetopt (fd, XL_SOCKET, XNOBLOCK, &on, optlen);
			BUG_ON (rc);
			DEBUG_OFF ("%d join fd %d begin", ep->eid, fd);
			if ( (ntg = ep->vfptr.join (ep, fd) ) < 0) {
				xclose (fd);
				DEBUG_OFF ("%d join fd %d with errno %d", ep->eid, fd, errno);
			} else
				sg_add_tg (ntg);
		}
		if (errno != EAGAIN)
			happened |= XPOLLERR;
	}
	if (happened & XPOLLERR) {
		DEBUG_OFF ("ep %d listener %d epipe", ep->eid, tg->fd);
		sp_generic_term_by_tgtd (ep, tg);
	}
}


static void epbase_close_bad_tgtds (struct epbase *ep)
{
	struct list_head bad_tgtds = {};
	struct tgtd *tg, *tmp;

	INIT_LIST_HEAD (&bad_tgtds);
	mutex_lock (&ep->lock);
	list_splice (&ep->bad_socks, &bad_tgtds);
	mutex_unlock (&ep->lock);

	walk_tgtd_s (tg, tmp, &bad_tgtds) {
		xclose (tg->fd);
		DEBUG_OFF ("ep %d socket %d bad status", ep->eid, tg->fd);
		ep->vfptr.term (ep, tg);
	}
}

static void event_hndl (struct poll_ent *ent)
{
	int socktype = 0;
	int optlen;
	struct tgtd *tg = (struct tgtd *) ent->self;

	BUG_ON (tg->fd != tg->ent.fd);
	tg->ent.happened = ent->happened;
	xgetopt (tg->fd, XL_SOCKET, XSOCKTYPE, &socktype, &optlen);

	switch (socktype) {
	case XCONNECTOR:
		connector_event_hndl (tg);
		break;
	case XLISTENER:
		listener_event_hndl (tg);
		break;
	default:
		BUG_ON (1);
	}

	/* We do the bad_status tgtd closing work here */
	epbase_close_bad_tgtds (tg->owner);

	DEBUG_OFF (sleep (1) );
}

static void shutdown_epbase()
{
	struct epbase *ep, *nep;

	mutex_lock (&sg.lock);
	walk_epbase_s (ep, nep, &sg.shutdown_head) {
		DEBUG_OFF ("eid %d shutdown", ep->eid);
		list_del_init (&ep->item);
		sg.unused[--sg.nendpoints] = ep->eid;
		ep->vfptr.destroy (ep);
	}
	mutex_unlock (&sg.lock);
}

extern const char *event_str[];

static int po_routine_worker (void *args)
{
	waitgroup_t *wg = (waitgroup_t *) args;
	int rc, i;
	int ivl = 0;
	const char *estr;
	struct poll_ent ent[100];

	waitgroup_done (wg);
	while (!sg.exiting) {
		rc = xpoll_wait (sg.pollid, ent, NELEM (ent, struct poll_ent), 1);
		if (ivl)
			usleep (ivl);
		if (rc < 0)
			goto SD_EPBASE;
		DEBUG_OFF ("%d sockets happened events", rc);
		for (i = 0; i < rc; i++) {
			estr = event_str[ent[i].happened];
			DEBUG_OFF ("socket %d with events %s", ent[i].fd, estr);
			event_hndl (&ent[i]);
		}
SD_EPBASE:
		shutdown_epbase();
	}
	shutdown_epbase();
	return 0;
}

extern struct epbase_vfptr *reqep_vfptr;
extern struct epbase_vfptr *repep_vfptr;

void sp_module_init()
{
	int eid;
	waitgroup_t wg;


	waitgroup_init (&wg);
	sg.exiting = false;
	mutex_init (&sg.lock);
	sg.pollid = xpoll_create();
	BUG_ON (sg.pollid < 0);
	for (eid = 0; eid < MAX_ENDPOINTS; eid++) {
		sg.unused[eid] = eid;
	}
	sg.nendpoints = 0;
	INIT_LIST_HEAD (&sg.epbase_head);
	list_add_tail (&reqep_vfptr->item, &sg.epbase_head);
	list_add_tail (&repep_vfptr->item, &sg.epbase_head);
	INIT_LIST_HEAD (&sg.shutdown_head);

	waitgroup_add (&wg);
	thread_start (&sg.po_routine, po_routine_worker, &wg);
	waitgroup_wait (&wg);
}


void sp_module_exit()
{
	sg.exiting = true;
	thread_stop (&sg.po_routine);
	mutex_destroy (&sg.lock);
	xpoll_close (sg.pollid);
	BUG_ON (sg.nendpoints);
}

int eid_alloc (int sp_family, int sp_type)
{
	struct epbase_vfptr *vfptr = epbase_vfptr_lookup (sp_family, sp_type);
	int eid;
	struct epbase *ep;

	if (!vfptr) {
		errno = EPROTO;
		return -1;
	}
	if (! (ep = vfptr->alloc() ) )
		return -1;
	ep->vfptr = *vfptr;
	mutex_lock (&sg.lock);
	BUG_ON (sg.nendpoints >= MAX_ENDPOINTS);
	eid = sg.unused[sg.nendpoints++];
	ep->eid = eid;
	sg.endpoints[eid] = ep;
	atomic_inc (&ep->ref);
	mutex_unlock (&sg.lock);
	return eid;
}

struct epbase *eid_get (int eid) {
	struct epbase *ep;
	mutex_lock (&sg.lock);
	if (! (ep = sg.endpoints[eid]) ) {
		mutex_unlock (&sg.lock);
		return 0;
	}
	BUG_ON (!atomic_read (&ep->ref) );
	atomic_inc (&ep->ref);
	mutex_unlock (&sg.lock);
	return ep;
}

void eid_put (int eid)
{
	struct epbase *ep = sg.endpoints[eid];

	BUG_ON (!ep);
	atomic_dec_and_lock (&ep->ref, mutex, sg.lock) {
		list_add_tail (&ep->item, &sg.shutdown_head);
		mutex_unlock (&sg.lock);
	}
}

void tgtd_free (struct tgtd *tg)
{
	mem_free (tg, sizeof (*tg) );
}


void epbase_init (struct epbase *ep)
{
	atomic_init (&ep->ref);
	mutex_init (&ep->lock);
	condition_init (&ep->cond);

	skbuf_head_init (&ep->rcv, SP_RCVWND);
	skbuf_head_init (&ep->snd, SP_SNDWND);

	ep->listener_num = 0;
	ep->connector_num = 0;
	ep->bad_num = 0;
	INIT_LIST_HEAD (&ep->listeners);
	INIT_LIST_HEAD (&ep->connectors);
	INIT_LIST_HEAD (&ep->bad_socks);
}

void epbase_exit (struct epbase *ep)
{
	struct skbuf *msg, *nmsg;
	struct tgtd *tg, *ntg;

	BUG_ON (atomic_read (&ep->ref) );

	list_splice (&ep->snd.head, &ep->rcv.head);
	walk_msg_s (msg, nmsg, &ep->rcv.head) {
		list_del_init (&msg->item);
		xfree_skbuf (msg);
	}
	BUG_ON (!list_empty (&ep->rcv.head) );

	list_splice (&ep->listeners, &ep->bad_socks);
	list_splice (&ep->connectors, &ep->bad_socks);
	epbase_close_bad_tgtds (ep);

	atomic_destroy (&ep->ref);
	mutex_destroy (&ep->lock);
	condition_destroy (&ep->cond);
}
