/*
tdl_dynamic_tdma.h
Note: header file for tdl_dynamic_tdma class
Usage: self-organized TDMA protocol
*/

#ifndef ns_tdl_dynamic_tdma_h
#define ns_tdl_dynamic_tdma_h

#include "marshall.h"       // contain many getbytes, storebytes functions
#include <delay.h>	        // Link delay functionality
#include <connector.h>      // Base class for Mac
#include <packet.h>	        // Base class for header
#include <random.h>         // random number generetor
#include <arp.h>            // ARP functionality
#include <ll.h>		        // LL functionality
#include <queue.h>
#include <mac.h>	        // Base class for this MAC protocol
#include "tdl_data_udp.h"

#define GET_ETHER_TYPE(x)		GET2BYTE((x))
#define SET_ETHER_TYPE(x,y)     {u_int16_t t = (y); STORE2BYTE(x,&t);}

/*
 * TDMA slot physical spec
 */

#define Phy_SlotTime		0.050		// 50 ms
#define Phy_GuardTime		0.002		// 2 ms
#define Phy_RxTxTurnaround	0.000005	// 5 us

class PHY_MIB {
public:
	double		SlotTime;
	double		GuardTime;
	double		RxTxTurnaroundTime;

};

/* ======================================================================
   Frame Formats
   ====================================================================== */


// Max data length allowed in one slot (byte)
#define MAC_TDMA_MAX_DATA_LEN 300

// How many time slots in one frame.
#define MAC_TDMA_SLOT_NUM     200

// Maximum number of nodes
#define MAX_NODE_NUM		16

#define NUM_CT_SLOTS        10

#define NUM_VSLOTS          5   //increase num vslots to raise opportunity for net entry

#define POLL_SIZE           3

// Indicate if this is the very first time the simulation runs.
#define FIRST_ROUND             -1

// Turn radio on /off
#define ON                       1
#define OFF                      0

// Initial Seed
#define INIT_SEED   0
// Available Net
#define MAX_NET_NO       8
#define CHANNEL_SPACING     25000   //channel spacing 25 kHz
#define BASE_FREQ   300000000       //first channel start at 300 MHz

enum FrameType {
	DATA_FRAME 		    = 0x1,
	CONTROL_FRAME 		= 0x2,
	NETENTRY_FRAME		= 0x4,
	POLLING_FRAME       = 0x5
};

struct hdr_mac_dynamic_tdma {
	double			arrival_time;   // use for delay measurement purpose
	FrameType		frame_type;     // use frametype
	char			srcID;          // src ID is 1 byte
	MsgType			srcMsgType;     // src message type indicate priority and update rate
	u_int8_t		srcSeed;        // src seed is int8
	u_char			dh_da[4];       // dest address
	u_char			dh_sa[4];       // src address
	u_char			dh_body[1];     // store header type as int8
};

struct hdr_nb_info {
    char			nbID[MAX_NODE_NUM-1];       //store neighbors' ID
	MsgType			nbMsgType[MAX_NODE_NUM-1];  //store neighbors' message type
	u_int8_t		nbSeed[MAX_NODE_NUM-1];     //store neighbors' seed
	u_int8_t	    nrNB;	                    // number of active neighbor in the net
	//Header access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static hdr_nb_info* access(const Packet* p) {
		return (hdr_nb_info*) p->access(offset_);
	}
};

// Mac header length
#define DYNAMIC_MAC_HDR_LEN     51

// Data structure for net entry request payload
struct net_entry_msg {
    char        entry_ID;
    u_int8_t    entry_msg; // 1 = request message, 2 = ack message
    //access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static net_entry_msg* access(const Packet* p) {
		return (net_entry_msg*) p->access(offset_);
	}
};

#define NET_ENTRY_PAYLOAD_SIZE  2

// Data structure for control payload
struct control_msg {
    char        control_ID;
    u_int8_t    control_msgtype; // 1 = request message, 2 = ack message
    //access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static control_msg* access(const Packet* p) {
		return (control_msg*) p->access(offset_);
	}
};

#define NET_ENTRY_PAYLOAD_SIZE  2

// Data structure for polling message
struct polling_msg {
    char    runner_ups[3];  // store first three runner ups with highest hash.
    //access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static polling_msg* access(const Packet* p) {
		return (polling_msg*) p->access(offset_);
	}
};
#define POLLING_PAYLOAD_SIZE    3

#define DATA_Time(len)	(8 * (len) / bandwidth_)

/* Timers */
class MacDynamicTdma;

class MacDynamicTdmaTimer : public Handler {
public:
	MacDynamicTdmaTimer(MacDynamicTdma* m, double s = 0) : mac(m) {
		busy_ = paused_ = 0; stime = rtime = 0.0; slottime_ = s;
	}

	virtual void handle(Event *e) = 0;

	virtual void start(Packet *p, double time);
	virtual void stop(Packet *p);
	virtual void pause(void) { assert(0); }
	virtual void resume(void) { assert(0); }

	inline int busy(void) { return busy_; }
	inline int paused(void) { return paused_; }
	inline double slottime(void) { return slottime_; }
	inline double expire(void) {
		return ((stime + rtime) - Scheduler::instance().clock());
	}
protected:
	MacDynamicTdma 	*mac;
	int		busy_;
	int		paused_;
	Event		intr;
	double		stime;	// start time
	double		rtime;	// remaining time
	double		slottime_;
};

/* Timers to schedule transmitting and receiving. */
class SlotDynamicTdmaTimer : public MacDynamicTdmaTimer {
public:
	SlotDynamicTdmaTimer(MacDynamicTdma *m) : MacDynamicTdmaTimer(m) {}
	void 	handle(Event *e);
};

/* Timers to control packet sending and receiving time. */
class RxPktDynamicTdmaTimer : public MacDynamicTdmaTimer {
public:
	RxPktDynamicTdmaTimer(MacDynamicTdma *m) : MacDynamicTdmaTimer(m) {}

	void	handle(Event *e);
};

class TxPktDynamicTdmaTimer : public MacDynamicTdmaTimer {
public:
	TxPktDynamicTdmaTimer(MacDynamicTdma *m) : MacDynamicTdmaTimer(m) {}

	void	handle(Event *e);
};
/* Timers to control backoff time, after receiving polling. */
class BackOffTimer : public MacDynamicTdmaTimer {
public:
    BackOffTimer(MacDynamicTdma *m) : MacDynamicTdmaTimer(m) {}

    void    handle(Event *e);
};
// Record to trace file timer
class RecordDynTimer : public TimerHandler {
 public:
	RecordDynTimer(MacDynamicTdma* t) : TimerHandler(), t_(t) {}
	inline virtual void expire(Event*);
 protected:
	MacDynamicTdma* t_;
};

// Dynamic TDMA MAC Layer
class MacDynamicTdma : public Mac {
	friend class SlotDynamicTdmaTimer;
	friend class RxPktDynamicTdmaTimer;
	friend class TxPktDynamicTdmaTimer;
	friend class BackOffTimer;

public:
	MacDynamicTdma(PHY_MIB* p);
	void		recv(Packet *p, Handler *h);
	inline int 	hdr_dst(char* hdr, int dst = -2);
	inline int	hdr_src(char* hdr, int src = -2);
	inline int	hdr_type(char* hdr, u_int16_t type = 0);

	/* Timer handler */
	void slotHandler(Event *e);
	void recvHandler(Event *e);
	void sendHandler(Event *e);
	void backoffHandler(Event *e);

	void recordHandler();


protected:
	PHY_MIB		*phymib_;

	// max slot num can be configured
	int		    max_slot_num_;
    // Node ID
    char        node_ID_;
    // Node Seed
    u_int8_t    node_seed_;
    u_int8_t    node_last_seed_;
    // Node Message Type
    MsgType     node_msg_type_;
    // Node Message Size
    int         node_msg_size_;
    // Net ID
    u_int8_t    net_ID_;
    // assigned Net ID
    int         assigned_Net_;
    // Net Frequency
    double      net_freq_;
    // VSLOTs seed
    u_int8_t    vslots[NUM_VSLOTS];
    // define VSLOTs
    char        vslotIDs[NUM_VSLOTS];




private:
	int command(int argc, const char*const* argv);
      void radioSwitch(int i);

	  /* Packet Transmission Functions.*/
	  void    sendUp(Packet* p);
	  void    sendDown(Packet* p);

	  /* Actually receive data packet when rxTimer times out. */
	  void recvDATA(Packet *p);
	  /* Actually send the packet buffered. */
	  void send();
	  /* Switch to net channel */
	  void switchToNetChannel();
	  /* Net Entry */
	  void doNetEntry();
	  /* Assign Seed */
	  u_int8_t assignSeed();
      /* Allocate data slot */
      void allocateDataSlots();
      void sortingMembers();
      int assignSlots(int id, int msg_t, int rate);
      /* sorting algorithm */
      void bubbleSort(int *arr1, int *arr2, int len);
      void bubbleSort2(int *arr1, char *arr2, int len);
      /* Access control slots for transmission update or net entry */
      void accessControlSlots();
      /* compute hash */
      int hashSeed(int slot_num, int seed);
      /* Find winning node for a net control slot */
      void findHashAndSort(int slot_num, int vslot_num);
      char findRunnerUpNode(int pos);
      /* Send Net Entry message */
      void sendNetEntry();
      /* Send Net entry ACK */
      void sendNetEntryACK();
      /* send update information */
      void sendControl();
      void sendControlACK();
      /* send polling */
      void sendPolling();
      /* check poll list, return order in the list */
      int checkPollList(Packet *p); // return 0 if a node is not in the poll list
      /* Update Node's Neighbor table */
      void updateNeighborTable(Packet *p);
      /* Update node's neighbor */
      void updateNeighbor(char id, MsgType msg_t, u_int8_t seed, u_int8_t hops);
      /* find leaving node at the end of cycle */
      void findLeavingNodes();
      /* Record 1-hop neighbor found in current frame */
      void recordOneHop(char id);
      /* check if giving neighbor is a 1-hop neighbor */
      int checkOneHop(char id);
      /* Record 2-hop neighbor found in current frame */
      void recordTwoHop(char id);
      /* check if giving neighbor is a 2-hop neighbor */
      int checkTwoHop(char id);
      /* determine number of hop of a given neighbor id */
      int findNrHops(char id);
      /* determine if channel is idle */
	  inline int	is_idle(void);


	  void mac_log(Packet *p) {
	    logtarget_->recv(p, (Handler*) 0);
	  }

	  inline double TX_Time(Packet *p) {
	    double t = DATA_Time((HDR_CMN(p))->size());

	    if(t < 0.0) {
	      drop(p, "XXX");
	      exit(1);
	    }
	    return t;
	  }

	  inline u_int16_t usec(double t) {
	    u_int16_t us = (u_int16_t)ceil(t *= 1e6);
	    return us;
	  };

	  /* Timers */
	  SlotDynamicTdmaTimer mhSlot_;
	  TxPktDynamicTdmaTimer mhTxPkt_;
	  RxPktDynamicTdmaTimer mhRxPkt_;
	  BackOffTimer mhBkOff_;
	  RecordDynTimer recT_;

	  /* Internal MAC state from mac.h */
	  MacState	rx_state_;	// incoming state (MAC_RECV or MAC_IDLE)
	  MacState	tx_state_;	// outgoing state

	  /* The indicator of the radio. */
	  int radio_active_;
	  /* The indicator of the node, if it is operating in the net */
	  int is_in_net;
	  /* The indicator of TDL running */
	  int is_app_start;
	  /* Indicator of the first node in net */
	  int is_first_node;
	  /* Indicate that a node is in net entry phase */
	  int is_net_entry;
	  // number of control slots a node has to wait before it try to access control slot to send entry
	  int waiting_ct_slot;
	  int waiting_ct_slot_cnt;
	  int found_exist_node;
      /* Indicate if an entry node is waiting for ACK */
      int is_ack_waiting;
      int waiting_ack_ctslot_count;
      /* Indicate required control message */
      int is_control_msg_required;
      int is_cack_waiting;
      int waiting_cack_count;
      /* Indicate a node is in back off period */
      int is_back_off;
      /* Indicate a node receive transmission during back off period */
      int is_rec_in_back_off;

      int is_seed_sent;

      int is_receiving;

	  int tx_active_;	// transmitter is ACTIVE

	  int num_alloc_;    // number of allocated nodes

      //int slot_conflict[MAC_TDMA_SLOT_NUM];
      int is_conflict_in_frame;
      int num_conflicts;

      //transmission update time measurement
      double update_msg_arr_time;

      //delay measurement
      double packet_arr_time;
      double cumulative_delay;
      int num_packets_sent;
      double avg_delay;

      //throughput, channel efficiency, channel utilization measurement
      int num_bytes_sent;
      int num_tbytes_sent;

      //time slot utilization
      int num_slots_reserved;
      int num_slots_used;


      //schedule record
      int record_time;

	  NsObject*	logtarget_;

      /* control packet buffer */
      Packet *ctrlPkt_;
      Packet *nePkt_;


	  // The time duration for each slot.
	  static double slot_time_;
	  // Duration for data in each slot
	  static double data_time_;
	  /* The start time for whole TDMA scheduling.
	  	All net should have the same start time
	  */
	  static double start_time_;



	  /* Data structure for tdma scheduling. */

	  int *tdma_schedule_;			// Time slot reserved table
	  char *table_nb_id;		        // Neighbor's ID Table
	  MsgType *table_nb_msg_type;		// Neighbor's Message Type Table
	  u_int8_t *table_nb_seed;			// Neighbor's seeds
	  u_int8_t *table_nb_hops;			// Neighbor's number of hops (1 = 1 hop, 2 = 2 hops, 3 = over 2 hops)


      // Data structure for net entry slots
      char ne_vslot;


	  // Keep track of current slot.
	  int slot_count_;


      // Hash Table order from high to low
      char hashID[MAX_NODE_NUM+NUM_VSLOTS];
      int hashValue[MAX_NODE_NUM+NUM_VSLOTS];

	  //keep track of 1-hop and 2-hop neighbor in current frame
      char onehop_nb_id[MAX_NODE_NUM-1];
      u_int8_t onehop_nb_found[MAX_NODE_NUM-1];
      char twohop_nb_id[MAX_NODE_NUM-1];
      u_int8_t twohop_nb_found[MAX_NODE_NUM-1];


      char poll_list_[POLL_SIZE];
	  // How many packets has been sent out?
	  static int tdma_ps_;
	  // How many packets has been received?
	  static int tdma_pr_;

	  //Measurement variables
	  //Net Entry time measurement variables
	  double first_pkt_atime;
	  double ne_ack_rtime;


};

double MacDynamicTdma::slot_time_ = 0;
double MacDynamicTdma::data_time_ = 0;
double MacDynamicTdma::start_time_ = 0;



int MacDynamicTdma::tdma_ps_ = 0;
int MacDynamicTdma::tdma_pr_ = 0;

#endif

