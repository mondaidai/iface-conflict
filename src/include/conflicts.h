#ifndef IFCONF_CONFLICTS_H
#define IFCONF_CONFLICTS_H

#include "ifconf.h"

struct blob_buf;

struct ifsubnet {
	struct ifconf_addr * subnet;
	struct ifinfo * interface;
};

struct subnet_list {
	struct ifsubnet * subnets;
	uint16_t count;
	uint16_t size;
};

struct ifconf_subnet_conflicts {
	struct subnet_list * conflicts;
	uint16_t count;
	uint16_t size;
};

ifconf_status_t ifconf_subnet_conflicts_to_blob(
	struct blob_buf * blob,
	const struct ifconf_subnet_conflicts * conflicts
);

void ifconf_free_subnet_conflicts(struct ifconf_subnet_conflicts * conflicts);

ifconf_status_t ifconf_subnet_list_add(
	struct subnet_list * list,
	struct ifconf_addr * subnet, struct ifinfo * interface
);

ifconf_status_t ifconf_subnet_conflicts_add(
	struct ifconf_subnet_conflicts * conflicts, 
	struct subnet_list * list
);

int ifconf_addr_equal(
	const struct ifconf_addr * left,
	const struct ifconf_addr * right
);

int ifconf_subnet_overlap(
	const struct ifconf_addr * left,
	const struct ifconf_addr * right
);

ifconf_status_t ifconf_subnet(
	struct ifgroup * ifs,
	struct ifconf_subnet_conflicts * conflicts
);

#endif