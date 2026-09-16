#ifndef IFCONF_TYPES_H
#define IFCONF_TYPES_H

#include <linux/if_ether.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>

struct ifconf_addr {
	sa_family_t family;

	union {
		struct in_addr v4;
		struct in6_addr v6;
	} addr;

	uint8_t prefixlen;
};

struct ifinfo {
	int ifindex;
	char ifname[IFNAMSIZ];

	uint8_t mac[ETH_ALEN];
	unsigned int flags;

	struct ifconf_addr * addrs;
	uint16_t addr_count;
};

struct ifgroup {
	struct ifinfo ** ifs;
	uint16_t count;
	uint16_t size;
};

#endif