#ifndef IFCONF_IFGROUP_H
#define IFCONF_IFGROUP_H

#include "ifconf.h"

ifconf_status_t ifconf_group_add(struct ifgroup * group,
	struct ifinfo * info);
struct ifinfo * ifconf_find_if(const struct ifconf_ctx * ctx, int ifindex);
ifconf_status_t ifconf_remove_if(struct ifconf_ctx * ctx, int ifindex);
void ifconf_free(struct ifgroup * group);

#endif