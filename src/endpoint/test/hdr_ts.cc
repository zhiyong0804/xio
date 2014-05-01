#include <gtest/gtest.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <string>
extern "C" {
#include <sync/spin.h>
#include <runner/thread.h>
#include <xio/socket.h>
#include <xio/endpoint.h>
#include <endpoint/ep_hdr.h>
}

using namespace std;

extern int randstr(char *buf, int len);

struct ts_hdr {
    struct ephdr h;
    struct epr rt[4];
    struct uhdr uh;
    char ubuf[128];
};

TEST(endpoint, hdr) {
    struct ts_hdr th;
    char *ubuf;

    th.h.ttl = 4;
    th.h.go = 1;
    th.h.size = sizeof(th.ubuf);
    th.uh.ephdr_off = sizeof(th.h) + sizeof(th.rt) + sizeof(th.uh);

    BUG_ON(ephdr_rtlen(&th.h) != sizeof(th.rt));
    BUG_ON(ephdr_ctlen(&th.h) != sizeof(th.h) + sizeof(th.rt) + sizeof(th.uh));
    BUG_ON(ephdr_dlen(&th.h) != sizeof(th.ubuf));
    BUG_ON(ubuf2ephdr(th.ubuf) != &th.h);
    BUG_ON(ephdr2ubuf(&th.h) != th.ubuf);
    BUG_ON(ephdr_uhdr(&th.h) != &th.uh);
    ubuf = xep_allocubuf(0, 128);
    randstr(ubuf, 128);
    xep_freeubuf(ubuf);
}

static int producer_thread(void *args) {
    string host;
    char buf[128];
    int s;
    int i;
    int efd;
    char *sbuf, *rbuf;

    host.assign((char *)args);
    host += "://127.0.0.1:18898";
    randstr(buf, sizeof(buf));
    BUG_ON((efd = xep_open(XEP_PRODUCER)) < 0);
    for (i = 0; i < 3; i++) {
	BUG_ON((s = xconnect(host.c_str())) < 0);
	BUG_ON(xep_add(efd, s) < 0);
    }
    for (i = 0; i < 30; i++) {
	sbuf = rbuf = 0;
	sbuf = xep_allocubuf(0, sizeof(buf));
	memcpy(sbuf, buf, sizeof(buf));
	BUG_ON(xep_send(efd, sbuf) != 0);
	while (xep_recv(efd, &rbuf) != 0)
	    usleep(20000);
	BUG_ON(sbuf == rbuf);
	BUG_ON(memcmp(rbuf, buf, sizeof(buf)) != 0);
	xep_freeubuf(rbuf);
    }
    xep_close(efd);
    return 0;
}

TEST(endpoint, route) {
    string host("tcp+inproc+ipc://127.0.0.1:18898");
    u32 i;
    thread_t t[1];
    const char *pf[] = {
	"tcp",
	"ipc",
	"inproc",
    };
    int s;
    int efd;
    char *ubuf;

    BUG_ON((s = xlisten(host.c_str())) < 0);
    BUG_ON((efd = xep_open(XEP_COMSUMER)) < 0);
    BUG_ON(xep_add(efd, s) < 0);

    for (i = 0; i < NELEM(t, thread_t); i++) {
	thread_start(&t[i], producer_thread, (void *)pf[i]);
    }

    for (i = 0; i < 90; i++) {
	while (xep_recv(efd, &ubuf) != 0)
	    usleep(20000);
	BUG_ON(xep_send(efd, ubuf));
    }

    for (i = 0; i < NELEM(t, thread_t); i++) {
	thread_stop(&t[i]);
    }
    xep_close(efd);
}
