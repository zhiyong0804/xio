#ifndef _HPIO_BCOPT_
#define _HPIO_BCOPT_

#include <inttypes.h>

enum {
    PINGPONG = 0,
    EXCEPTION,
};

extern char *proxyname;

struct bc_opt {
    int size;
    int timeout;
    int64_t deadline;
    int comsumer_num;
    int producer_num;
    char *host;
    int mode;
};

int getoption(int argc, char *argv[], struct bc_opt *cf);

#endif
