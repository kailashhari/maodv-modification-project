#include <aodv/aodv.h>
#include <aodv/aodv_packet.h>
#include <ip.h>
#include <random.h>
#include <cmu-trace.h>

#define max(a, b) ((a) > (b) ? (a) : (b))

#define INITIAL_ENERGY 2.0
#define THRESHOLD 0.5

/****************************************************************/
// Timer to keep fresh packet ids
/****************************************************************/
void PacketTimer::handle(Event *e)
{
    agent->pid_purge();
    Scheduler::instance().schedule(this, &intr, BCAST_ID_SAVE);
}

void AODV::pid_insert(nsaddr_t addr, u_int32_t pid)
{

    PacketID *b = new PacketID(addr, pid);
    assert(b);
    b->expire = CURRENT_TIME + BCAST_ID_SAVE;

    LIST_INSERT_HEAD(&pihead, b, plink);
}

bool AODV::pid_lookup(nsaddr_t addr, u_int32_t pid)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    PacketID *b = pihead.lh_first;

    for (; b; b = b->plink.le_next)
    {
        if ((b->src == addr) && (b->id == pid))
            return true;
    }

    return false;
}

void AODV::pid_purge()
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    PacketID *b = pihead.lh_first;
    PacketID *bn;
    double now = CURRENT_TIME;

    for (; b; b = bn)
    {
        bn = b->plink.le_next;
        if (b->expire <= now)
        {
            LIST_REMOVE(b, plink);
            delete b;
        }
    }
}

/**************************************************************/
// periodically checking if there is any branch should be pruned
/**************************************************************/
void PruneTimer::handle(Event *e)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    Packet *p = (Packet *)e;
    struct hdr_ip *ih = HDR_IP(p);
    agent->mt_prune(ih->daddr());
    Packet::free(p);
}

void AODV::mt_prune(nsaddr_t dst)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    aodv_mt_entry *mt = mtable.mt_lookup(dst);
    if (mt && mt->mt_node_status == ON_TREE && mt->mt_nexthops.downstream() == NULL)
    {
        if (mt->mt_keep_on_tree_timeout > CURRENT_TIME)
            setPruneTimer(mt);
        else
        {
            aodv_nh_entry *nh = mt->mt_nexthops.upstream();
            if (nh)
            {
                sendMACT(mt->mt_dst, MACT_P, 0, nh->next_hop);
                mt->mt_nexthops.remove(nh);
            }
            downMT(mt);
        }
    }
}

/**************************************************************/
// periodically broadcasting group hello throughout the network
/*************************************************************/
void GroupHelloTimer::handle(Event *e)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    agent->sendMGRPH();
    Scheduler::instance().schedule(this, &intr,
                                   0.9 * GROUP_HELLO_INTERVAL + 0.2 * GROUP_HELLO_INTERVAL * Random::uniform());
}

void AODV::sendMGRPH()
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    aodv_mt_entry *mt;
    for (mt = mtable.head(); mt; mt = mt->mt_link.le_next)
    {
        if (mt->mt_grp_leader_addr == index && mt->mt_grp_merge_timeout <= CURRENT_TIME)
        {
            if (mt->mt_hops_grp_leader != 0)
            {
                printf("******ERROR: %.9f in %s, node %i is group leader for group %d, BUT HOP COUNT IS NOT 0!\n",
                       CURRENT_TIME, __FUNCTION__, index, mt->mt_dst);
                exit(1);
            }
            if (mt->mt_nexthops.upstream() != NULL)
            {
                printf("******ERROR: %.9f in %s,  node %i is group leader for group %d, BUT IT HAS UPSTEAM!\n",
                       CURRENT_TIME, __FUNCTION__, index, mt->mt_dst);
                exit(1);
            }

            Packet *p = Packet::alloc();
            struct hdr_cmn *ch = HDR_CMN(p);
            struct hdr_ip *ih = HDR_IP(p);
            struct hdr_aodv_grph *gh = HDR_AODV_GRPH(p);

            gh->gh_type = AODVTYPE_GRPH;
            gh->gh_flags = GRPH_NO_FLAG;
            gh->gh_hop_count = 0;
            gh->gh_grp_leader_addr = index;
            gh->gh_multi_grp_addr = mt->mt_dst;
            mt->mt_seqno++;
            gh->gh_grp_seqno = mt->mt_seqno;

            ch->ptype() = PT_AODV;
            ch->size() = IP_HDR_LEN + gh->size();
            ch->iface() = -2;
            ch->error() = 0;
            ch->addr_type() = NS_AF_NONE;
            ch->prev_hop_ = index;
            ch->next_hop_ = MAC_BROADCAST;
            ch->direction() = hdr_cmn::DOWN;

            ih->saddr() = index;
            ih->daddr() = IP_BROADCAST;
            ih->sport() = RT_PORT;
            ih->dport() = RT_PORT;
            ih->ttl_ = NETWORK_DIAMETER;

            id_insert(gh->gh_multi_grp_addr + gh->gh_grp_leader_addr, gh->gh_grp_seqno);

            controlNextHello();

            Scheduler::instance().schedule(target_, p, 0.01 * Random::uniform());
        }
    }
}

/******************************************************************/
// receive GROUP HELLO and rebroadcast
// in order to find partitioned tree
/*****************************************************************/
void AODV::recvMGRPH(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_aodv_grph *gh = HDR_AODV_GRPH(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);

    // exclude GRPH_U
    if (gh->gh_flags == GRPH_U)
    {
        recvMGRPH_U(p);
        return;
    }

    // hop count +1
    gh->gh_hop_count++;

    // 1. not-on-tree node forward the msg
    // 2. tree member:
    //  2.1 if not knowing grp leader, discard the msg
    //  2.2 if with same grp leader, must receive the msg from its upstream, then forward it;
    //      otherwise, discard it
    //  2.2 if with different grp leader:
    //    a) if with bigger grp leader, discard the msg
    //    b) if with smaller grp leader, initiate Reqeust, discard the msg
    //       RREQ_R to its own grp leader if it has valid upstream
    //       RREQ_JR if it is the grp leader and has not permitted other node to request merge

    aodv_mt_entry *mt = mtable.mt_lookup(gh->gh_multi_grp_addr);

    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE ||
        mt->mt_grp_leader_addr == INFINITY8 ||
        mt->mt_grp_leader_addr != gh->gh_grp_leader_addr)
    {

        // only handle the first received msg
        if (id_lookup(gh->gh_multi_grp_addr + gh->gh_grp_leader_addr, gh->gh_grp_seqno))
        {
            Packet::free(p);
            return;
        }
        id_insert(gh->gh_multi_grp_addr + gh->gh_grp_leader_addr, gh->gh_grp_seqno);

        // update grp leader table entry
        aodv_glt_entry *glt = gltable.glt_lookup(gh->gh_multi_grp_addr);
        if (glt == NULL)
            glt = gltable.glt_add(gh->gh_multi_grp_addr);
        if ((glt->glt_expire <= CURRENT_TIME) || (glt->glt_grp_leader_addr < gh->gh_grp_leader_addr))
        {
            glt->glt_grp_leader_addr = gh->gh_grp_leader_addr;
            glt->glt_next_hop = ch->prev_hop_;
            glt->glt_expire = CURRENT_TIME + GROUP_HELLO_INTERVAL;
        }

        if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
        {
            gh->gh_flags == GRPH_M;
            mt_forward(p, DELAY);
        }
        else if (mt->mt_grp_leader_addr == INFINITY8)
            Packet::free(p);
        else
        {
            if (mt->mt_grp_leader_addr < gh->gh_grp_leader_addr &&
                glt->glt_grp_leader_addr == gh->gh_grp_leader_addr)
            {
                if (mt->mt_grp_leader_addr == index)
                {
                    if (mt->mt_grp_merge_timeout < CURRENT_TIME)
                    {
                        mt->mt_grp_merge_permission = index;
                        mt->mt_grp_merge_timeout = CURRENT_TIME + GROUP_HELLO_INTERVAL;
                        sendMRQ(mt, RREQ_JR);
                    }
                }
                else if (mt->mt_nexthops.upstream())
                {
                    sendMRQ(mt, RREQ_R);
                }
            }
            Packet::free(p);
        }
    }
    else
    { // tree member and mt->mt_grp_leader_addr == gh->gh_grp_leader_addr
        aodv_nh_entry *nh = mt->mt_nexthops.upstream();
        nsaddr_t up_node;
        if (gh->gh_flags == GRPH_NO_FLAG &&
            nh && nh->next_hop == ch->prev_hop_ &&
            mt->mt_seqno <= gh->gh_grp_seqno)
        {

            // only handle the first received msg
            if (id_lookup(gh->gh_multi_grp_addr + gh->gh_grp_leader_addr, gh->gh_grp_seqno))
            {
                Packet::free(p);
                return;
            }
            id_insert(gh->gh_multi_grp_addr + gh->gh_grp_leader_addr, gh->gh_grp_seqno);

            // update grp leader table entry
            aodv_glt_entry *glt = gltable.glt_lookup(gh->gh_multi_grp_addr);
            if (glt == NULL)
                glt = gltable.glt_add(gh->gh_multi_grp_addr);
            if ((glt->glt_expire <= CURRENT_TIME) || (glt->glt_grp_leader_addr < gh->gh_grp_leader_addr))
            {
                glt->glt_grp_leader_addr = gh->gh_grp_leader_addr;
                glt->glt_next_hop = ch->prev_hop_;
                glt->glt_expire = CURRENT_TIME + GROUP_HELLO_INTERVAL;
            }

            mt->mt_seqno = gh->gh_grp_seqno;
            mt->mt_hops_grp_leader = gh->gh_hop_count;
            mt_forward(p, DELAY);
        }
        else
            Packet::free(p);
    }
}

/******************************************************************/
// when there is a change about group leader info,
// the corresponding node must send out GRPH_U to its downstream nodes
/*****************************************************************/
void AODV::sendMGRPH_U(aodv_mt_entry *mt, nsaddr_t next_hop)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // before entering, make sure the node has valid upstream node or it is the grp leader

    Packet *p = Packet::alloc();
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_aodv_grph *gh = HDR_AODV_GRPH(p);

    gh->gh_type = AODVTYPE_GRPH;
    gh->gh_flags = GRPH_U;
    gh->gh_hop_count = mt->mt_hops_grp_leader;
    gh->gh_grp_leader_addr = mt->mt_grp_leader_addr;
    gh->gh_multi_grp_addr = mt->mt_dst;
    gh->gh_grp_seqno = mt->mt_seqno;

    ch->ptype() = PT_AODV;
    ch->size() = IP_HDR_LEN + gh->size();
    ch->iface() = -2;
    ch->error() = 0;
    ch->addr_type() = NS_AF_NONE;
    ch->prev_hop_ = index;
    ch->next_hop_ = MAC_BROADCAST;
    ch->direction() = hdr_cmn::DOWN;

    ih->saddr() = index;
    ih->daddr() = mt->mt_dst;
    ih->sport() = RT_PORT;
    ih->dport() = RT_PORT;
    ih->ttl_ = NETWORK_DIAMETER;

    if (next_hop == INFINITY8)
    {
        u_int8_t size = mt->mt_nexthops.size();
        if ((mt->mt_grp_leader_addr == index && size == 1) ||
            (mt->mt_grp_leader_addr != index && size == 2))
        {
            aodv_nh_entry *nh = mt->mt_nexthops.downstream();
            ch->next_hop_ = nh->next_hop;
            ch->addr_type() = NS_AF_INET;
        }
        else
            controlNextHello();
    }
    else
    {
        ch->next_hop_ = next_hop;
        ch->addr_type() = NS_AF_INET;
    }

    Scheduler::instance().schedule(target_, p, 0.01 * Random::uniform());
}

/******************************************************************/
// receive GRPH_U and rebroadcast
// only propagate from upstream to downstream
/*****************************************************************/
void AODV::recvMGRPH_U(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_aodv_grph *gh = HDR_AODV_GRPH(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);

    // hop count +1
    gh->gh_hop_count++;

    // must be on tree node and receive msg from its upstream
    aodv_mt_entry *mt = mtable.mt_lookup(gh->gh_multi_grp_addr);
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
    {
        Packet::free(p);
        return;
    }
    aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE || nh->link_direction != NH_UPSTREAM)
    {
        if (nh && nh->enabled_flag != NH_ENABLE)
            mt->mt_nexthops.remove(nh);
        Packet::free(p);
        return;
    }

    // if the recorded mt is good, no need to forward further
    if (mt->mt_grp_leader_addr == gh->gh_grp_leader_addr &&
        (mt->mt_seqno > gh->gh_grp_seqno ||
         (mt->mt_seqno == gh->gh_grp_seqno &&
          mt->mt_hops_grp_leader == gh->gh_hop_count)))
    {
        Packet::free(p);
        return;
    }

    // severe error
    if (index == gh->gh_grp_leader_addr)
    {
        printf("ERROR: %.9f in %s, grp leader %d receives msg from its own upstream %d\n",
               CURRENT_TIME, __FUNCTION__, index, ch->prev_hop_);
        Packet::free(p);
        exit(1);
    }

    // severe warning
    if (index == ih->saddr())
    {
        //printf("******WARNING: %.9f in %s, node %d receive GRPH_U with gh leader %d and ip src %d\n",
        //       CURRENT_TIME, __FUNCTION__, index, gh->gh_grp_leader_addr, ih->saddr());
        mt->mt_nexthops.remove(nh);
        sendMACT(mt->mt_dst, MACT_P, 0, ch->prev_hop_);
        sendMRQ(mt, RREQ_J);
        Packet::free(p);
        return;
    }

    mt->mt_grp_leader_addr = gh->gh_grp_leader_addr;
    mt->mt_seqno = gh->gh_grp_seqno;
    mt->mt_hops_grp_leader = gh->gh_hop_count;

    // forward the msg if applicable
    u_int8_t size = mt->mt_nexthops.size();
    if (size > 1)
    {
        if (size == 2)
        {
            aodv_nh_entry *nh = mt->mt_nexthops.downstream();
            ch->next_hop_ = nh->next_hop;
            ch->addr_type() = NS_AF_INET;
        }
        else
        {
            ch->next_hop_ = MAC_BROADCAST;
            ch->addr_type() = NS_AF_NONE;

            controlNextHello();
        }
        mt_forward(p, DELAY);
    }
    else
        Packet::free(p);
}
/**************************************************************/
//receive call back as the data cannot be transmitted
//from other node or from upper stack
/*************************************************************/
static void aodv_mt_failed_callback(Packet *p, void *arg)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    ((AODV *)arg)->mt_ll_failed(p);
}

void AODV::mt_ll_failed(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    if (DATA_PACKET(ch->ptype()) && ch->next_hop_ != MAC_BROADCAST)
    {
        aodv_mt_entry *mt = mtable.mt_lookup(ih->daddr());
        if (mt && mt->mt_node_status != NOT_ON_TREE)
        {
            aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->next_hop_);
            if (nh)
            {
                if (nh->enabled_flag != NH_ENABLE)
                    mt->mt_nexthops.remove(nh);
                else
                {
                    mt_repair(mt, nh);
                }
            }
        }
    }

    drop(p, DROP_RTR_MAC_CALLBACK);
}
/**************************************************************/
// on-tree node finding an active link is broke
// if the link is upstream, try to repair
// if the link is downstream, delete that link
//   and set prune timer if necessary
/**************************************************************/
void AODV::mt_repair(aodv_mt_entry *mt, aodv_nh_entry *nh)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    if (nh->link_direction == NH_DOWNSTREAM)
    {
        mt->mt_nexthops.remove(nh);
        setPruneTimer(mt);
    }
    else
    {
        // link direction is upstream
        mt->mt_nexthops.remove(nh);
#ifdef PREDICTION
        if (mt->mt_nexthops.upstream())
            return;
        else
        {
            mt->mt_flags = MTF_IN_REPAIR;
            sendMRQ(mt, RREQ_J);
        }
#else
        mt->mt_flags = MTF_IN_REPAIR;
        sendMRQ(mt, RREQ_J);
#endif
    }
}

void AODV::mt_nb_fail(nsaddr_t next_hop)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    aodv_mt_entry *mt;

    for (mt = mtable.head(); mt; mt = mt->mt_link.le_next)
    {
        if (mt->mt_node_status != NOT_ON_TREE)
        {
            aodv_nh_entry *nh = mt->mt_nexthops.lookup(next_hop);
            if (nh)
            {
                if (nh->enabled_flag != NH_ENABLE)
                    mt->mt_nexthops.remove(nh);
                else
                {
                    mt_repair(mt, nh);
                }
            }
        }
    }
}

void AODV::mt_link_purge()
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    aodv_mt_entry *mt;
    for (mt = mtable.head(); mt; mt = mt->mt_link.le_next)
    {
        if (mt->mt_node_status != NOT_ON_TREE)
        {
            aodv_nh_entry *nh = mt->mt_nexthops.first();
            while (nh != NULL)
            {
                aodv_nh_entry *nh_next = nh->next_;
                if (nh->enabled_flag != NH_ENABLE)
                    mt->mt_nexthops.remove(nh);
                else
                {
                    if (nh->link_expire > 0 && nh->link_expire < CURRENT_TIME)
                    {
                        if (nh->link_direction == NH_UPSTREAM)
                        {
                            mt->mt_nexthops.remove(nh);
                            if (mt->mt_nexthops.upstream() == NULL)
                            {
                                mt->mt_flags = MTF_IN_REPAIR;
                                sendMRQ(mt, RREQ_J);
                                break; //must have break
                            }
                        }
                        else
                        {
                            mt->mt_nexthops.remove(nh);
                            //must not be here: mt_prune(mt->mt_dst);
                        }
                    }
                }

                nh = nh_next;
            }
            if (mt->mt_nexthops.downstream() == NULL)
                mt_prune(mt->mt_dst);
        }
    }
}
/**************************************************************/
//receive multicast data packet
//from other node or from upper stack
/*************************************************************/
void AODV::mt_resolve(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    ch->xmit_failure_ = aodv_mt_failed_callback;
    ch->xmit_failure_data_ = (void *)this;

    aodv_mt_entry *mt = mtable.mt_lookup(ih->daddr());

    // not tree member
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
    {
        if ((ih->saddr() == index && ch->num_forwards() == 0) ||
            (ch->next_hop_ == index))
        {
            if (pid_lookup(index, ch->uid()))
                Packet::free(p);
            else
            {
                pid_insert(index, ch->uid());
                rt_resolve(p);
            }
        }
        else
            Packet::free(p);
        return;
    }

    // tree member
    // once initiated by tree member or is now propagated in tree, the msg is broadcast out
    // if it comes from out of tree, it must be unicast

#ifdef PREDICTION
    if (ch->num_forwards() != 0 && ch->next_hop_ == MAC_BROADCAST)
    {
        double breakTime = 2000.0;
        Node *currentNode = Node::get_node_by_address(index);
        breakTime = currentNode->getTime(ch->prev_hop_);
        aodv_nh_entry *prev_link = mt->mt_nexthops.lookup(ch->prev_hop_);

        if (prev_link && prev_link->enabled_flag == NH_ENABLE &&
            breakTime < 2000.0 && breakTime > CURRENT_TIME &&
            (breakTime - CURRENT_TIME) < PREDICTION_TIME_FOR_MULTICAST &&
            prev_link->link_expire == 0)
        {
            //printf("PREDICTION: %s at %.9f (%.9f) when receiving packet %d from node %d at node %d\n",
            //    __FUNCTION__, CURRENT_TIME, breakTime, ch->uid(), ch->prev_hop_, index);
            prev_link->link_expire = breakTime;
            if (prev_link->link_direction == NH_UPSTREAM)
            {
                sendMWARN(mt->mt_dst, WARN_U, prev_link->link_expire, ch->prev_hop_);
                sendMRQ(mt, RREQ_J);
            }
            else
            {
                sendMWARN(mt->mt_dst, WARN_D, prev_link->link_expire, ch->prev_hop_);
            }
        }
    }
#endif

    u_int8_t size = mt->mt_nexthops.size();
    if (size == 0)
    {
        if (pid_lookup(index, ch->uid()))
            Packet::free(p);
        else
        {
            pid_insert(index, ch->uid());

#ifdef UPPER_LEVEL_RECEIVE
            if (mt->mt_node_status == ON_GROUP)
            {
                ih->daddr() = index;
                ch->direction() = hdr_cmn::UP;
                ih->dport() = UPPER_LEVEL_PORT;
                Scheduler::instance().schedule(target_, p, 0);
            }
            else
                Packet::free(p);
#else
            Packet::free(p);
#endif
        }
        return;
    }

    if (ih->saddr() == index && ch->num_forwards() == 0)
    {
        if (pid_lookup(index, ch->uid()))
            Packet::free(p);
        else
        {
            pid_insert(index, ch->uid());
            ch->next_hop_ = MAC_BROADCAST;
            ch->addr_type() = NS_AF_ILINK;
            mt_forward(p, DELAY);
        }
    }
    else if (ch->next_hop_ != MAC_BROADCAST)
    {
        if (pid_lookup(index, ch->uid()))
            Packet::free(p);
        else
        {
            pid_insert(index, ch->uid());

#ifdef UPPER_LEVEL_RECEIVE
            if (mt->mt_node_status == ON_GROUP)
            {
                Packet *p_new = p->copy();
                struct hdr_cmn *ch_new = HDR_CMN(p_new);
                struct hdr_ip *ih_new = HDR_IP(p_new);

                ih_new->daddr() = index;
                ch_new->direction() = hdr_cmn::UP;
                ih_new->dport() = UPPER_LEVEL_PORT;
                Scheduler::instance().schedule(target_, p_new, 0);
            }
#endif
            ch->next_hop_ = MAC_BROADCAST;
            ch->addr_type() = NS_AF_ILINK;
            mt_forward(p, DELAY);
        }
    }

    else
    { //ch->next_hop_ == MAC_BROADCAST
        aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (nh == NULL || nh->enabled_flag != NH_ENABLE)
        {
            if (nh)
                mt->mt_nexthops.remove(nh);
            Packet::free(p);
        }
        else
        {
            if (pid_lookup(index, ch->uid()))
                Packet::free(p);
            else
            {
                pid_insert(index, ch->uid());

#ifdef UPPER_LEVEL_RECEIVE
                if (mt->mt_node_status == ON_GROUP)
                {
                    Packet *p_new = p->copy();
                    struct hdr_cmn *ch_new = HDR_CMN(p_new);
                    struct hdr_ip *ih_new = HDR_IP(p_new);

                    ih_new->daddr() = index;
                    ch_new->direction() = hdr_cmn::UP;
                    ih_new->dport() = UPPER_LEVEL_PORT;
                    Scheduler::instance().schedule(target_, p_new, 0);
                }
#endif

                if (size == 1 && mt->mt_nexthops.hop()->next_hop == ch->prev_hop_)
                    Packet::free(p);
                else
                {
                    mt_forward(p, DELAY);
                }
            }
        }
    }
}

/***********************************************************************/
// forward multicast data packets
// unciastly or multicastly
/***********************************************************************/
void AODV::mt_forward(Packet *p, double delay)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    // check TTL
    if (ih->ttl_ == 0)
    {
        drop(p, DROP_RTR_TTL);
        return;
    }

    ch->prev_hop_ = index;
    ch->direction() = hdr_cmn::DOWN;

    if (ch->next_hop_ == MAC_BROADCAST)
        controlNextHello();

    Scheduler::instance().schedule(target_, p, 0.01 * Random::uniform());
}

/**********************************************************/
//send request for RREQ_J, RREQ_JR, RREQ_R
//  Note: RREQ follows the unicast route discovery
//  RREQ_J: join group (unicastly or broadcastly)
//       or local repair (broadcastly)
//  RREQ_JR: tree merge (unicastly)
//  RREQ_R:  ask permission for tree merge (unicastly)
/*********************************************************/
void AODV::sendMRQ(aodv_mt_entry *mt, u_int8_t flags)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    nsaddr_t addr_type, next_hop, ipdst;
    u_int8_t ttl;

    if (flags != RREQ_R && flags != RREQ_JR && flags != RREQ_J)
        return;

    if (flags == RREQ_R)
    {
        // before entering this function,
        // make sure: the node is tree member except group leader
        //            and has valid upstream and valid grp leader
        // the msg is sent from downstream to upstream

        aodv_nh_entry *nh = mt->mt_nexthops.upstream();

        if (nh == NULL)
            return;

        addr_type = NS_AF_INET;
        next_hop = nh->next_hop;
        ttl = NETWORK_DIAMETER;
        ipdst = mt->mt_grp_leader_addr;
    }

    else if (flags == RREQ_JR)
    {
        // before entering this function,
        // make sure: 1. the node is tree member
        //            and valid grp leader
        //            and valid upstream if not group leader
        //            2. glt table has up-to-date info
        //            and the recorded group leader is greater than
        //            that recorded in node's mt table
        // the msg is sent along the info in glt table

        aodv_glt_entry *glt = gltable.glt_lookup(mt->mt_dst);

        if (glt == NULL || glt->glt_grp_leader_addr <= mt->mt_grp_leader_addr)
            return;

        addr_type = NS_AF_INET;
        next_hop = glt->glt_next_hop;
        ttl = NETWORK_DIAMETER;
        ipdst = glt->glt_grp_leader_addr;
    }

    else
    { // flags == RREQ_J, tree member except leader, has no upstream
        // double checking
        if (mt->mt_node_status == NOT_ON_TREE)
            return;
        if (mt->mt_grp_leader_addr == index)
            return;
        // #ifdef PREDICTION
        //         aodv_nh_entry *nh_up = mt->mt_nexthops.upstream();
        //         if (nh_up && nh_up->link_expire == 0)
        //             return;
        //         if (nh_up)
        //             mt->mt_flags = MTF_UP;
        //         else
        //             mt->mt_flags = MTF_IN_REPAIR;
        // #else
        if (mt->mt_nexthops.upstream())
            return;
        mt->mt_flags = MTF_IN_REPAIR;
        // #endif

        if (mt->mt_req_cnt > RREQ_RETRIES)
        {
            // #ifdef PREDICTION
            //             if (mt->mt_nexthops.upstream() != NULL)
            //                 return;
            // #endif
            selectLeader(mt, INFINITY8);
            return;
        }

        // finding ttl and recording corresponding info in mt
        if (mt->mt_hops_grp_leader != INFINITY2)
            mt->mt_req_last_ttl = max(mt->mt_req_last_ttl, mt->mt_hops_grp_leader);
        if (mt->mt_req_last_ttl == 0)
            ttl = TTL_START;
        else
        {
            if (mt->mt_req_last_ttl < TTL_THRESHOLD)
                ttl = mt->mt_req_last_ttl + TTL_INCREMENT;
            else
            {
                ttl = NETWORK_DIAMETER;
                mt->mt_req_cnt++;
            }
        }
        mt->mt_req_last_ttl = ttl;
        mt->mt_req_times++;

        // deciding if it can be unicastly sent to group leader
        aodv_glt_entry *glt = gltable.glt_lookup(mt->mt_dst);
        if ((mt->mt_grp_leader_addr == INFINITY8 && mt->mt_node_status == ON_GROUP) &&
            (mt->mt_req_times == 1) &&
            (glt && glt->glt_expire > CURRENT_TIME))
        {
            addr_type = NS_AF_INET;
            next_hop = glt->glt_next_hop;
            ipdst = glt->glt_grp_leader_addr;
        }
        else
        {
            addr_type = NS_AF_NONE;
            next_hop = MAC_BROADCAST;
            ipdst = IP_BROADCAST;
        }

        Packet *p_store = Packet::alloc();
        struct hdr_ip *ih = HDR_IP(p_store);
        ih->daddr() = mt->mt_dst;
        Scheduler::instance().schedule(&rtetimer, (Event *)p_store, RREP_WAIT_TIME);
    }

    Packet *p = Packet::alloc();
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);

    // fill up the request fields
    rq->rq_type = AODVTYPE_RREQ;
    rq->rq_flags = flags;
    rq->rq_hop_count = 0;
    bid++;
    rq->rq_bcast_id = bid;
    rq->rq_dst = mt->mt_dst;
    if (flags == RREQ_J && mt->mt_grp_leader_addr == INFINITY8 && mt->mt_node_status == ON_GROUP)
        rq->rq_dst_seqno = 0;
    else
        rq->rq_dst_seqno = mt->mt_seqno;
    rq->rq_src = index;
    seqno += 2;
    rq->rq_src_seqno = seqno;
    rq->rq_timestamp = CURRENT_TIME;

    // fill up the common header part
    ch->ptype() = PT_AODV;
    ch->size() = IP_HDR_LEN + rq->size();
    ch->iface() = -2;
    ch->error() = 0;
    ch->addr_type() = addr_type;
    ch->prev_hop_ = index;
    ch->next_hop_ = next_hop;
    ch->direction() = hdr_cmn::DOWN;

    // fill up the ip header part
    ih->saddr() = index;
    ih->daddr() = ipdst;
    ih->sport() = RT_PORT;
    ih->dport() = RT_PORT;
    ih->ttl_ = ttl;

    if (flags == RREQ_J && mt->mt_grp_leader_addr != INFINITY8)
    {
        struct hdr_aodv_request_ext *rqe =
            (struct hdr_aodv_request_ext *)(HDR_AODV_REQUEST(p) + rq->size());
        rqe->type = AODVTYPE_RREQ_EXT;
        rqe->length = AODVTYPE_RREQ_EXT_LEN;
        rqe->hop_count = mt->mt_hops_grp_leader;

        ch->size() += rqe->size();

        if (mt->mt_nexthops.downstream())
            sendMGRPH_U(mt, INFINITY8);
    }

    // insert bcast_id in cache
    id_insert(rq->rq_src, rq->rq_bcast_id);

    if (ch->next_hop_ == MAC_BROADCAST)
        controlNextHello();

    // send out the request immidiately
    Scheduler::instance().schedule(target_, p, 0.01 * Random::uniform());
}

/***********************************************************************/
// receive Request_with_no_flag for multicast tree
// the request source node must:
//      be not_on_tree and has no route to the tree
/***********************************************************************/
void AODV::recvMRQ_NOFLAG(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s at node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // the msg has no extension
    struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);
    struct hdr_ip *ih = HDR_IP(p);

    // update the msg
    ih->saddr() = index;
    rq->rq_hop_count++;

    aodv_mt_entry *mt = mtable.mt_lookup(rq->rq_dst);

    // a tree member is reached
    if (mt && mt->mt_node_status != NOT_ON_TREE)
    {
        if (mt->mt_grp_leader_addr == index ||
            (mt->mt_grp_leader_addr != INFINITY8 &&
             mt->mt_seqno >= rq->rq_dst_seqno))
        {
            sendReply(rq->rq_src,       // ipdst
                      RREP_NO_FLAG,     // flags
                      0,                // hop count to tree
                      mt->mt_dst,       // rpdst = multicast group addr
                      mt->mt_seqno,     // multicast group seqno
                      MY_ROUTE_TIMEOUT, // lifetime
                      rq->rq_timestamp, // timestamp = the timestamp of request
                      0,                // hop count to group leader ( no use for RREP_NO_FLAG)
                      0);               // grp leader addr (no use for RREP_NO_FLAG)
        }
        Packet::free(p);
        return;
    }

    // not tree member is reached
    aodv_rt_entry *rt = rtable.rt_lookup(rq->rq_dst);
    if (rt && rt->rt_flags == RTF_UP && rt->rt_seqno >= rq->rq_dst_seqno)
    {
        sendReply(rq->rq_src,
                  RREP_NO_FLAG,
                  rt->rt_hops,
                  rq->rq_dst,
                  rt->rt_seqno,
                  (u_int32_t)(rt->rt_expire - CURRENT_TIME),
                  rq->rq_timestamp,
                  0,
                  0);
        aodv_rt_entry *rt0 = rtable.rt_lookup(rq->rq_src);
        rt->pc_insert(rt0->rt_nexthop);
        rt0->pc_insert(rt->rt_nexthop);
        Packet::free(p);
        return;
    }

    if (ih->daddr() == index)
    {
        Packet::free(p);
        return;
    }

    if (rt)
        rq->rq_dst_seqno = max(rt->rt_seqno, rq->rq_dst_seqno);

    if (ih->daddr() == IP_BROADCAST)
        mt_forward(p, DELAY);
    else
    {
        // follow the glt table entry
        aodv_glt_entry *glt = gltable.glt_lookup(rq->rq_dst);
        if (glt && glt->glt_expire > CURRENT_TIME &&
            glt->glt_grp_leader_addr == ih->daddr())
        {
            hdr_cmn *ch = HDR_CMN(p);
            ch->next_hop_ = glt->glt_next_hop;
            mt_forward(p, NO_DELAY);
        }
        else
            drop(p, DROP_RTR_NO_ROUTE);
    }
}
/***********************************************************************/
// receive Request_with_J_flag for multicast tree
// the request source node must:
//      be at MTF_IN_REPAIR, having no upstream, not leader
//      wants to join the group or to reconnect to the tree
/***********************************************************************/
void AODV::recvMRQ_J(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s at node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);
    u_int16_t hop_count = INFINITY4;

    if (ch->size() > rq->size())
    {
        struct hdr_aodv_request_ext *rqe =
            (struct hdr_aodv_request_ext *)(HDR_AODV_REQUEST(p) + rq->size());
        hop_count = rqe->hop_count;
    }

    // update the msg
    ih->saddr() = index;
    rq->rq_hop_count++;

    aodv_mt_entry *mt = mtable.mt_lookup(rq->rq_dst);

    // a tree member is reached
    if (mt && mt->mt_node_status != NOT_ON_TREE)
    {

#ifdef PREDICTION
        aodv_nh_entry *link = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (link && link->link_expire != 0)
        {
            Packet::free(p);
            return;
        }

        link = mt->mt_nexthops.upstream();
        if (mt->mt_grp_leader_addr != index &&
            (link == NULL || link->link_expire != 0))
        {
            Packet::free(p);
            return;
        }
#endif
        aodv_nh_entry *nh = mt->mt_nexthops.upstream();
        if ((mt->mt_grp_leader_addr == index) ||
            (mt->mt_grp_leader_addr != INFINITY8 && nh != NULL &&
             ((mt->mt_seqno > rq->rq_dst_seqno) ||
              (mt->mt_seqno == rq->rq_dst_seqno && mt->mt_hops_grp_leader <= hop_count) ||
              (mt->mt_seqno == rq->rq_dst_seqno && hop_count == 1 && mt->mt_hops_grp_leader == 2 &&
               nh->next_hop != rq->rq_src))))
        {
            aodv_rt_entry *rt = rtable.rt_lookup(rq->rq_src);
            if (mt->mt_grp_leader_addr == index ||
                (mt->mt_grp_leader_addr != rq->rq_src && mt->mt_grp_leader_addr != rt->rt_nexthop &&
                 nh->next_hop != rq->rq_src && nh->next_hop != rt->rt_nexthop))
            {
                sendReply(rq->rq_src,              //ipdst
                          RREP_J,                  //flags
                          0,                       //hop_count to tree
                          mt->mt_dst,              //rpdst = multicast group addr
                          mt->mt_seqno,            //multicast grp seqno
                          MY_ROUTE_TIMEOUT,        //lifetime
                          rq->rq_timestamp,        //timestamp = the timestamp in reqeust
                          mt->mt_hops_grp_leader,  //hop_count to grp leader
                          mt->mt_grp_leader_addr); //grp leader addr
                mt->mt_keep_on_tree_timeout = CURRENT_TIME + 1.5 * RREP_WAIT_TIME;
            }
        }
        Packet::free(p);
        return;
    }

    // destination but not tree member is reached
    if (ih->daddr() == index)
    {
        Packet::free(p);
        return;
    }

    // intermediate node but not tree member is reached
    if (ih->daddr() == IP_BROADCAST)
        mt_forward(p, DELAY);
    else
    {
        // follow the glt table entry
        aodv_glt_entry *glt = gltable.glt_lookup(rq->rq_dst);
        if (glt && glt->glt_expire > CURRENT_TIME &&
            ih->daddr() == glt->glt_grp_leader_addr)
        {
            ch->next_hop_ = glt->glt_next_hop;
            mt_forward(p, NO_DELAY);
        }
        else
            drop(p, DROP_RTR_NO_ROUTE);
    }
}

/***********************************************************************/
// receive Request_with_R_flag for multicast tree
// the request source node must:
//      be on_tree and
//      wants to get permission from its group leader for requesting tree merge
//      along upstream towards its own leader
// NOT ADD ANY NEXT HOP in MT TABLE, NOT UPDATE GRP INFO in MT TABLE
/***********************************************************************/
void AODV::recvMRQ_R(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s at node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // the msg has no extension
    struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);

    // update the msg
    ih->saddr() = index;
    rq->rq_hop_count++;

    // check the node
    // must be tree member, should has valid upstream if not grp leader
    // the msg must be came from its downstream
    aodv_mt_entry *mt = mtable.mt_lookup(rq->rq_dst);
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
    {
        Packet::free(p);
        return;
    }
    if (mt->mt_grp_leader_addr != index && mt->mt_nexthops.upstream() == NULL)
    {
        Packet::free(p);
        return;
    }
    aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE || nh->link_direction != NH_DOWNSTREAM)
    {
        if (nh && nh->enabled_flag != NH_ENABLE)
            mt->mt_nexthops.remove(nh);
        Packet::free(p);
        return;
    }

    // destination and also group leader is reached
    // send reply, if it has not permitted other node to request tree merge
    if (mt->mt_grp_leader_addr == index && ih->daddr() == index)
    {
        if (mt->mt_grp_merge_timeout < CURRENT_TIME)
        {
            mt->mt_grp_merge_permission = rq->rq_src;
            mt->mt_grp_merge_timeout = CURRENT_TIME + GROUP_HELLO_INTERVAL;
            sendReply(rq->rq_src,       // ipdst
                      RREP_R,           // flags
                      0,                // for RREP_R, used as hop count to grp leader
                      mt->mt_dst,       //rpdst = multicast group addr
                      mt->mt_seqno,     //multicast grp seqno
                      MY_ROUTE_TIMEOUT, //lifetime
                      rq->rq_timestamp, //timestamp = the timestamp in request
                      0,                // hop count to grp leader (no use for RREP_R)
                      0);               // grp leader addr (no use for RREP_R)
        }
        Packet::free(p);
        return;
    }

    // destination is reached but not group leader
    if (ih->daddr() == index)
    {
        sendMGRPH_U(mt, ch->prev_hop_);
        Packet::free(p);
        return;
    }

    // group leader but not its destination
    if (mt->mt_grp_leader_addr == index)
    {
        sendMGRPH_U(mt, ch->prev_hop_);
        Packet::free(p);
        return;
    }

    // intermediate node is reached and not group leader
    // along upstream to forward the msg
    if (mt->mt_grp_leader_addr == ih->daddr())
    {
        nh = mt->mt_nexthops.upstream();
        ch->next_hop_ = nh->next_hop;
        mt_forward(p, NO_DELAY);
    }
    else
    {
        sendMGRPH_U(mt, ch->prev_hop_);
        Packet::free(p);
    }
}

// added by Indresh for energy for geomentric routing
double calculateEnergy(double t_energy, int hop_in_between ){
    return pow(t_energy, 1/hop_in_between)/(INITIAL_ENERGY);
}

/***********************************************************************/
// receive Reply_with_R_flag for multicast tree
// the msg must be from leader to downstream on tree
// NOT ADD ANY NEXT HOP in MT TABLE, BUT UPDATE GRP INFO in MT TABLE
/***********************************************************************/
void AODV::recvMRP_R(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s at node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // the msg has no extension
    struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);
    aodv_mt_entry *mt;
    aodv_nh_entry *nh;

    // for RREP_R, rp_hop_count is used as hop count to grp leader
    rp->rp_hop_count++;

    // must be on tree node and receive msg from its upstream
    mt = mtable.mt_lookup(rp->rp_dst);
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
    {
        Packet::free(p);
        return;
    }
    nh = mt->mt_nexthops.lookup(ch->prev_hop_);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE || nh->link_direction != NH_UPSTREAM)
    {
        if (nh && nh->enabled_flag != NH_ENABLE)
            mt->mt_nexthops.remove(nh);
        Packet::free(p);
        return;
    }

    if (index == mt->mt_grp_leader_addr)
    {
        printf("******ERROR: at %.9f in %s, node %i is group leader for group %d, BUT IT HAS UPSTREAM!\n",
               CURRENT_TIME, __FUNCTION__, index, mt->mt_dst);
        Packet::free(p);
        exit(1);
    }

    if (index == rp->rp_src)
    {
        printf("******ERROR: at %.9f in %s, node %d is group leader for group %d, BUT IT RECEIVE RREP_R FROM ITSELF!\n",
               CURRENT_TIME, __FUNCTION__, index, mt->mt_dst);
        Packet::free(p);
        exit(1);
    }

    // update the grp info
    mt->mt_grp_leader_addr = rp->rp_src;
    mt->mt_seqno = rp->rp_dst_seqno;
    mt->mt_hops_grp_leader = rp->rp_hop_count;

    // destination node is reached
    if (index == ih->daddr())
    {
        iEnergy = iNode->energy_model()->energy();
        p->t_energy *= iEnergy;
        p->min_energy_ = min(p->min_energy_, iEnergy);
        double energy_ = calculateEnergy(p->t_energy, p->rp_hop_count);

        

        if(compareEnergy(energy_, iEnergy) == 1){
            sendMRQ(mt, RREQ_JR);
            Packet::free(p);
        }

        return;
    }

    // intermediate node is reached
    // must forward to its downstream
    aodv_rt_entry *rt = rtable.rt_lookup(ih->daddr());
    if (rt == NULL || rt->rt_flags != RTF_UP)
    {
        //printf("******WARNING: at %.9f in %s, node %d cannot find route to node %d\n",
        //   CURRENT_TIME, __FUNCTION__, index, ih->daddr());
        Packet::free(p);
        return;
    }
    nh = mt->mt_nexthops.lookup(rt->rt_nexthop);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE || nh->link_direction != NH_DOWNSTREAM)
    {
        //printf("******WARNING: at %.9f in %s, node %d cannot follow downstream to node %d, because node %d is not its downstream\n",
        //    CURRENT_TIME, __FUNCTION__, index, ih->daddr(), rt->rt_nexthop);
        if (nh && nh->enabled_flag != NH_ENABLE)
            mt->mt_nexthops.remove(nh);
        Packet::free(p);
        return;
    }

    ch->next_hop_ = nh->next_hop;
    mt_forward(p, NO_DELAY);
}

bool compareEnergy(double energy_, double iEnergy){
    if(energy_ > iEnergy){
        return true;
    } else if(energy_ == iEnergy){
        if(min_energy_ > THRESHOLD){
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

/***********************************************************************/
// receive Request_with_JR_flag for multicast tree
// the request source node must be on tree and requests tree merge
// if the node receiving this msg is not-on-tree node, follow the glt entry
// if the node receiving this msg is a tree member, follow to upstream till group leader
// ONLY ADD NEXT HOP and UPDATE GRP Seqno in MT TABLE when GRP LEADER reached
/***********************************************************************/
void AODV::recvMRQ_JR(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // the msg has no extension
    struct hdr_aodv_request *rq = HDR_AODV_REQUEST(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);
    aodv_mt_entry *mt = mtable.mt_lookup(rq->rq_dst);

    // update request msg
    ih->saddr() = index;
    rq->rq_hop_count++;

    // group leader is reached
    if (mt && mt->mt_grp_leader_addr == index)
    {
        aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (nh && nh->enabled_flag == NH_ENABLE && nh->link_direction == NH_UPSTREAM)
        {
            printf("******ERROR: %.9f in %s,  node %i is group leader for group %d, BUT IT HAS UPSTEAM!\n",
                   CURRENT_TIME, __FUNCTION__, index, mt->mt_dst);
            exit(1);
        }

        // add the new downstream link
        if (nh)
            mt->mt_nexthops.remove(nh);
        nh = new aodv_nh_entry(ch->prev_hop_);
        mt->mt_nexthops.add(nh);
        nh->enabled_flag = NH_ENABLE;
        nh->link_direction = NH_DOWNSTREAM;

        // get new seqno
        mt->mt_seqno = max(mt->mt_seqno, rq->rq_dst_seqno) + 1;

        sendReply(rq->rq_src,       //ipdst: should be another group leader
                  RREP_JR,          //flags
                  0,                // used as hop count to the new group leader
                  mt->mt_dst,       // rpdst = multicast group addr
                  mt->mt_seqno,     // multicast grp seqno
                  MY_ROUTE_TIMEOUT, // lifetime
                  rq->rq_timestamp, // timestamp = the timestamp in request
                  0,                // hop count to grp leader (no use for RREP_JR)
                  0);               // grp leader addr (no use for RREP_JR)

        Packet::free(p);
        return;
    }

    // destination is reached but not group leader
    if (ih->daddr() == index)
    {
        Packet::free(p);
        return;
    }

    // intermediate node is reached but not group leader
    aodv_glt_entry *glt = gltable.glt_lookup(rq->rq_dst);

    // 1. not on tree node, forward msg along glt info
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
    {
        if (glt && glt->glt_grp_leader_addr == ih->daddr() &&
            glt->glt_expire > CURRENT_TIME)
        {
            ch->next_hop_ = glt->glt_next_hop;
            mt_forward(p, NO_DELAY);
        }
        else
            Packet::free(p);
        return;
    }

    // 2. on tree node but not group leader
    // it must has upstream, forward msg to upstream
    // also, if it has any downstream, this msg should be received from its downstream
    aodv_nh_entry *nh = mt->mt_nexthops.upstream();
    if (nh == NULL)
    {
        Packet::free(p);
        return;
    }

    ch->next_hop_ = nh->next_hop;
    mt_forward(p, NO_DELAY);
}

/***********************************************************************/
// receive Reply_with_JR_flag for multicast tree
// ADD NEXT HOP and UPDATE GRP INFO in MT TABLE
/***********************************************************************/
void AODV::recvMRP_JR(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // the msg has no extension
    struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);
    aodv_mt_entry *mt = mtable.mt_lookup(rp->rp_dst);
    if (mt == NULL)
        mt = mtable.mt_add(rp->rp_dst);

    // hop count to group leader +1
    rp->rp_hop_count++;

    // grp leader is reached
    if (mt->mt_grp_leader_addr == index)
    {
        // check the new grp leader is not itself
        if (rp->rp_src == index)
        {
            printf("******ERROR: %.9f in %s,  node %i is group leader for group %d, BUT IT RECEIVE RREP_JR FROM ITS SELF!\n",
                   CURRENT_TIME, __FUNCTION__, index, mt->mt_dst);
            Packet::free(p);
            exit(1);
        }

        // before entering, make sure that grp leader must not have any upstream
        aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (nh)
            mt->mt_nexthops.remove(nh);
        nh = new aodv_nh_entry(ch->prev_hop_);
        mt->mt_nexthops.add(nh);
        nh->enabled_flag = NH_ENABLE;
        nh->link_direction = NH_UPSTREAM;

        // update the grp info
        mt->mt_flags = MTF_UP;
        mt->mt_grp_leader_addr = rp->rp_src;
        mt->mt_seqno = rp->rp_dst_seqno;
        mt->mt_hops_grp_leader = rp->rp_hop_count;
        if (mt->mt_nexthops.size() > 1)
            sendMGRPH_U(mt, INFINITY8);

        Packet::free(p);
        return;
    }

    // destination or intermediate node is reached,  but not group leader
    // not tree member
    if (mt->mt_node_status == NOT_ON_TREE)
    {
        if (ih->daddr() == index)
        {
            sendMACT(mt->mt_dst, MACT_P, 0, ch->prev_hop_);
            Packet::free(p);
            return;
        }

        aodv_rt_entry *rt = rtable.rt_lookup(ih->daddr());
        if (rt == NULL || rt->rt_flags != RTF_UP || rt->rt_nexthop == ch->prev_hop_)
        {
            sendMACT(mt->mt_dst, MACT_P, 0, ch->prev_hop_);
            Packet::free(p);
            return;
        }

        aodv_nh_entry *nh = new aodv_nh_entry(ch->prev_hop_);
        mt->mt_nexthops.add(nh);
        nh->link_direction = NH_UPSTREAM;
        nh->enabled_flag = NH_ENABLE;

        nh = new aodv_nh_entry(rt->rt_nexthop);
        mt->mt_nexthops.add(nh);
        nh->link_direction = NH_DOWNSTREAM;
        nh->enabled_flag = NH_ENABLE;

        mt->mt_flags = MTF_UP;
        mt->mt_node_status = ON_TREE;
        mt->mt_grp_leader_addr = rp->rp_src;
        mt->mt_seqno = rp->rp_dst_seqno;
        mt->mt_hops_grp_leader = rp->rp_hop_count;

        clearMReqState(mt);
        clearMRpyState(mt);

        ch->next_hop_ = rt->rt_nexthop;

        mt_forward(p, NO_DELAY);

        return;
    }

    // tree member with the same group leader, but not grp leader
    if (mt->mt_grp_leader_addr == rp->rp_src)
    {
        aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (nh == NULL || nh->enabled_flag != NH_ENABLE || nh->link_direction == NH_DOWNSTREAM)
        {
            if (nh)
            {
                if (nh->enabled_flag != NH_ENABLE)
                    mt->mt_nexthops.remove(nh);
                else
                {
                    mt->mt_nexthops.remove(nh);
                    mt_prune(mt->mt_dst);
                }
            }
            sendMACT(mt->mt_dst, MACT_P, 0, ch->prev_hop_);
            Packet::free(p);
            return;
        }

        //from its own upstream, update grp info
        u_int16_t old_hop_count = mt->mt_hops_grp_leader;
        mt->mt_seqno = rp->rp_dst_seqno;
        mt->mt_hops_grp_leader = rp->rp_hop_count;

        // check its next hop
        aodv_rt_entry *rt = rtable.rt_lookup(ih->daddr());
        if (rt == NULL || rt->rt_flags != RTF_UP || rt->rt_nexthop == nh->next_hop)
        {
            if (old_hop_count != mt->mt_hops_grp_leader &&
                mt->mt_nexthops.size() > 1)
                sendMGRPH_U(mt, INFINITY8);
            Packet::free(p);
            return;
        }

        aodv_nh_entry *nh_d = mt->mt_nexthops.lookup(rt->rt_nexthop);
        //if already enabled, must be downstream
        if (nh_d)
            mt->mt_nexthops.remove(nh_d);
        nh_d = new aodv_nh_entry(rt->rt_nexthop);
        mt->mt_nexthops.add(nh_d);
        nh_d->link_direction = NH_DOWNSTREAM;
        nh_d->enabled_flag = NH_ENABLE;

        ch->next_hop_ = rt->rt_nexthop;

        if (old_hop_count != mt->mt_hops_grp_leader &&
            mt->mt_nexthops.size() > 2)
            sendMGRPH_U(mt, INFINITY8);

        mt_forward(p, NO_DELAY);
        return;
    }

    // tree member with different group leader, but this node is not group leader
    if (index == rp->rp_src)
    {
        printf("******ERROR: at %.9f in %s,  node %i is not group leader for group %d, BUT IT RECEIVE RREP_JR FROM ITS SELF!\n",
               CURRENT_TIME, __FUNCTION__, index, mt->mt_dst);
        Packet::free(p);
        exit(1);
    }

    if (mt->mt_grp_leader_addr > rp->rp_src)
    {
        aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (nh == NULL || nh->enabled_flag != NH_ENABLE || nh->link_direction == NH_DOWNSTREAM)
        {
            if (nh)
                mt->mt_nexthops.remove(nh);
            sendMACT(mt->mt_dst, MACT_P, 0, ch->prev_hop_);
            Packet::free(p);
            return;
        }
        else
        { // from its own upstream, update the grp info
            mt->mt_grp_leader_addr = rp->rp_src;
            mt->mt_seqno = rp->rp_dst_seqno;
            mt->mt_hops_grp_leader = rp->rp_hop_count;
            if (mt->mt_nexthops.size() > 1)
                sendMGRPH_U(mt, INFINITY8);
            Packet::free(p);
            return;
        }
    }

    // mt->mt_grp_leader < rp->rp_src
    aodv_nh_entry *nh = mt->mt_nexthops.lookup(ch->prev_hop_);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE || nh->link_direction == NH_DOWNSTREAM)
    {
        nsaddr_t old_grp_leader = mt->mt_grp_leader_addr;

        // update the grp info
        mt->mt_flags = MTF_UP;
        mt->mt_grp_leader_addr = rp->rp_src;
        mt->mt_seqno = rp->rp_dst_seqno;
        mt->mt_hops_grp_leader = rp->rp_hop_count;

        // upstream node if it has, must not be nh link
        aodv_nh_entry *nh_u = mt->mt_nexthops.upstream();
        if (nh_u)
            nh_u->link_direction = NH_DOWNSTREAM;

        if (nh)
            mt->mt_nexthops.remove(nh);
        nh = new aodv_nh_entry(ch->prev_hop_);
        mt->mt_nexthops.add(nh);
        nh->enabled_flag = NH_ENABLE;
        nh->link_direction = NH_UPSTREAM;

        if ((nh_u && mt->mt_nexthops.size() > 2) ||
            (nh_u == NULL && mt->mt_nexthops.size() > 1))
            sendMGRPH_U(mt, INFINITY8);

        if (nh_u)
        {
            if (ih->daddr() == index)
                ih->daddr() = old_grp_leader;
            ch->next_hop_ = nh_u->next_hop;
            mt_forward(p, NO_DELAY);
        }
        else
            Packet::free(p);

        return;
    }

    // from its own upstream, update the grp info
    mt->mt_grp_leader_addr = rp->rp_src;
    mt->mt_seqno = rp->rp_dst_seqno;
    mt->mt_hops_grp_leader = rp->rp_hop_count;
    if (mt->mt_nexthops.size() > 1)
        sendMGRPH_U(mt, INFINITY8);
    Packet::free(p);
    return;
}

/***********************************************************************/
// receive Reply_with_J for multicast tree
// the request source node must:
//      be at MTF_IN_REPAIR, having no upstream, not leader
//      wants to join the group or to reconnect to the tree
/***********************************************************************/
void AODV::recvMRP_J(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s\n", __FUNCTION__);
#endif

    struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_aodv_reply_ext *rpe = (struct hdr_aodv_reply_ext *)(rp + rp->size());

    // update the msg
    rp->rp_hop_count++;
    rpe->hops_grp_leader++;

    aodv_mt_entry *mt = mtable.mt_lookup(rp->rp_dst);
    if (mt == NULL)
        mt = mtable.mt_add(rp->rp_dst);

    // double check
    if (mt->mt_node_status != NOT_ON_TREE)
    {
        if (rpe->grp_leader_addr == index)
        {
            Packet::free(p);
            return;
        }

        aodv_nh_entry *nh = mt->mt_nexthops.lookup(rp->rp_src);
        if (nh && nh->enabled_flag == NH_ENABLE && nh->link_direction == NH_DOWNSTREAM)
        {
            Packet::free(p);
            return;
        }

        nh = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (nh && nh->enabled_flag == NH_ENABLE && nh->link_direction == NH_DOWNSTREAM)
        {
            Packet::free(p);
            return;
        }

        nh = mt->mt_nexthops.lookup(rpe->grp_leader_addr);
        if (nh && nh->enabled_flag == NH_ENABLE && nh->link_direction == NH_DOWNSTREAM)
        {
            Packet::free(p);
            return;
        }
    }

    // the destination is reached
    if (ih->daddr() == index)
    {

#ifdef PREDICTION
        aodv_nh_entry *link = mt->mt_nexthops.lookup(ch->prev_hop_);
        if (link && link->link_expire != 0)
        {
            Packet::free(p);
            return;
        }

        if (mt && mt->mt_node_status != NOT_ON_TREE &&
            mt->mt_grp_leader_addr != index)
        {
            aodv_nh_entry *nh_up = mt->mt_nexthops.upstream();
            if (nh_up == NULL || nh_up->link_expire != 0)
            {
                if ((mt->mt_rep_timeout <= CURRENT_TIME) ||
                    (mt->mt_rep_grp_leader_addr < rpe->grp_leader_addr) ||
                    (mt->mt_rep_grp_leader_addr == rpe->grp_leader_addr &&
                     ((mt->mt_rep_seqno < rp->rp_dst_seqno) ||
                      (mt->mt_rep_seqno == rp->rp_dst_seqno &&
                       ((mt->mt_rep_hops_tree > rp->rp_hop_count) ||
                        (mt->mt_rep_hops_tree == rp->rp_hop_count && mt->mt_rep_hops_grp_leader > rpe->hops_grp_leader))))))
                {
                    recordMRpy(mt, p);
                }
            }
            Packet::free(p);
            return;
        }

#else

        if (mt && mt->mt_node_status != NOT_ON_TREE &&
            mt->mt_grp_leader_addr != index &&
            mt->mt_nexthops.upstream() == NULL)
        {
            if ((mt->mt_rep_timeout <= CURRENT_TIME) ||
                (mt->mt_rep_grp_leader_addr < rpe->grp_leader_addr) ||
                (mt->mt_rep_grp_leader_addr == rpe->grp_leader_addr &&
                 ((mt->mt_rep_seqno < rp->rp_dst_seqno) ||
                  (mt->mt_rep_seqno == rp->rp_dst_seqno &&
                   ((mt->mt_rep_hops_tree > rp->rp_hop_count) ||
                    (mt->mt_rep_hops_tree == rp->rp_hop_count && mt->mt_rep_hops_grp_leader > rpe->hops_grp_leader))))))
            {
                recordMRpy(mt, p);
            }
        }
        Packet::free(p);
        return;
#endif
    }

    // an intermediate node is reached
    // should be not-on-tree
    // if reach on a tree node, discard it
    if (mt->mt_node_status != NOT_ON_TREE)
    {
        Packet::free(p);
        return;
    }

    // not tree member is reached
    if ((mt->mt_rep_timeout <= CURRENT_TIME) ||
        (mt->mt_rep_ipdst == ih->daddr() &&
         ((mt->mt_rep_grp_leader_addr < rpe->grp_leader_addr) ||
          (mt->mt_rep_grp_leader_addr == rpe->grp_leader_addr &&
           ((mt->mt_rep_seqno < rp->rp_dst_seqno) ||
            (mt->mt_rep_seqno == rp->rp_dst_seqno &&
             ((mt->mt_rep_hops_tree > rp->rp_hop_count) ||
              (mt->mt_rep_hops_tree == rp->rp_hop_count && mt->mt_rep_hops_grp_leader > rpe->hops_grp_leader))))))))
    {
        recordMRpy(mt, p);

        aodv_rt_entry *rt = rtable.rt_lookup(ih->daddr());
        if (rt && rt->rt_flags == RTF_UP)
            forward(rt, p, NO_DELAY);
        else
            Packet::free(p);
    }
    else
        Packet::free(p);
}

/**************************************************************/
// the time to check after waiting for RREP
// only set the timer when sending out RREQ_J
/*************************************************************/
void RREPWaitTimer::handle(Event *e)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    Packet *p = (Packet *)e;
    struct hdr_ip *ih = HDR_IP(p);
    agent->afterWaitRREP(ih->daddr());
    Packet::free(p);
}

void AODV::afterWaitRREP(nsaddr_t dst)
{
#ifdef DEBUG
    fprintf(stdout, "%s at node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    aodv_mt_entry *mt = mtable.mt_lookup(dst);

    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
        return;
    if (mt->mt_grp_leader_addr == index)
        return;

#ifdef PREDICTION
    aodv_nh_entry *link = mt->mt_nexthops.upstream();
    if (link != NULL && link->link_expire == 0)
        return;
#else
    if (mt->mt_nexthops.upstream() != NULL)
        return;
#endif

    // must be tree member, not group leader, has no upstream

    // if not receiving reply, send request again
    if (mt->mt_rep_timeout <= CURRENT_TIME)
    {
        sendMRQ(mt, RREQ_J);
        return;
    }

    // check the reply
    // recorded grp leader must not be the node itself,
    // because we do the check when recording the reply
    aodv_nh_entry *nh = mt->mt_nexthops.lookup(mt->mt_rep_selected_upstream);

#ifdef PREDICTION
    if (nh && nh->enabled_flag == NH_ENABLE && nh->link_direction == NH_UPSTREAM)
    {
        clearMReqState(mt);
        clearMRpyState(mt);
        return;
    }
#endif

    if (nh && nh->enabled_flag == NH_ENABLE)
    {
        // as node has no upstream, this link must be downstream
        //printf("******WARNING: %.9f in %s, node %d cannot activate its existing downstream %d to upstream, need send RREQ_J again\n",
        //    CURRENT_TIME, __FUNCTION__, index, nh->next_hop);

        clearMReqState(mt);
        clearMRpyState(mt);
        sendMRQ(mt, RREQ_J);
        return;
    }

    // now, has valid reply
#ifdef PREDICTION
    if (link)
        mt->mt_nexthops.remove(link);
#endif

    mt->mt_flags = MTF_UP;
    nsaddr_t old_leader = mt->mt_grp_leader_addr;
    mt->mt_grp_leader_addr = mt->mt_rep_grp_leader_addr;
    mt->mt_seqno = mt->mt_rep_seqno;
    nsaddr_t old_hops_grp_leader = mt->mt_hops_grp_leader;
    mt->mt_hops_grp_leader = mt->mt_rep_hops_grp_leader;

    if (nh)
        mt->mt_nexthops.remove(nh);
    nh = new aodv_nh_entry(mt->mt_rep_selected_upstream);
    mt->mt_nexthops.add(nh);
    nh->link_direction = NH_UPSTREAM;
    nh->enabled_flag = NH_ENABLE;

    if (mt->mt_nexthops.downstream() != NULL &&
        (old_leader != mt->mt_grp_leader_addr || old_hops_grp_leader != mt->mt_hops_grp_leader))
        sendMGRPH_U(mt, INFINITY8);

    clearMReqState(mt);
    clearMRpyState(mt);

    sendMACT(mt->mt_dst, MACT_J, mt->mt_hops_grp_leader, nh->next_hop);
}

void AODV::sendMACT(nsaddr_t dst, u_int8_t flags, u_int8_t hop_count, nsaddr_t next_hop)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // as there is no MACT_U, next_hop must be a node address
    // and MACT must be sent out unicastly

    Packet *p = Packet::alloc();
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_aodv_mact *mact = HDR_AODV_MACT(p);

    mact->mact_type = AODVTYPE_MACT;
    mact->mact_flags = flags;
    mact->mact_hop_count = hop_count;
    mact->mact_grp_dst = dst;
    mact->mact_src = index;
    mact->mact_src_seqno = seqno;

    ch->ptype() = PT_AODV;
    ch->size() = IP_HDR_LEN + mact->size();
    ch->iface() = -2;
    ch->error() = 0;
    ch->prev_hop_ = index;
    ch->direction() = hdr_cmn::DOWN;
    ch->next_hop_ = next_hop;
    ch->addr_type() = NS_AF_INET;

    ih->saddr() = index;
    ih->daddr() = next_hop;
    ih->sport() = RT_PORT;
    ih->dport() = RT_PORT;
    ih->ttl_ = 1;

    Scheduler::instance().schedule(target_, p, 0.01 * Random::uniform());
}

void AODV::recvMACT(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_aodv_mact *mact = HDR_AODV_MACT(p);

    switch (mact->mact_flags)
    {
    case MACT_J:
        recvMACT_J(p);
        return;
    case MACT_P:
        recvMACT_P(p);
        return;
    case MACT_GL:
        recvMACT_GL(p);
        return;
    default:
        Packet::free(p);
        return;
    }
}

/******************************************************************/
// receive MACT_P
// from upstream: remove that link and select leader
// from downstream: remove that link and prune its self if necessary
/******************************************************************/
void AODV::recvMACT_P(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_aodv_mact *mact = HDR_AODV_MACT(p);
    nsaddr_t grp_dst = mact->mact_grp_dst;
    nsaddr_t prev_hop = mact->mact_src;
    Packet::free(p);

    // must be on tree and has valid link
    aodv_mt_entry *mt = mtable.mt_lookup(grp_dst);
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
        return;
    aodv_nh_entry *nh = mt->mt_nexthops.lookup(prev_hop);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE)
    {
        if (nh)
            mt->mt_nexthops.remove(nh);
        return;
    }

    u_int8_t direction = nh->link_direction;
    mt->mt_nexthops.remove(nh);

    if (direction == NH_UPSTREAM)
        selectLeader(mt, INFINITY8);
    else
        mt_prune(mt->mt_dst);
}

/******************************************************************/
// receive MACT_GL
// should be only from upstream
/******************************************************************/
void AODV::recvMACT_GL(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_aodv_mact *mact = HDR_AODV_MACT(p);
    nsaddr_t grp_dst = mact->mact_grp_dst;
    nsaddr_t prev_hop = mact->mact_src;
    Packet::free(p);

    // must be on tree and has valid link
    aodv_mt_entry *mt = mtable.mt_lookup(grp_dst);
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
    {
        sendMACT(grp_dst, MACT_P, 0, prev_hop);
        return;
    }
    aodv_nh_entry *nh = mt->mt_nexthops.lookup(prev_hop);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE)
    {
        sendMACT(grp_dst, MACT_P, 0, prev_hop);
        if (nh)
            mt->mt_nexthops.remove(nh);
        return;
    }

    if (nh->link_direction == NH_DOWNSTREAM)
    {
        //printf("WARNING: at %.9f in %s, node %d receives MACT_GL from donwstream %d\n",
        //            CURRENT_TIME, __FUNCTION__, index, nh->next_hop);
        sendMGRPH_U(mt, INFINITY8);
    }
    else
    {
        // change the upstream direction to downstream and
        // select leader
        nh->link_direction = NH_DOWNSTREAM;
        selectLeader(mt, nh->next_hop);
    }
}

/******************************************************************/
// receive MACT_J
// activate a branch to group tree
/******************************************************************/
void AODV::recvMACT_J(Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s at node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    struct hdr_aodv_mact *mact = HDR_AODV_MACT(p);
    aodv_mt_entry *mt = mtable.mt_lookup(mact->mact_grp_dst);
    if (mt == NULL)
        mt = mtable.mt_add(mact->mact_grp_dst);
    nsaddr_t prev_hop = mact->mact_src;
    u_int8_t hop_count = mact->mact_hop_count - 1;
    Packet::free(p);

    // a tree member is reached
    if (mt->mt_node_status != NOT_ON_TREE)
    {
        if (mt->mt_grp_leader_addr == INFINITY8)
        {
            //printf("******WARNING: %.9f in %s, node %d send MACT_P to previous hop %d because its leader info is INIFINITY\n",
            //    CURRENT_TIME, __FUNCTION__, index, prev_hop);
            sendMACT(mt->mt_dst, MACT_P, 0, prev_hop);
            return;
        }

        if (hop_count != mt->mt_hops_grp_leader)
            sendMGRPH_U(mt, prev_hop);

        aodv_nh_entry *nh = mt->mt_nexthops.lookup(prev_hop);
        if (nh && nh->enabled_flag == NH_ENABLE && nh->link_direction == NH_UPSTREAM)
        {
            //printf("******WARNING: %.9f in %s, node %d send RREQ_J because its previous upstream node %d becomes downstream\n",
            //    CURRENT_TIME, __FUNCTION__, index, prev_hop);
            nh->link_direction = NH_DOWNSTREAM;
            mt->mt_flags = MTF_IN_REPAIR;
            sendMRQ(mt, RREQ_J);
            return;
        }

        if (nh)
            mt->mt_nexthops.remove(nh);
        nh = new aodv_nh_entry(prev_hop);
        mt->mt_nexthops.add(nh);
        nh->link_direction = NH_DOWNSTREAM;
        nh->enabled_flag = NH_ENABLE;

        return;
    }

    // a not-on-tree node is reached
    // a not-on-tree node has no any valid link
    if (mt->mt_rep_timeout <= CURRENT_TIME)
    {
        //printf("******WARNING: %.9f in %s, node %d send MACT_P to previous hop %d, because it is a NOT_ON_TREE node and has no up-to-date reply cache\n",
        //        CURRENT_TIME, __FUNCTION__, index, prev_hop);
        sendMACT(mt->mt_dst, MACT_P, 0, prev_hop);
        return;
    }
    if (prev_hop == mt->mt_rep_selected_upstream)
    {
        //printf("******WARNING: %.9f in %s, node %d send MACT_P to previous hop %d, because its recorded upstream is the same as previous hop\n",
        //        CURRENT_TIME, __FUNCTION__, index, prev_hop);
        sendMACT(mt->mt_dst, MACT_P, 0, prev_hop);
        clearMRpyState(mt);
        return;
    }
    // recorded grp leader must not be the node itself,
    // because we do the check when recording the reply

    // has valid reply, the not-on-tree node become a tree member
    mt->mt_flags = MTF_UP;
    mt->mt_node_status = ON_TREE;
    mt->mt_grp_leader_addr = mt->mt_rep_grp_leader_addr;
    mt->mt_seqno = mt->mt_rep_seqno;
    mt->mt_hops_grp_leader = mt->mt_rep_hops_grp_leader;

    aodv_nh_entry *nh_d = new aodv_nh_entry(prev_hop);
    mt->mt_nexthops.add(nh_d);
    nh_d->link_direction = NH_DOWNSTREAM;
    nh_d->enabled_flag = NH_ENABLE;

    aodv_nh_entry *nh_u = new aodv_nh_entry(mt->mt_rep_selected_upstream);
    mt->mt_nexthops.add(nh_u);
    nh_u->link_direction = NH_UPSTREAM;
    nh_u->enabled_flag = NH_ENABLE;

    clearMRpyState(mt);
    clearMReqState(mt);

    if (hop_count != mt->mt_hops_grp_leader)
        sendMGRPH_U(mt, prev_hop);

    sendMACT(mt->mt_dst, MACT_J, mt->mt_hops_grp_leader, nh_u->next_hop);
}
/********************************************************************/
/********************************************************************/
void AODV::sendMWARN(nsaddr_t dst, u_int8_t flags, double breaktime, nsaddr_t next_hop)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    Packet *p = Packet::alloc();
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);
    struct hdr_aodv_warn *warn = HDR_AODV_WARN(p);

    warn->wn_type = AODVTYPE_WARN;
    warn->wn_flags = flags;
    warn->wn_expire = breaktime;
    warn->wn_grp = dst;

    ch->ptype() = PT_AODV;
    ch->size() = IP_HDR_LEN + warn->size();
    ch->iface() = -2;
    ch->error() = 0;
    ch->prev_hop_ = index;
    ch->direction() = hdr_cmn::DOWN;
    ch->next_hop_ = next_hop;
    ch->addr_type() = NS_AF_INET;

    ih->saddr() = index;
    ih->daddr() = next_hop;
    ih->sport() = RT_PORT;
    ih->dport() = RT_PORT;
    ih->ttl_ = 1;

    Scheduler::instance().schedule(target_, p, 0.01 * Random::uniform());
}

void AODV::recvMWARN(Packet *p)
{
#ifdef DEBUG
    printf("%.9f at node %d in recvMWARN\n", CURRENT_TIME, index);
#endif

    struct hdr_aodv_warn *warn = HDR_AODV_WARN(p);
    struct hdr_ip *ih = HDR_IP(p);
    nsaddr_t dst = warn->wn_grp;
    u_int8_t flags = warn->wn_flags;
    double breaktime = warn->wn_expire;
    nsaddr_t next_hop = ih->saddr();

    Packet::free(p);

    if (flags != WARN_D && flags != WARN_U)
    {
        return;
    }

    aodv_mt_entry *mt = mtable.mt_lookup(dst);
    if (mt == NULL || mt->mt_node_status == NOT_ON_TREE)
    {
        return;
    }

    aodv_nh_entry *nh = mt->mt_nexthops.lookup(next_hop);
    if (nh == NULL || nh->enabled_flag != NH_ENABLE)
    {
        if (nh)
            mt->mt_nexthops.remove(nh);
        return;
    }

    if (flags == WARN_U)
    {
        if (nh->link_direction == NH_UPSTREAM)
        {
            //printf("******WARNING: receive WARN_U\n");
            return;
        }

        if (nh->link_expire == 0)
            nh->link_expire = breaktime;
        return;
    }

    // flags == WARN_D
    if (nh->link_direction == NH_DOWNSTREAM)
    {
        //printf("******WARNING: receive WARN_D\n");
        return;
    }

    if (nh->link_expire == 0)
    {
        nh->link_expire = breaktime;
        sendMRQ(mt, RREQ_J);
    }
}
/******************************************************************/
// select leader
/******************************************************************/
void AODV::selectLeader(aodv_mt_entry *mt, nsaddr_t next_hop)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif

    // must be on tree and has no upstream

    // reach a ON_GROUP node
    if (mt->mt_node_status == ON_GROUP)
    {
        mt->mt_flags = MTF_UP;
        mt->mt_grp_leader_addr = index;
        mt->mt_seqno++;
        mt->mt_hops_grp_leader = 0;

        // clear all soft state
        clearMReqState(mt);
        clearMRpyState(mt);
        clearMGrpMerge(mt);

        if (mt->mt_nexthops.size() > 0)
            sendMGRPH_U(mt);

        return;
    }

    // cannot be leader
    u_int8_t size = mt->mt_nexthops.size();
    if (size == 0)
    {
        downMT(mt);
        return;
    }

    if (size == 1)
    {
        aodv_nh_entry *nh = mt->mt_nexthops.hop();
        nsaddr_t only_hop = nh->next_hop;
        mt->mt_nexthops.remove(nh);
        sendMACT(mt->mt_dst, MACT_P, 0, only_hop);

        downMT(mt);

        return;
    }

    aodv_nh_entry *nh;
    if (next_hop == INFINITY8)
        nh = mt->mt_nexthops.hop();
    else
        nh = mt->mt_nexthops.hopExcept(next_hop);
    nh->link_direction = NH_UPSTREAM;
    mt->mt_grp_leader_addr = INFINITY8;
    mt->mt_flags = MTF_UP;

    sendMACT(mt->mt_dst, MACT_GL, 0, nh->next_hop);
}

void AODV::clearMReqState(aodv_mt_entry *mt)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    mt->mt_req_cnt = 0;
    mt->mt_req_times = 0;
}

void AODV::clearMRpyState(aodv_mt_entry *mt)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    mt->mt_rep_selected_upstream = INFINITY8;
    mt->mt_rep_hops_tree = INFINITY2;
    mt->mt_rep_seqno = 0;
    mt->mt_rep_grp_leader_addr = INFINITY8;
    mt->mt_rep_hops_grp_leader = INFINITY2;
    mt->mt_rep_timeout = 0;
    mt->mt_rep_ipdst = INFINITY8;
}

void AODV::clearMGrpMerge(aodv_mt_entry *mt)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    mt->mt_grp_merge_permission = INFINITY8;
    mt->mt_grp_merge_timeout = 0;
}

void AODV::recordMRpy(aodv_mt_entry *mt, Packet *p)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    struct hdr_aodv_reply *rp = HDR_AODV_REPLY(p);
    struct hdr_aodv_reply_ext *rpe = (struct hdr_aodv_reply_ext *)(rp + rp->size());
    struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_ip *ih = HDR_IP(p);

    mt->mt_rep_selected_upstream = ch->prev_hop_;
    mt->mt_rep_hops_tree = rp->rp_hop_count;
    mt->mt_rep_seqno = rp->rp_dst_seqno;
    mt->mt_rep_grp_leader_addr = rpe->grp_leader_addr;
    mt->mt_rep_hops_grp_leader = rpe->hops_grp_leader;
    mt->mt_rep_ipdst = ih->daddr();
    mt->mt_rep_timeout = CURRENT_TIME + 1.5 * RREP_WAIT_TIME;
}

void AODV::downMT(aodv_mt_entry *mt)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    aodv_rt_entry *rt = rtable.rt_lookup(mt->mt_dst);
    if (rt == NULL)
        rt = rtable.rt_add(mt->mt_dst);
    else
        rt_down(rt);
    if (mt->mt_seqno > rt->rt_seqno)
    {
        if (mt->mt_seqno % 2 != 0)
            rt->rt_seqno = mt->mt_seqno - 1;
        else
            rt->rt_seqno = mt->mt_seqno;
    }

    mt->mt_flags = MTF_DOWN;
    mt->mt_node_status = NOT_ON_TREE;
    mt->mt_grp_leader_addr = INFINITY8;
    mt->mt_prev_grp_leader_addr = INFINITY8;
    mt->mt_hops_grp_leader = INFINITY2;
    mt->mt_seqno = 0;
    mt->mt_nexthops.clear();
}

void AODV::controlNextHello()
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    double interval = MinHelloInterval +
                      ((MaxHelloInterval - MinHelloInterval) * Random::uniform());
    assert(interval >= 0);

    hello_timeout = CURRENT_TIME + interval;

    Packet *p = Packet::alloc();
    Scheduler::instance().schedule(&htimer, p, interval);
}

void AODV::setPruneTimer(aodv_mt_entry *mt)
{
#ifdef DEBUG
    fprintf(stdout, "%s, node %i at %.9f\n", __FUNCTION__, index, CURRENT_TIME);
#endif
    if (mt->mt_node_status == ON_TREE && mt->mt_nexthops.downstream() == NULL)
    {
        Packet *p_store = Packet::alloc();
        struct hdr_ip *ih = HDR_IP(p_store);
        ih->daddr() = mt->mt_dst;
        Scheduler::instance().schedule(&prune_timer, (Event *)p_store, PRUNE_TIMER);
    }
}

