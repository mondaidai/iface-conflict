#include "ubus_api.h"
#include "conflicts.h"
#include "netlink.h"

#include <errno.h>
#include <libubox/blobmsg_json.h>
#include <libubox/utils.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>

#define IFCONF_RECV_BUFSZ 4096

struct ubus_object_type ifconf_type = {
	.name = IFCONF_UBUS_OBJ_NAME,
	.id = 0,
	.methods = NULL,
	.n_methods = 0,
};

struct ubus_object ifconf_object = {
	.name = IFCONF_UBUS_OBJ_NAME,
	.type = &ifconf_type,
	.methods = NULL,
	.n_methods = 0,
};


static ifconf_status_t notify_event(struct ubus_context * ctx, const char * event, struct blob_buf *blob)
{
	int ret;

#ifdef DEBUG
	char *json = blobmsg_format_json(blob->head, true);

	if(json) {
		syslog(LOG_DEBUG, "%s: %s", event, json);
		free(json);
	}
#endif

	ret = ubus_notify(ctx, &ifconf_object, event, blob->head, -1);

	return ret == 0 ? IFCONF_OK : IFCONF_ERR;
}

void netlink_cb(struct uloop_fd * ufd, unsigned int events)
{
	struct ifconf_ctx * ctx;
	int actions = 0;
	if(!ufd)
		return;
	if(ufd->error) {
		syslog(LOG_WARNING, "netlink_cb: netlink socket error");
		return;
	}
	ctx = container_of(ufd, struct ifconf_ctx, ufd);
	if(!(events & ULOOP_READ))
		return;
	
	char recvbuf[IFCONF_RECV_BUFSZ];
	
	for(;;) {
		ssize_t n = recv(ufd->fd, recvbuf, sizeof(recvbuf), 0);

		if(n < 0) {
			if(errno == EINTR)
				continue;
			if(errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			syslog(LOG_WARNING, "netlink_cb: recv error: %s", strerror(errno));
			break;
		}
		if(n == 0)
			break;

		// updates context structure
		actions |= handle_netlink_message(ctx, recvbuf, n);
	}

	ifconf_status_t ret;
	
	if(actions & NETLINK_NEWLINK || actions & NETLINK_NEWADDR) {
		struct ifconf_subnet_conflicts conflicts = {0};
		ret = ifconf_subnet(&ctx->ifs, &conflicts);
		if(ret != IFCONF_OK) {
			syslog(LOG_WARNING, "netlink_cb: ifconf_subnet error: %s", ifconf_strerr(ret));
			ifconf_free_subnet_conflicts(&conflicts);
			return;
		}
		if(conflicts.count > 0) {
			struct blob_buf blob = {0};
			if(blob_buf_init(&blob, 0) != 0) {
				syslog(LOG_WARNING, "netlink_cb: failed to initialize blob buffer");
			} else if(ifconf_subnet_conflicts_to_blob(&blob, &conflicts) == IFCONF_OK) {
				ret = notify_event(ctx->ubus, "subnet_conflicts", &blob);
				if(ret != IFCONF_OK)
					syslog(LOG_WARNING, "netlink_cb: %u subnet conflicts detected",
					   conflicts.count);
			}
			blob_buf_free(&blob);
		}
		ifconf_free_subnet_conflicts(&conflicts);
	}
	
	return;
}