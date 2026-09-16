#ifndef IFCONFDET_H
#define IFCONFDET_H
#include <libubox/uloop.h>

#include "ifconf_types.h"

struct ubus_context;

//uncomment to enable logging of conflicts to syslog 
#define DEBUG

/* Set to 0 to include IPv6 link-local addresses in subnet conflicts. */
#ifndef IFCONF_IGNORE_IPV6_LINKLOCAL
#define IFCONF_IGNORE_IPV6_LINKLOCAL 1
#endif

typedef enum {
	IFCONF_OK,
	IFCONF_SOCK_CREATE_ERR,
	IFCONF_SOCK_ERR,
	IFCONF_MEM_ERR,
	IFCONF_BIND_ERR,
	IFCONF_ULOOP_ERR,
	IFCONF_NETLINK_DATA_ERR,
	IFCONF_UBUS_ADD_OBJECT_ERR,
	IFCONF_UBUS_CONNECT_ERR,
	IFCONF_ERR
} ifconf_status_t;

struct ifconf_ctx {
	struct uloop_fd ufd;
	struct ifgroup ifs;
	struct ubus_context * ubus;
	uint32_t seq;
	uint32_t link_seq;
	uint32_t addr_seq;
};

ifconf_status_t ifconf_initialize(struct ifconf_ctx * ctx);

ifconf_status_t ifconf_sync(struct ifconf_ctx * ctx);

void ifconf_cleanup(struct ifconf_ctx * ctx);

char * ifconf_strerr(ifconf_status_t code);

#endif