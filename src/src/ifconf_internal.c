#include "conflicts.h"
#include "ifgroup.h"

#include <arpa/inet.h>
#include <libubox/blobmsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ifconf_status_t ifconf_subnet_conflicts_to_blob(struct blob_buf * blob, const struct ifconf_subnet_conflicts * conflicts)
{
	void * conflict_array;

	if(!blob || !conflicts)
		return IFCONF_ERR;
	if(conflicts->count && !conflicts->conflicts)
		return IFCONF_NETLINK_DATA_ERR;

	conflict_array = blobmsg_open_array(blob, "conflicts");
	if(!conflict_array)
		return IFCONF_MEM_ERR;

	for(uint16_t i = 0; i < conflicts->count; i++) {
		const struct subnet_list * conflict = &conflicts->conflicts[i];
		void * conflict_table;
		void * subnet_array;

		if(conflict->count && !conflict->subnets)
			return IFCONF_NETLINK_DATA_ERR;

		conflict_table = blobmsg_open_table(blob, NULL);
		if(!conflict_table)
			return IFCONF_MEM_ERR;
		subnet_array = blobmsg_open_array(blob, "subnets");
		if(!subnet_array)
			return IFCONF_MEM_ERR;

		for(uint16_t j = 0; j < conflict->count; j++) {
			const struct ifsubnet * subnet = &conflict->subnets[j];
			const void * raw_address;
			char address[INET6_ADDRSTRLEN];
			void * subnet_table;

			if(!subnet->subnet || !subnet->interface)
				return IFCONF_NETLINK_DATA_ERR;
			switch(subnet->subnet->family) {
			case AF_INET:
				raw_address = &subnet->subnet->addr.v4;
				break;
			case AF_INET6:
				raw_address = &subnet->subnet->addr.v6;
				break;
			default:
				return IFCONF_NETLINK_DATA_ERR;
			}
			if(!inet_ntop(subnet->subnet->family, raw_address,
				address, sizeof(address)))
				return IFCONF_NETLINK_DATA_ERR;

			subnet_table = blobmsg_open_table(blob, NULL);
			if(!subnet_table)
				return IFCONF_MEM_ERR;
			blobmsg_add_string(blob, "interface", subnet->interface->ifname);
			blobmsg_add_string(blob, "address", address);
			blobmsg_add_u32(blob, "prefixlen", subnet->subnet->prefixlen);
			blobmsg_close_table(blob, subnet_table);
		}

		blobmsg_close_array(blob, subnet_array);
		blobmsg_close_table(blob, conflict_table);
	}

	blobmsg_close_array(blob, conflict_array);
	return IFCONF_OK;
}

static uint32_t prefixlen_to_mask(uint8_t prefixlen)
{
	if(prefixlen == 0)
		return 0;
	if(prefixlen >= 32)
		return UINT32_MAX;
	return UINT32_MAX << (32 - prefixlen);
}

int ifconf_subnet_overlap(const struct ifconf_addr *left,
							 const struct ifconf_addr *right)
{
	uint8_t common_prefix;
	uint8_t full_bytes;
	uint8_t remaining_bits;

	if(!left || !right || left->family != right->family)
		return 0;

	switch(left->family) {
	case AF_INET:
		if(left->prefixlen > 32 || right->prefixlen > 32)
			return 0;
		common_prefix = left->prefixlen < right->prefixlen ?
			left->prefixlen : right->prefixlen;
		return (ntohl(left->addr.v4.s_addr) & prefixlen_to_mask(common_prefix)) ==
			(ntohl(right->addr.v4.s_addr) & prefixlen_to_mask(common_prefix));
	case AF_INET6:
		if(left->prefixlen > 128 || right->prefixlen > 128)
			return 0;
		common_prefix = left->prefixlen < right->prefixlen ?
			left->prefixlen : right->prefixlen;
		full_bytes = common_prefix / 8;
		remaining_bits = common_prefix % 8;
		if(memcmp(&left->addr.v6, &right->addr.v6, full_bytes) != 0)
			return 0;
		if(!remaining_bits)
			return 1;
		return (left->addr.v6.s6_addr[full_bytes] &
			(0xffu << (8 - remaining_bits))) ==
			(right->addr.v6.s6_addr[full_bytes] &
			(0xffu << (8 - remaining_bits)));
	default:
		return 0;
	}
}

static void ifconf_subnet_list_cleanup(struct subnet_list * list)
{
	if(!list)
		return;
	free(list->subnets);
	memset(list, 0, sizeof(*list));
}

static int ifconf_subnet_entry_equal(const struct ifsubnet * left,
	const struct ifsubnet * right)
{
	return left && right && left->interface == right->interface &&
		ifconf_addr_equal(left->subnet, right->subnet);
}

static int ifconf_subnet_lists_related(const struct subnet_list * left,
	const struct subnet_list * right)
{
	if(!left || !right)
		return 0;

	for(uint16_t i = 0; i < left->count; i++) {
		for(uint16_t j = 0; j < right->count; j++) {
			if(ifconf_subnet_overlap(left->subnets[i].subnet,
				right->subnets[j].subnet))
				return 1;
		}
	}
	return 0;
}

static ifconf_status_t ifconf_subnet_list_merge(struct subnet_list * target,
	const struct subnet_list * source)
{
	if(!target || !source)
		return IFCONF_ERR;

	for(uint16_t i = 0; i < source->count; i++) {
		int duplicate = 0;
		for(uint16_t j = 0; j < target->count; j++) {
			if(ifconf_subnet_entry_equal(&source->subnets[i],
				&target->subnets[j])) {
				duplicate = 1;
				break;
			}
		}
		if(!duplicate && ifconf_subnet_list_add(target,
			source->subnets[i].subnet, source->subnets[i].interface) != IFCONF_OK)
			return IFCONF_MEM_ERR;
	}
	return IFCONF_OK;
}

ifconf_status_t ifconf_subnet_list_add(struct subnet_list * list,
	struct ifconf_addr * subnet, struct ifinfo * interface)
{
	struct ifsubnet * tmp;
	size_t new_size;

	if(!list || !subnet || !interface)
		return IFCONF_ERR;
	for(uint16_t i = 0; i < list->count; i++) {
		if(list->subnets[i].interface == interface &&
			ifconf_addr_equal(list->subnets[i].subnet, subnet))
			return IFCONF_OK;
	}
	if(list->count == UINT16_MAX)
		return IFCONF_MEM_ERR;

	if(list->count == list->size) {
		new_size = list->size ? (size_t)list->size * 2 : 2;
		if(new_size > UINT16_MAX)
			new_size = UINT16_MAX;
		tmp = realloc(list->subnets, new_size * sizeof(*tmp));
		if(!tmp)
			return IFCONF_MEM_ERR;
		list->subnets = tmp;
		list->size = (uint16_t)new_size;
	}

	list->subnets[list->count].subnet = subnet;
	list->subnets[list->count].interface = interface;
	list->count++;
	return IFCONF_OK;
}

ifconf_status_t ifconf_subnet_conflicts_add(
	struct ifconf_subnet_conflicts * conflicts, struct subnet_list * list)
{
	struct subnet_list * tmp;
	size_t new_size;

	if(!conflicts || !list)
		return IFCONF_ERR;
	if(!list->count) {
		ifconf_subnet_list_cleanup(list);
		return IFCONF_OK;
	}

	for(uint16_t i = 0; i < conflicts->count;) {
		struct subnet_list * existing = &conflicts->conflicts[i];
		if(!ifconf_subnet_lists_related(list, existing)) {
			i++;
			continue;
		}
		if(ifconf_subnet_list_merge(list, existing) != IFCONF_OK)
			return IFCONF_MEM_ERR;
		free(existing->subnets);
		memmove(existing, existing + 1,
			(conflicts->count - i - 1) * sizeof(*existing));
		conflicts->count--;
	}
	if(conflicts->count == UINT16_MAX)
		return IFCONF_MEM_ERR;

	if(conflicts->count == conflicts->size) {
		new_size = conflicts->size ? (size_t)conflicts->size * 2 : 2;
		if(new_size > UINT16_MAX)
			new_size = UINT16_MAX;
		tmp = realloc(conflicts->conflicts, new_size * sizeof(*tmp));
		if(!tmp)
			return IFCONF_MEM_ERR;
		conflicts->conflicts = tmp;
		conflicts->size = (uint16_t)new_size;
	}

	conflicts->conflicts[conflicts->count++] = *list;
	memset(list, 0, sizeof(*list));
	return IFCONF_OK;
}

ifconf_status_t ifconf_subnet(struct ifgroup * ifs,
	struct ifconf_subnet_conflicts * conflicts)
{
    if(!ifs || !conflicts)
        return IFCONF_ERR;

    for(uint16_t i = 0; i < ifs->count; i++) {
		struct ifinfo * left_interface = ifs->ifs[i];
		if(!left_interface)
			continue;

		for(uint16_t j = i + 1; j < ifs->count; j++) {
			struct ifinfo * right_interface = ifs->ifs[j];
			struct subnet_list list = {0};
			if(!right_interface)
				continue;

			for(uint16_t left_index = 0;
				left_index < left_interface->addr_count; left_index++) {
				struct ifconf_addr * left_address =
					&left_interface->addrs[left_index];
				for(uint16_t right_index = 0;
					right_index < right_interface->addr_count; right_index++) {
					struct ifconf_addr * right_address =
						&right_interface->addrs[right_index];

					if(!ifconf_subnet_overlap(left_address, right_address))
						continue;
					if(ifconf_subnet_list_add(&list, left_address,
						left_interface) != IFCONF_OK ||
						ifconf_subnet_list_add(&list, right_address,
							right_interface) != IFCONF_OK) {
						ifconf_subnet_list_cleanup(&list);
						return IFCONF_MEM_ERR;
					}
				}
			}
			if(list.count && ifconf_subnet_conflicts_add(conflicts, &list) != IFCONF_OK) {
				ifconf_subnet_list_cleanup(&list);
				return IFCONF_MEM_ERR;
			}
		}
	}

    return IFCONF_OK;
}

// ---------------------- Utils --------------------------

void ifconf_free(struct ifgroup * group)
{
	if(!group) return;
	if(group->ifs) {
		for(int i = 0; i < group->count; i++) {
			struct ifinfo * info = group->ifs[i];
			group->ifs[i] = NULL;
			if(!info) continue;
			free(info->addrs);
			free(info);
		}
	}
	free(group->ifs);
	memset(group, 0, sizeof(struct ifgroup));
	return;
}

void ifconf_free_subnet_conflicts(struct ifconf_subnet_conflicts * conflicts)
{
	if(!conflicts)
		return;
	if(conflicts->conflicts) {
		for(uint16_t i = 0; i < conflicts->count; i++)
			free(conflicts->conflicts[i].subnets);
	}
	free(conflicts->conflicts);
	memset(conflicts, 0, sizeof(*conflicts));
}


ifconf_status_t ifconf_group_add(struct ifgroup * group, struct ifinfo * info)
{
	struct ifinfo ** tmp;
	size_t new_size;

	if(!group || !info)
		return IFCONF_ERR;

	for(uint16_t i = 0; i < group->count; i++) {
		if(group->ifs[i] == info)
			return IFCONF_OK;
	}

	if(group->count == UINT16_MAX)
		return IFCONF_MEM_ERR;

	if(group->count == group->size) {
		new_size = group->size ? group->size * 2 : 4;
		if(new_size > UINT16_MAX)
			new_size = UINT16_MAX;
		tmp = realloc(group->ifs, sizeof(*group->ifs) * new_size);
		if(!tmp)
			return IFCONF_MEM_ERR;
		group->ifs = tmp;
		group->size = new_size;
	}
	group->ifs[group->count++] = info;

	return IFCONF_OK;
}

struct ifinfo * ifconf_find_if(const struct ifconf_ctx * ctx, int ifindex)
{
	struct ifinfo * cur;
	for(uint16_t i = 0; i < ctx->ifs.count; i++) {
		cur = ctx->ifs.ifs[i];
		if(cur && cur->ifindex == ifindex)
			return cur;
	}
	return NULL;
}

int ifconf_addr_equal(const struct ifconf_addr * left, const struct ifconf_addr * right)
{
	if(!left || !right || left->family != right->family ||
		left->prefixlen != right->prefixlen)
		return 0;

	switch(left->family) {
	case AF_INET:
		return memcmp(&left->addr.v4, &right->addr.v4,
			sizeof(left->addr.v4)) == 0;
	case AF_INET6:
		return memcmp(&left->addr.v6, &right->addr.v6,
			sizeof(left->addr.v6)) == 0;
	default:
		return 0;
	}
}

ifconf_status_t ifconf_remove_if(struct ifconf_ctx * ctx, int ifindex)
{
	struct ifinfo * cur;
	for(uint16_t i = 0; i < ctx->ifs.count; i++) {
		cur = ctx->ifs.ifs[i];
		if(cur && cur->ifindex == ifindex) {
			free(cur->addrs);
			free(cur);
			memmove(&ctx->ifs.ifs[i],
					&ctx->ifs.ifs[i + 1],
					(ctx->ifs.count - i - 1) * sizeof(*ctx->ifs.ifs));
			ctx->ifs.count--;
			ctx->ifs.ifs[ctx->ifs.count] = NULL;

			if(!ctx->ifs.count) {
				free(ctx->ifs.ifs);
				ctx->ifs.ifs = NULL;
				ctx->ifs.size = 0;
			} else if(ctx->ifs.size > 8 &&
					  ctx->ifs.count <= ctx->ifs.size / 4) {
				size_t new_size = ctx->ifs.size / 2;
				if(new_size < 8)
					new_size = 8;
				void * tmp = realloc(ctx->ifs.ifs,
								 sizeof(*ctx->ifs.ifs) * new_size);
				if(tmp) {
					ctx->ifs.ifs = tmp;
					ctx->ifs.size = new_size;
				}
			}
			return IFCONF_OK;
		}
	}
	return IFCONF_OK;
}
