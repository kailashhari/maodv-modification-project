#ifndef __aodv_rtable_h__
#define __aodv_rtable_h__

#include <assert.h>
#include <sys/types.h>
#include <config.h>
#include <lib/bsd-list.h>
#include <scheduler.h>

#define CURRENT_TIME Scheduler::instance().clock()
#define INFINITY2 0xff

/*
   AODV Neighbor Cache Entry
*/
class AODV_Neighbor
{
        friend class AODV;
        friend class aodv_rt_entry;

public:
        AODV_Neighbor(u_int32_t a) { nb_addr = a; }

protected:
        LIST_ENTRY(AODV_Neighbor)
        nb_link;
        nsaddr_t nb_addr;
        double nb_expire; // ALLOWED_HELLO_LOSS * HELLO_INTERVAL
};

LIST_HEAD(aodv_ncache, AODV_Neighbor);

/*
   AODV Precursor list data structure
*/
class AODV_Precursor
{
        friend class AODV;
        friend class aodv_rt_entry;

public:
        AODV_Precursor(u_int32_t a) { pc_addr = a; }

protected:
        LIST_ENTRY(AODV_Precursor)
        pc_link;
        nsaddr_t pc_addr; // precursor address
};

LIST_HEAD(aodv_precursors, AODV_Precursor);

/*
  Route Table Entry
*/

class aodv_rt_entry
{
        friend class aodv_rtable;
        friend class AODV;
        friend class LocalRepairTimer;

public:
        aodv_rt_entry();
        ~aodv_rt_entry();

        void nb_insert(nsaddr_t id);
        AODV_Neighbor *nb_lookup(nsaddr_t id);

        void pc_insert(nsaddr_t id);
        AODV_Precursor *pc_lookup(nsaddr_t id);
        void pc_delete(nsaddr_t id);
        void pc_delete(void);
        bool pc_empty(void);

        double rt_req_timeout; // when I can send another req

        /*** added for multicast by Indresh ***/
        u_int8_t rt_req_times;
        /****************************/

        u_int8_t rt_req_cnt; // number of route requests

protected:
        LIST_ENTRY(aodv_rt_entry)
        rt_link;

        nsaddr_t rt_dst;
        u_int32_t rt_seqno;
        /* u_int8_t 	rt_interface; */
        u_int16_t rt_hops;     // hop count
        int rt_last_hop_count; // last valid hop count
        nsaddr_t rt_nexthop;   // next hop IP address
                               /* list of precursors */
        aodv_precursors rt_pclist;
        double rt_expire; // when entry expires
        u_int8_t rt_flags;

#define RTF_DOWN 0
#define RTF_UP 1
#define RTF_IN_REPAIR 2

/*** added for prediction by Indresh in unicast ***/
#define RTF_P_LINK 3
#define RTF_PREDICTION 4

        u_int8_t rt_prevnode_warning;
        u_int32_t rt_retry_pid;
        u_int8_t rt_retry_times;
        /*****************************************/

        /*
         *  Must receive 4 errors within 3 seconds in order to mark
         *  the route down.
        u_int8_t        rt_errors;      // error count
        double          rt_error_time;
#define MAX_RT_ERROR            4       // errors
#define MAX_RT_ERROR_TIME       3       // seconds
         */

#define MAX_HISTORY 3
        double rt_disc_latency[MAX_HISTORY];
        char hist_indx;
        int rt_req_last_ttl; // last ttl value used
                             // last few route discovery latencies
                             // double 		rt_length [MAX_HISTORY];
                             // last few route lengths

        /*
         * a list of neighbors that are using this route.
         */
        aodv_ncache rt_nblist;
};

/*
  The Routing Table
*/

class aodv_rtable
{
public:
        aodv_rtable() { LIST_INIT(&rthead); }

        aodv_rt_entry *head() { return rthead.lh_first; }

        aodv_rt_entry *rt_add(nsaddr_t id);
        void rt_delete(nsaddr_t id);
        aodv_rt_entry *rt_lookup(nsaddr_t id);

private:
        LIST_HEAD(aodv_rthead, aodv_rt_entry)
        rthead;
};

#endif /* _aodv__rtable_h__ */
