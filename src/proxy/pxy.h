#ifndef _HPIO_PXY_
#define _HPIO_PXY_

#include <uuid/uuid.h>
#include <base.h>
#include <channel/channel.h>
#include <ds/map.h>
#include <ds/list.h>
#include <runner/thread.h>
#include <sync/mutex.h>
#include "url_parse.h"
#include "rtb_struct.h"

extern const char *py_str[2];

struct ep {
    int type;
    struct pxy *y;
};

struct pxy {
    u32 fstopped:1;
    mutex_t mtx;
    thread_t backend;
    struct upoll_t *po;
    struct rtb tb;
    struct list_head listener;
    struct list_head unknown;
};

struct pxy *pxy_new();
void pxy_free(struct pxy *y);
int pxy_listen(struct pxy *y, const char *url);
int pxy_connect(struct pxy *y, const char *url);
int pxy_onceloop(struct pxy *y);
int pxy_startloop(struct pxy *y);
void pxy_stoploop(struct pxy *y);

#endif