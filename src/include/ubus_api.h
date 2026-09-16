#ifndef UBUS_API_H
#define UBUS_API_H

#include <libubus.h>
#include <libubox/uloop.h>

#include "ifconf.h"

#define IFCONF_UBUS_OBJ_NAME "iface-conflict"

extern struct ubus_object ifconf_object;

void netlink_cb(struct uloop_fd * ufd, unsigned int events);

#endif