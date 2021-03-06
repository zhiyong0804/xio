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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <utils/waitgroup.h>
#include <utils/taskpool.h>
#include "sockbase.h"

struct msgbuf* rcv_msgbuf_head_rm(struct sockbase* sb) {
    int rc;
    struct msgbuf* msg = 0;

    mutex_lock(&sb->lock);

    while (!sb->flagset.epipe
            && msgbuf_head_empty(&sb->rcv) && !sb->flagset.non_block) {
        sb->rcv.waiters++;
        condition_wait(&sb->cond, &sb->lock);
        sb->rcv.waiters--;
    }

    if ((rc = msgbuf_head_out_msg(&sb->rcv, &msg)) == 0) {
        SKLOG_NOTICE(sb, "%d socket rcvbuf rm %d", sb->fd, msgbuf_len(msg));
    }

    __emit_pollevents(sb);
    mutex_unlock(&sb->lock);
    return msg;
}

int rcv_msgbuf_head_add(struct sockbase* sb, struct msgbuf* msg) {
    int rc;

    mutex_lock(&sb->lock);
    msgbuf_head_in_msg(&sb->rcv, msg);

    if (sb->rcv.waiters > 0) {
        condition_broadcast(&sb->cond);
    }

    SKLOG_NOTICE(sb, "%d socket rcvbuf add %d", sb->fd, msgbuf_len(msg));
    __emit_pollevents(sb);
    mutex_unlock(&sb->lock);
    return 0;
}

int xgeneric_recv(struct sockbase* sb, char** ubuf) {
    struct msgbuf* msg = 0;

    if (!(msg = rcv_msgbuf_head_rm(sb))) {
        errno = sb->flagset.epipe ? EPIPE : EAGAIN;
        return -1;
    } else {
        *ubuf = get_ubuf(msg);
    }

    return 0;
}


int xrecv(int fd, char** ubuf) {
    int rc = 0;
    struct sockbase* sb;

    BUG_ON(!ubuf);

    if (!(sb = xget(fd))) {
        errno = EBADF;
        return -1;
    }

    if (!sb->vfptr->recv) {
        xput(fd);
        errno = EINVAL;
        return -1;
    }

    rc = sb->vfptr->recv(sb, ubuf);
    xput(fd);
    return rc;
}
