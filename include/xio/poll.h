#ifndef _HPIO_POLL_
#define _HPIO_POLL_

#ifdef __cplusplus
extern "C" {
#endif

#define XPOLLIN   1
#define XPOLLOUT  2
#define XPOLLERR  4

    int xselect(int events, int nin, int *in_set, int nout, int *out_set);

    struct xpoll_event {
	int xd;
	void *self;

	/* What events i care about ... */
	int care;

	/* What events happened now */
	int happened;
    };

    struct xpoll_t;

    struct xpoll_t *xpoll_create();
    void xpoll_close(struct xpoll_t *po);

#define XPOLL_ADD 1
#define XPOLL_DEL 2
#define XPOLL_MOD 3
    int xpoll_ctl(struct xpoll_t *xp, int op, struct xpoll_event *ue);

    int xpoll_wait(struct xpoll_t *xp, struct xpoll_event *events, int n,
		   int timeout);

#ifdef __cplusplus
}
#endif

#endif