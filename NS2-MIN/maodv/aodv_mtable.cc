#include "aodv/aodv_mtable.h"

/* =====================================================================
   The Multicast Routing Table
   ===================================================================== */

aodv_mt_entry::aodv_mt_entry(nsaddr_t id)
{
	mt_dst = id;
	mt_flags = MTF_DOWN;
    mt_node_status = NOT_ON_TREE;
	mt_grp_leader_addr = INFINITY8;
	mt_seqno = 0;
	mt_hops_grp_leader = INFINITY8;
	
	mt_req_last_ttl = 0;
	mt_req_cnt = 0;
	mt_req_times = 0;

	mt_rep_grp_leader_addr = INFINITY8;
	mt_rep_seqno = 0;
	mt_rep_hops_tree = INFINITY4;
	mt_rep_hops_grp_leader = INFINITY4;
	mt_rep_selected_upstream = INFINITY8;
	mt_rep_timeout = 0;
    mt_rep_ipdst = INFINITY8;

    mt_keep_on_tree_timeout = 0;
    mt_prev_grp_leader_addr = INFINITY8;

    mt_grp_merge_permission = INFINITY8;
	mt_grp_merge_timeout = 0;

    // mt_nexthops call aodv_nhlist constructor
};


/* =====================================================================
   The Multicast Routing Table
   ===================================================================== */
aodv_mt_entry* aodv_mtable::mt_lookup(nsaddr_t id)
{
	aodv_mt_entry *mt = mthead.lh_first;

	for(; mt; mt = mt->mt_link.le_next) {
		if(mt->mt_dst == id)
		break;
	}
	return mt;
}

void
aodv_mtable::mt_delete(nsaddr_t id)
{
	aodv_mt_entry *mt = mt_lookup(id);

	if(mt) {
		LIST_REMOVE(mt, mt_link);
		delete mt;
	}
}

aodv_mt_entry*
aodv_mtable::mt_add(nsaddr_t id)
{
	aodv_mt_entry *mt;

	mt = new aodv_mt_entry(id);
	assert(mt);

	LIST_INSERT_HEAD(&mthead, mt, mt_link);

	return mt;
}

