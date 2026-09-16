#include "netlink.h"
#include "conflicts.h"
#include "ifgroup.h"

#include <netinet/in.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <stdlib.h>

static ifconf_status_t ifconf_parse_addr(const struct ifaddrmsg * ifa, size_t attr_len, struct ifconf_addr * addr)
{
	struct rtattr * rta;
	struct rtattr * raw_rta = NULL;
	struct rtattr * local_rta = NULL;

	memset(addr, 0, sizeof(*addr));
	addr->family = ifa->ifa_family;
	addr->prefixlen = ifa->ifa_prefixlen;

	for(rta = IFA_RTA(ifa); RTA_OK(rta, attr_len); rta = RTA_NEXT(rta, attr_len)) {
		switch(rta->rta_type) {
		case IFA_ADDRESS:
			raw_rta = rta;
			break;
		case IFA_LOCAL:
			local_rta = rta;
			break;
		}
	}

	switch(addr->family) {
	case AF_INET:
		if(!local_rta) local_rta = raw_rta;
		if(!local_rta || RTA_PAYLOAD(local_rta) < sizeof(addr->addr.v4))
			return IFCONF_NETLINK_DATA_ERR;
		memcpy(&addr->addr.v4, RTA_DATA(local_rta), sizeof(addr->addr.v4));
		return IFCONF_OK;
	case AF_INET6:
		if(!raw_rta || RTA_PAYLOAD(raw_rta) < sizeof(addr->addr.v6))
			return IFCONF_NETLINK_DATA_ERR;
		memcpy(&addr->addr.v6, RTA_DATA(raw_rta), sizeof(addr->addr.v6));
		return IFCONF_OK;
	default:
		return IFCONF_NETLINK_DATA_ERR;
	}
}

static ifconf_status_t handle_newlink(struct ifconf_ctx * ctx, struct nlmsghdr * nlh)
{	
	struct ifinfomsg * ifi = NLMSG_DATA(nlh);
	int ifindex = ifi->ifi_index;
	unsigned int flags = ifi->ifi_flags;
	char * ifname = NULL;
	size_t ifname_len = 0;
	uint8_t * mac = NULL;

	struct rtattr * rta;
	int len = IFLA_PAYLOAD(nlh);

	for(rta = IFLA_RTA(ifi);
		RTA_OK(rta, len);
		rta = RTA_NEXT(rta, len)) {

		switch(rta->rta_type) {
		case IFLA_IFNAME:
			if(RTA_PAYLOAD(rta) > 0) {
				ifname = (char *)RTA_DATA(rta);
				ifname_len = RTA_PAYLOAD(rta);
			}
			break;
		case IFLA_ADDRESS:
			if(RTA_PAYLOAD(rta) >= ETH_ALEN)
				mac = RTA_DATA(rta);
			break;
		}
	}
	if(ifindex <= 0) return IFCONF_NETLINK_DATA_ERR;
	struct ifinfo * cur = ifconf_find_if(ctx, ifindex);
	if(cur) {
		if(ifname) {
			size_t copy_len = ifname_len < IFNAMSIZ - 1 ? ifname_len : IFNAMSIZ - 1;
			memcpy(cur->ifname, ifname, copy_len);
			cur->ifname[copy_len] = '\0';
		}
		if(mac) {
			memcpy(cur->mac, mac, ETH_ALEN);
		}
		cur->flags = flags;
		return IFCONF_OK;
	}
	struct ifinfo * new = (struct ifinfo *)calloc(1, sizeof(struct ifinfo));
	if(!new) return IFCONF_MEM_ERR;
	new->ifindex = ifindex;
	if(ifname) {
		size_t copy_len = ifname_len < IFNAMSIZ - 1 ? ifname_len : IFNAMSIZ - 1;
		memcpy(new->ifname, ifname, copy_len);
		new->ifname[copy_len] = '\0';
	}
	if(mac) {
		memcpy(new->mac, mac, ETH_ALEN);
	}
	new->flags = flags;

	if(ifconf_group_add(&ctx->ifs, new) != IFCONF_OK) {
		free(new);
		return IFCONF_MEM_ERR;
	}
	return IFCONF_OK;
}

static ifconf_status_t handle_dellink(struct ifconf_ctx * ctx, struct nlmsghdr * nlh)
{
	struct ifinfomsg * ifi = NLMSG_DATA(nlh);
	if(ifi->ifi_index <= 0) return IFCONF_NETLINK_DATA_ERR;
	return ifconf_remove_if(ctx, ifi->ifi_index);
}

static ifconf_status_t handle_newaddr(struct ifconf_ctx * ctx, struct nlmsghdr * nlh)
{
	struct ifaddrmsg * ifa = NLMSG_DATA(nlh);
	struct ifinfo * info;
	struct ifconf_addr addr;

	if(ifa->ifa_index <= 0)
		return IFCONF_NETLINK_DATA_ERR;
	info = ifconf_find_if(ctx, ifa->ifa_index);
	if(!info)
		return IFCONF_NETLINK_DATA_ERR;
	if(ifconf_parse_addr(ifa, IFA_PAYLOAD(nlh), &addr) != IFCONF_OK)
		return IFCONF_NETLINK_DATA_ERR;

#if IFCONF_IGNORE_IPV6_LINKLOCAL
	if(addr.family == AF_INET6 &&
		IN6_IS_ADDR_LINKLOCAL(&addr.addr.v6))
		return IFCONF_OK;
#endif

	for(uint16_t i = 0; i < info->addr_count; i++) {
		if(ifconf_addr_equal(&info->addrs[i], &addr))
			return IFCONF_OK;
	}

	if(info->addr_count == UINT16_MAX)
		return IFCONF_MEM_ERR;
	struct ifconf_addr * addrs = realloc(info->addrs,
							 (info->addr_count + 1) * sizeof(*addrs));
	if(!addrs)
		return IFCONF_MEM_ERR;
	info->addrs = addrs;
	info->addrs[info->addr_count++] = addr;
	return IFCONF_OK;
}

static ifconf_status_t handle_deladdr(struct ifconf_ctx * ctx, struct nlmsghdr * nlh)
{
	struct ifaddrmsg * ifa = NLMSG_DATA(nlh);
	struct ifinfo * info;
	struct ifconf_addr addr;

	if(ifa->ifa_index <= 0)
		return IFCONF_NETLINK_DATA_ERR;
	info = ifconf_find_if(ctx, ifa->ifa_index);
	if(!info)
		return IFCONF_NETLINK_DATA_ERR;
	if(ifconf_parse_addr(ifa, IFA_PAYLOAD(nlh), &addr) != IFCONF_OK)
		return IFCONF_NETLINK_DATA_ERR;

	for(uint16_t i = 0; i < info->addr_count; i++) {
		if(ifconf_addr_equal(&info->addrs[i], &addr)) {
			memmove(&info->addrs[i], &info->addrs[i + 1],
					(info->addr_count - i - 1) * sizeof(*info->addrs));
			info->addr_count--;
			if(!info->addr_count) {
				free(info->addrs);
				info->addrs = NULL;
			}
			return IFCONF_OK;
		}
	}

	return IFCONF_OK;
}


static ifconf_status_t ifconf_validate_netlink_message(const struct nlmsghdr * nlh)
{
	if(!nlh)
		return IFCONF_NETLINK_DATA_ERR;

	switch(nlh->nlmsg_type) {
	case RTM_NEWLINK:
	case RTM_DELLINK:
		if(nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct ifinfomsg)))
			return IFCONF_NETLINK_DATA_ERR;
		break;
	case RTM_NEWADDR:
	case RTM_DELADDR:
		if(nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct ifaddrmsg)))
			return IFCONF_NETLINK_DATA_ERR;
		break;
	case NLMSG_ERROR:
		if(nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
			return IFCONF_NETLINK_DATA_ERR;
		break;
	case NLMSG_DONE:
		if(nlh->nlmsg_len < NLMSG_LENGTH(0))
			return IFCONF_NETLINK_DATA_ERR;
		break;
	default:
		break;
	}

	return IFCONF_OK;
}


int handle_netlink_message(struct ifconf_ctx * ctx, const void * recvbuf, size_t n)
{
    int ret_flags = NETLINK_NO_OP;
    int len = (int)n;
	struct nlmsghdr * nlh;
	for(nlh = (struct nlmsghdr *)recvbuf;
		NLMSG_OK(nlh, len);
		nlh = NLMSG_NEXT(nlh, len)) {
		ifconf_status_t ret = ifconf_validate_netlink_message(nlh);
		if(ret != IFCONF_OK) {
			syslog(LOG_WARNING, "netlink_cb: invalid message type=%u length=%u", nlh->nlmsg_type, nlh->nlmsg_len);
				continue;
		}
		switch(nlh->nlmsg_type) {
		    case RTM_NEWLINK:
			    ret_flags |= NETLINK_NEWLINK;
			    ret = handle_newlink(ctx, nlh);
			    break;
		    case RTM_DELLINK:
			    ret_flags |= NETLINK_DELLINK;
			    ret = handle_dellink(ctx, nlh);
			    break;
		    case RTM_NEWADDR:
			    ret_flags |= NETLINK_NEWADDR;
			    ret = handle_newaddr(ctx, nlh);
			    break;
		    case RTM_DELADDR:
			    ret_flags |= NETLINK_DELADDR;
			    ret = handle_deladdr(ctx, nlh);
			    break;
		    case NLMSG_DONE:
			    if(nlh->nlmsg_seq == ctx->link_seq) {
				    ret = ifconf_request_addrs(ctx);
				    if(ret != IFCONF_OK)
					    syslog(LOG_WARNING, "netlink_cb: address dump request error: %s", ifconf_strerr(ret));
			    }
			    break;
		    case NLMSG_ERROR: {
                ret_flags |= NETLINK_ERR;
			    struct nlmsgerr * err = NLMSG_DATA(nlh);
			    if(err->error != 0)
				    syslog(LOG_WARNING,
					    "netlink error: seq=%u req_seq=%u type=%u err=%d (%s)",
					    nlh->nlmsg_seq,
					    err->msg.nlmsg_seq,
					    err->msg.nlmsg_type,
					    -err->error,
					    strerror(-err->error));
			    break;
		    }
		    default:
			    break;
		}
		if(ret != IFCONF_OK)
			syslog(LOG_WARNING, 
                "netlink_cb: message type=%u error: %s",
				 nlh->nlmsg_type, ifconf_strerr(ret));
	}
    return ret_flags;
}
ifconf_status_t ifconf_request_links(struct ifconf_ctx * ctx)
{
	struct {
		struct nlmsghdr nlh;
		struct rtgenmsg gen;
	} req = {0};

	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
	req.nlh.nlmsg_type = RTM_GETLINK;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	ctx->link_seq = ++ctx->seq;
	req.nlh.nlmsg_seq = ctx->link_seq;

	req.gen.rtgen_family = AF_PACKET;

	if(send(ctx->ufd.fd, &req, req.nlh.nlmsg_len, 0) < 0)
		return IFCONF_SOCK_ERR;

	return IFCONF_OK;
}

ifconf_status_t ifconf_request_addrs(struct ifconf_ctx * ctx)
{
	struct {
		struct nlmsghdr nlh;
		struct rtgenmsg gen;
	} req = {0};

	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
	req.nlh.nlmsg_type = RTM_GETADDR;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	ctx->addr_seq = ++ctx->seq;
	req.nlh.nlmsg_seq = ctx->addr_seq;

	req.gen.rtgen_family = AF_UNSPEC;

	if(send(ctx->ufd.fd, &req, req.nlh.nlmsg_len, 0) < 0)
		return IFCONF_SOCK_ERR;

	return IFCONF_OK;
}