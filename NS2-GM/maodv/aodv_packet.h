/*
Copyright (c) 1997, 1998 Carnegie Mellon University.  All Rights
Reserved. 

Permission to use, copy, modify, and distribute this
software and its documentation is hereby granted (including for
commercial or for-profit use), provided that both the copyright notice and this permission notice appear in all copies of the software, derivative works, or modified versions, and any portions thereof, and that both notices appear in supporting documentation, and that credit is given to Carnegie Mellon University in all publications reporting on direct or indirect use of this code or its derivatives.

ALL CODE, SOFTWARE, PROTOCOLS, AND ARCHITECTURES DEVELOPED BY THE CMU
MONARCH PROJECT ARE EXPERIMENTAL AND ARE KNOWN TO HAVE BUGS, SOME OF
WHICH MAY HAVE SERIOUS CONSEQUENCES. CARNEGIE MELLON PROVIDES THIS
SOFTWARE OR OTHER INTELLECTUAL PROPERTY IN ITS ``AS IS'' CONDITION,
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE OR
INTELLECTUAL PROPERTY, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.

Carnegie Mellon encourages (but does not require) users of this
software or intellectual property to return any improvements or
extensions that they make, and to grant Carnegie Mellon the rights to redistribute these changes without encumbrance.

The AODV code developed by the CMU/MONARCH group was optimized and tuned by Samir Das and Mahesh Marina, University of Cincinnati. The work was partially done in Sun Microsystems.
*/

#ifndef __aodv_packet_h__
#define __aodv_packet_h__

//#include <config.h>
//#include "aodv.h"
#define AODV_MAX_ERRORS 100

/* =====================================================================
   Packet Formats...
   ===================================================================== */
#define AODVTYPE_HELLO 0x01
#define AODVTYPE_RREQ 0x02
#define AODVTYPE_RREP 0x04
#define AODVTYPE_RERR 0x08
#define AODVTYPE_RREP_ACK 0x10

/*** added for multicast by Indresh ***/
#define AODVTYPE_GRPH 0x06
#define AODVTYPE_MACT 0x05
/***************************/

/*** added for prediction by Indresh in multicast***/
#define AODVTYPE_WARN 0x07
#define AODVTYPE_LPW 0x09
#define AODVTYPE_RPE 0x03
#define AODVTYPE_LINK_RREQ 0x11
/****************************/

/*
 * AODV Routing Protocol Header Macros
 */
#define HDR_AODV(p) ((struct hdr_aodv *)hdr_aodv::access(p))
#define HDR_AODV_REQUEST(p) ((struct hdr_aodv_request *)hdr_aodv::access(p))
#define HDR_AODV_REPLY(p) ((struct hdr_aodv_reply *)hdr_aodv::access(p))
#define HDR_AODV_ERROR(p) ((struct hdr_aodv_error *)hdr_aodv::access(p))
#define HDR_AODV_RREP_ACK(p) ((struct hdr_aodv_rrep_ack *)hdr_aodv::access(p))

/*** added for multicast by Indresh ***/
#define HDR_AODV_GRPH(p) ((struct hdr_aodv_grph *)hdr_aodv::access(p))
#define HDR_AODV_MACT(p) ((struct hdr_aodv_mact *)hdr_aodv::access(p))
/***************************/

/*** added for prediction by Indresh***/
#define HDR_AODV_WARN(p) ((struct hdr_aodv_warn *)hdr_aodv::access(p))
#define HDR_AODV_REQUEST_LINK(p) ((struct hdr_aodv_request_link *)hdr_aodv::access(p))
#define HDR_AODV_RPE(p) ((struct hdr_aodv_rpe *)hdr_aodv::access(p))
#define HDR_AODV_LPW(p) ((struct hdr_aodv_lpw *)hdr_aodv::access(p))
/******************************************/

/*
 * General AODV Header - shared by all formats
 */
struct hdr_aodv
{
        u_int8_t ah_type;
        /*
        u_int8_t        ah_reserved[2];
        u_int8_t        ah_hopcount;
	*/
        // Header access methods
        static int offset_; // required by PacketHeaderManager
        inline static int &offset() { return offset_; }
        inline static hdr_aodv *access(const Packet *p)
        {
                return (hdr_aodv *)p->access(offset_);
        }
};

/*** added for multicast by Indresh ***/
#define RREQ_NO_FLAG 0x00
#define RREQ_J 0x01
#define RREQ_JR 0x03
#define RREQ_R 0x04
/**************************/

struct hdr_aodv_request
{
        u_int8_t rq_type; // Packet Type

        /*** modified for multicast by Indresh P ***/
        //u_int8_t        reserved[2];
        u_int8_t rq_flags;
        u_int8_t reserved;
        /******************************/

        u_int8_t rq_hop_count; // Hop Count
        u_int32_t rq_bcast_id; // Broadcast ID

        nsaddr_t rq_dst;        // Destination IP Address
        u_int32_t rq_dst_seqno; // Destination Sequence Number
        nsaddr_t rq_src;        // Source IP Address
        u_int32_t rq_src_seqno; // Source Sequence Number

        double rq_timestamp; // when REQUEST sent;
                             // used to compute route discovery latency

        // This define turns on gratuitous replies- see aodv.cc for implementation contributed by
        // Anant Utgikar, 09/16/02.
        //#define RREQ_GRAT_RREP	0x80

        inline int size()
        {
                int sz = 0;
                /*
  	sz = sizeof(u_int8_t)		// rq_type
	     + 2*sizeof(u_int8_t) 	// reserved
	     + sizeof(u_int8_t)		// rq_hop_count
	     + sizeof(double)		// rq_timestamp
	     + sizeof(u_int32_t)	// rq_bcast_id
	     + sizeof(nsaddr_t)		// rq_dst
	     + sizeof(u_int32_t)	// rq_dst_seqno
	     + sizeof(nsaddr_t)		// rq_src
	     + sizeof(u_int32_t);	// rq_src_seqno
  */
                sz = 7 * sizeof(u_int32_t);
                assert(sz >= 0);
                return sz;
        }
};

/*** added for multicast by Indresh ***/
#define RREP_NO_FLAG 0x00
#define RREP_J 0x01
#define RREP_JR 0x03
#define RREP_R 0x04
/***************************/

struct hdr_aodv_reply
{
        u_int8_t rp_type; // Packet Type

        /*** modified for multicast ***/
        //u_int8_t        reserved[2];
        u_int8_t rp_flags;
        u_int8_t reserved;
        /*******************************/

        u_int8_t rp_hop_count;  // Hop Count
        nsaddr_t rp_dst;        // Destination IP Address
        u_int32_t rp_dst_seqno; // Destination Sequence Number
        nsaddr_t rp_src;        // Source IP Address
        double rp_lifetime;     // Lifetime

        double rp_timestamp; // when corresponding REQ sent;
                             // used to compute route discovery latency

        inline int size()
        {
                int sz = 0;
                /*
  	sz = sizeof(u_int8_t)		// rp_type
	     + 2*sizeof(u_int8_t) 	// rp_flags + reserved
	     + sizeof(u_int8_t)		// rp_hop_count
	     + sizeof(double)		// rp_timestamp
	     + sizeof(nsaddr_t)		// rp_dst
	     + sizeof(u_int32_t)	// rp_dst_seqno
	     + sizeof(nsaddr_t)		// rp_src
	     + sizeof(u_int32_t);	// rp_lifetime
  */
                sz = 6 * sizeof(u_int32_t);
                assert(sz >= 0);
                return sz;
        }

        // added by Indresh for energy for geomentric routing
        double t_energy, min_energy_;
};

struct hdr_aodv_error
{
        u_int8_t re_type;     // Type
        u_int8_t reserved[2]; // Reserved
        u_int8_t DestCount;   // DestCount
        // List of Unreachable destination IP addresses and sequence numbers
        nsaddr_t unreachable_dst[AODV_MAX_ERRORS];
        u_int32_t unreachable_dst_seqno[AODV_MAX_ERRORS];

        inline int size()
        {
                int sz = 0;
                /*
  	sz = sizeof(u_int8_t)		// type
	     + 2*sizeof(u_int8_t) 	// reserved
	     + sizeof(u_int8_t)		// length
	     + length*sizeof(nsaddr_t); // unreachable destinations
  */
                sz = (DestCount * 2 + 1) * sizeof(u_int32_t);
                assert(sz);
                return sz;
        }
};

struct hdr_aodv_rrep_ack
{
        u_int8_t rpack_type;
        u_int8_t reserved;
};

/*** added for multicast by Indresh ***/
#define MACT_J 0x01
#define MACT_P 0x02
#define MACT_GL 0x03

struct hdr_aodv_mact
{
        u_int8_t mact_type;
        u_int8_t mact_flags;
        u_int8_t reserved;
        u_int8_t mact_hop_count;
        nsaddr_t mact_grp_dst;
        nsaddr_t mact_src;
        u_int32_t mact_src_seqno;

        inline int size()
        {
                int sz = 4 * sizeof(u_int32_t);
                return sz;
        }
};

#define GRPH_U 0x01
#define GRPH_NO_FLAG 0x00
#define GRPH_M 0x02
struct hdr_aodv_grph
{
        u_int8_t gh_type;
        u_int8_t gh_flags;
        u_int8_t reserved;
        u_int8_t gh_hop_count;
        nsaddr_t gh_grp_leader_addr;
        nsaddr_t gh_multi_grp_addr;
        u_int32_t gh_grp_seqno;

        inline int size()
        {
                int sz = 4 * sizeof(u_int32_t);
                return sz;
        }
};

#define AODVTYPE_RREP_EXT 0x05
#define AODVTYPE_RREP_EXT_LEN 0x06
struct hdr_aodv_reply_ext
{
        u_int8_t type;
        u_int8_t length;
        u_int16_t hops_grp_leader;
        nsaddr_t grp_leader_addr;

        inline int size()
        {
                int sz = 2 * sizeof(u_int32_t);
                return sz;
        }
};

#define AODVTYPE_RREQ_EXT 0x04
#define AODVTYPE_RREQ_EXT_LEN 0x02
struct hdr_aodv_request_ext
{
        u_int8_t type;
        u_int8_t length;
        u_int16_t hop_count;

        inline int size()
        {
                int sz = 1 * sizeof(u_int32_t);
                return sz;
        }
};
/************************/

/*** added for prediction by Indresh ***/
#define WARN_U 0x01
#define WARN_D 0x02
struct hdr_aodv_warn
{
        u_int8_t wn_type;
        u_int8_t wn_flags;
        u_int8_t reserved;
        nsaddr_t wn_grp;
        double wn_expire;

        inline int size()
        {
                int sz = 3 * sizeof(u_int8_t) + 2 * sizeof(u_int32_t);
                return sz;
        }
};

struct hdr_aodv_lpw
{
        u_int8_t rp_type;
        nsaddr_t rp_dst;
        double breakTime;

        inline int size()
        {
                int sz = 2 * sizeof(u_int32_t) + sizeof(u_int8_t);
                return sz;
        }
};

struct hdr_aodv_rpe
{
        u_int8_t rp_type;     // Type
        u_int8_t reserved[2]; // Reserved
        u_int8_t DestCount;   // DestCount
        // List of destination IP addresses and sequence numbers

        double breakTime;
        nsaddr_t vulnerable_dst[AODV_MAX_ERRORS];
        u_int32_t vulnerable_dst_seqno[AODV_MAX_ERRORS];

        inline int size()
        {
                int sz = (DestCount * 2 + 1) * sizeof(u_int32_t) + sizeof(double);
                assert(sz);
                return sz;
        }
};

struct hdr_aodv_request_link
{
        u_int8_t rq_type; // Packet Type
        u_int8_t rq_flags;
        u_int8_t reserved;
        u_int8_t rq_hop_count; // Hop Count

        u_int32_t rq_bcast_id; // Broadcast ID

        nsaddr_t rq_dst;        // Destination IP Address
        u_int32_t rq_dst_seqno; // Destination Sequence Number
        nsaddr_t rq_src;        // Source IP Address
        u_int32_t rq_src_seqno; // Source Sequence Number

        double rq_timestamp; // when REQUEST sent

        nsaddr_t from;
        nsaddr_t to;

        inline int size()
        {
                int sz = 9 * sizeof(u_int32_t);
                return sz;
        }
};
/**********************/

// for size calculation of header-space reservation
union hdr_all_aodv
{
        hdr_aodv ah;
        hdr_aodv_request rreq;
        hdr_aodv_reply rrep;
        hdr_aodv_error rerr;
        hdr_aodv_rrep_ack rrep_ack;

        /*** added for multicast by Indresh ***/
        hdr_aodv_grph grph;
        hdr_aodv_mact mact;
        /***************************/

        /*** added for prediction by Indresh ***/
        hdr_aodv_lpw lpw;
        hdr_aodv_rpe rep;
        hdr_aodv_request_link rq;
        hdr_aodv_warn warn;
        /****************************/
};

#endif /* __aodv_packet_h__ */
