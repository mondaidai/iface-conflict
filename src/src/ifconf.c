#include "ifconf.h"
#include "ifgroup.h"
#include "ubus_api.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <libubus.h>
#include <libubox/uloop.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include "netlink.h"

ifconf_status_t ifconf_initialize(struct ifconf_ctx * ctx)
{
	if(!ctx)
	return IFCONF_ERR;
	memset(ctx, 0, sizeof(struct ifconf_ctx));
	ctx->ufd.fd = -1;
	ctx->ubus = ubus_connect(NULL);
	if(!ctx->ubus)
		return IFCONF_UBUS_CONNECT_ERR;
	
	int sockfd = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK, NETLINK_ROUTE);
	if(sockfd < 0) {
		ubus_free(ctx->ubus);
		ctx->ubus = NULL;
		return IFCONF_SOCK_CREATE_ERR;
	}
	
	struct sockaddr_nl sa;
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR;
	if(bind(sockfd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		close(sockfd);
		ubus_free(ctx->ubus);
		ctx->ubus = NULL;
		return IFCONF_BIND_ERR;
	}
	
	ctx->ufd.fd = sockfd;
	ctx->ufd.cb = netlink_cb;
	
	int ret;
	if((ret = uloop_fd_add(&ctx->ufd, ULOOP_READ))) {
		close(sockfd);
		ctx->ufd.fd = -1;
		ubus_free(ctx->ubus);
		ctx->ubus = NULL;
		return IFCONF_ULOOP_ERR;
	}
	if((ret = ubus_add_object(ctx->ubus, &ifconf_object))) {
		ifconf_cleanup(ctx);
		return IFCONF_UBUS_ADD_OBJECT_ERR;	
	}

	return IFCONF_OK;
}

ifconf_status_t ifconf_sync(struct ifconf_ctx * ctx)
{
	ifconf_status_t ret;

	ret = ifconf_request_links(ctx);
	if(ret != IFCONF_OK)
		return ret;

	return IFCONF_OK;
}

void ifconf_cleanup(struct ifconf_ctx * ctx)
{
	if(!ctx) return;
	if(ctx->ufd.fd >= 0) {
		uloop_fd_delete(&ctx->ufd);
		close(ctx->ufd.fd);
		ctx->ufd.fd = -1;
	}
	ifconf_free(&ctx->ifs);
	if(ctx->ubus) {
		ubus_free(ctx->ubus);
		ctx->ubus = NULL;
	}
	return;
}

char * ifconf_strerr(ifconf_status_t code)
{
	switch(code) {
		case IFCONF_OK: return "Success";
		case IFCONF_SOCK_CREATE_ERR: return "Failed to create socket";
		case IFCONF_SOCK_ERR: return "Netlink socket operation failed";
		case IFCONF_MEM_ERR: return "Could not allocate enough memory";
		case IFCONF_ULOOP_ERR: return "Uloop error";
		case IFCONF_BIND_ERR: return "Failed to bind socket";
		case IFCONF_UBUS_CONNECT_ERR: return "Failed to connect to ubus";
		case IFCONF_UBUS_ADD_OBJECT_ERR: return "Failed to add ubus object";
		default:
			break;
	}
	return "Unknown error";
}
