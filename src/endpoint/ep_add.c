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

#include <stdio.h>
#include <os/alloc.h>
#include <xio/socket.h>
#include "ep_struct.h"

struct endsock *__xep_add(int eid, int sockfd) {
    struct endpoint *ep = eid_get(eid);
    int fnb = 1;
    int socktype = XCONNECTOR;
    int optlen = sizeof(socktype);
    struct endsock *sk = endsock_new();

    if (!(socktype & (XCONNECTOR|XLISTENER))) {
	errno = EBADF;
	return 0;
    }
    if (!sk)
	return 0;
    sk->owner = ep;
    sk->fd = sockfd;
    xsetopt(sockfd, XL_SOCKET, XNOBLOCK, &fnb, sizeof(fnb));
    xgetopt(sockfd, XL_SOCKET, XSOCKTYPE, &socktype, &optlen);
    switch (socktype) {
    case XCONNECTOR:
	list_add_tail(&sk->link, &ep->connectors);
	if (ep->type == XEP_PRODUCER)
	    uuid_generate(sk->uuid);
	break;
    case XLISTENER:
	list_add_tail(&sk->link, &ep->listeners);
	break;
    default:
	BUG_ON(1);
    }
    DEBUG_OFF("endpoint %d add %d socket", eid, sockfd);
    return sk;
}

int xep_add(int eid, int sockfd) {
    struct endsock *s = __xep_add(eid, sockfd);
    DEBUG_OFF("endpoint %d add %d socket", eid, sockfd);
    if (!s)
	return -1;
    return 0;
}
