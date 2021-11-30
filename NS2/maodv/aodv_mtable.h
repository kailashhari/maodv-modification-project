#ifndef __aodv_mtable_h__
#define __aodv_mtable_h__

#include <assert.h>
#include <sys/types.h>

#include <config.h>
#include <lib/bsd-list.h>
#include <scheduler.h>
#include <aodv/aodv_mtable_aux.h>
#include <packet.h>

/* =====================================================================
   Multicast Route Table Entry
   ===================================================================== */
// Internet Class D addresses are or multicast
// 1110 XXXX XXXX XXXX XXXX XXXX XXXX XXXX
// Class D Range: 0xE0000000 - 0xEFFFFFFF
#define IP_MULTICAST 0xE000000

#define INFINITY4 0xffff
#define INFINITY8 0xffffffff

#define MTF_DOWN 0
#define MTF_UP 1
#define MTF_IN_REPAIR 2

#define NOT_ON_TREE 0
#define ON_TREE 1
#define ON_GROUP 2

class aodv_mt_entry
{
	friend class aodv_mtable;
	friend class AODV;
	friend class aodv_nhlist;

public:
	aodv_mt_entry(nsaddr_t dst);
	~aodv_mt_entry(){};

protected:
	LIST_ENTRY(aodv_mt_entry)
	mt_link;

	nsaddr_t mt_dst;	// Multicast Group IP Address
	u_int8_t mt_flags;	// Entry status
	u_int32_t mt_seqno; // Multicast Group Sequence Number
	u_int8_t mt_node_status;
	nsaddr_t mt_grp_leader_addr; // Multicast Group Leader IP Address
	u_int8_t mt_hops_grp_leader; // Hop Count to Multicast Grp Leader

	aodv_nhlist mt_nexthops; // Mcast Grp Next hops IP addresses

	u_int8_t mt_req_last_ttl;
	u_int8_t mt_req_cnt;
	u_int8_t mt_req_times;

	nsaddr_t mt_rep_selected_upstream;
	u_int8_t mt_rep_hops_tree;
	u_int32_t mt_rep_seqno;
	nsaddr_t mt_rep_grp_leader_addr;
	u_int8_t mt_rep_hops_grp_leader;
	double mt_rep_timeout;
	nsaddr_t mt_rep_ipdst;

	double mt_keep_on_tree_timeout;
	nsaddr_t mt_prev_grp_leader_addr;

	nsaddr_t mt_grp_merge_permission;
	double mt_grp_merge_timeout;
};

/* =====================================================================
   The Multicast Routing Table
   ===================================================================== */
class aodv_mtable
{
public:
	aodv_mtable() { LIST_INIT(&mthead); }

	aodv_mt_entry *head() { return mthead.lh_first; }
	aodv_mt_entry *mt_lookup(nsaddr_t id);
	aodv_mt_entry *mt_add(nsaddr_t id);
	void mt_delete(nsaddr_t id);

private:
	LIST_HEAD(, aodv_mt_entry)
	mthead;
};
#endif /* __aodv_mtable_h__ */
