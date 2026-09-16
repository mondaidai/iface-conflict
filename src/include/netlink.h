#ifndef NETLINK_H
#define NETLINK_H

#include <stddef.h>

#include "ifconf.h"

enum {
    NETLINK_NO_OP = 0,
    NETLINK_NEWLINK = 1 << 0,
    NETLINK_DELLINK = 1 << 1,
    NETLINK_NEWADDR = 1 << 2,
    NETLINK_DELADDR = 1 << 3,
    NETLINK_ERR = 1 << 4
};

int handle_netlink_message(struct ifconf_ctx * ctx, const void * recvbuf, size_t n);
ifconf_status_t ifconf_request_links(struct ifconf_ctx * ctx);
ifconf_status_t ifconf_request_addrs(struct ifconf_ctx * ctx);

#endif