#ifndef ns_tdl_dynamic_tdma_h
#define ns_tdl_dynamic_tdma_h

#include "marshall.h"         // contain many getbytes, storebytes functions
#include <delay.h>	      // Link delay functionality
#include <connector.h>        // Base class for Mac
#include <packet.h>	      // Base class for header
#include <random.h>           // random number generetor
#include <arp.h>              // ARP functionality
#include <ll.h>		      // LL functionality
#include <mac.h>	      // Base class for this MAC protocol
#include "tdl_data_udp.h"

#define GET_ETHER_TYPE(x)		GET2BYTE((x))
#define SET_ETHER_TYPE(x,y)            {u_int16_t t = (y); STORE2BYTE(x,&t);}

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
#define MAC_TDMA_SLOT_NUM     400

// Maximum number of nodes
#define MAX_NODE_NUM		16

#define NUM_NC_SLOTS        4

#define NUM_CT_SLOTS        16

#define NUM_VSLOTS          3   //adjustable between 3-5

// Indicate if this is the very first time the simulation runs.
#define FIRST_ROUND             -1

// Turn radio on /off
#define ON                       1
#define OFF                      0

// Initial Seed
#define INIT_SEED   0
// Available Net
#define MAX_NET_NO       8
#define CHANNEL_SPACING     5000000


enum FrameType {
	DATA_FRAME 		    = 0x1,
	CONTROL_FRAME 		= 0x2,
	NETCONTROL_FRAME 	= 0x3,
	NETENTRY_FRAME		= 0x4,
	POLLING_FRAME       = 0x5
};

struct hdr_mac_dynamic_tdma {
	double			arrival_time; // use for delay measurement purpose
	FrameType		frame_type; //use frametype as defined in mac.h
	char			srcID;      // src ID is 1 byte
	MsgType			srcMsgType;    // src message type indicate priority and update rate
	u_int8_t		srcSeed;    // src seed is int32
	//char			nbID[MAX_NODE_NUM-1]; //store neighbors' ID
	//MsgType			nbMsgType[MAX_NODE_NUM-1]; //store neighbors' message type
	//u_int8_t		nbSeed[MAX_NODE_NUM-1]; //store neighbors' seed
	//u_int8_t	    nrNB;	   // number of active neighbor in the net
	u_char			dh_da[4]; // dest address
	u_char			dh_sa[4]; // src address
	u_char			dh_body[1]; // store header type as int16
};

struct hdr_nb_info {
    char			nbID[MAX_NODE_NUM-1]; //store neighbors' ID
	MsgType			nbMsgType[MAX_NODE_NUM-1]; //store neighbors' message type
	u_int8_t		nbSeed[MAX_NODE_NUM-1]; //store neighbors' seed
	u_int8_t	    nrNB;	   // number of active neighbor in the net
	//Header access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static hdr_nb_info* access(const Packet* p) {
		return (hdr_nb_info*) p->access(offset_);
	}
};

// DYNAMIC_MAC_HDR_LEN is practical MAC header len that we account for transmission
// arrival_time, dh_da, dh_sa, dh_body are not included, because they only be used in simulation.
#define DYNAMIC_MAC_HDR_LEN     (4+8+4+8+8*(MAX_NODE_NUM-1)+4*(MAX_NODE_NUM-1)+8*(MAX_NODE_NUM-1)+8+4)/8

// Data structure for net control payload
struct net_control_info {
    u_int8_t          net_ID;         // Net unique ID
    u_int8_t          vslot_seed[NUM_VSLOTS];  // Seed of vslots
    //u_int8_t          vslot_wins[NUM_VSLOTS];
    //construct payload as packet header so that it can be easily access
    //Header access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static net_control_info* access(const Packet* p) {
		return (net_control_info*) p->access(offset_);
	}
};

#define NET_CONTROL_PAYLOAD_SIZE    1+1*NUM_VSLOTS

// Data structure for net entry request payload
struct net_entry_msg {
    char        entry_ID;
    //MsgType     entry_msg_t;
    //u_int8_t    entry_seed;
    u_int8_t    entry_msg; // 1 = request message, 2 = ack message
    //access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static net_entry_msg* access(const Packet* p) {
		return (net_entry_msg*) p->access(offset_);
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

class BackOffTimer : public MacDynamicTdmaTimer {
public:
    BackOffTimer(MacDynamicTdma *m) : MacDynamicTdmaTimer(m) {}

    void    handle(Event *e);
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
    int         assigned_Net_;   // 0-8
    // Net Frequency
    double      net_freq_;
    // VSLOTs seed
    u_int8_t    vslots[NUM_VSLOTS];
    // define VSLOTs
    char        vslotIDs[NUM_VSLOTS];

    // Control Channel
    double      control_channel_;


private:
	int command(int argc, const char*const* argv);
	// Do slot scheduling for the active nodes within one cluster.
	  void re_schedule();
	  void radioSwitch(int i);

	  /* Packet Transmission Functions.*/
	  void    sendUp(Packet* p);
	  void    sendDown(Packet* p);

	  /* Actually receive data packet when rxTimer times out. */
	  void recvDATA(Packet *p);
	  /* Actually send the packet buffered. */
	  void send();
	  /* Switch to control channel */
	  void switchToControlChannel();
	  /* Switch to net channel */
	  void switchToNetChannel();
	  /* Turn on radio and listen in net control slot */
	  void listenForNC();
	  /* Turn on radio and determine whether to tx or rx net info */
	  void accessNCToTxRx();
	  /* Get Net info  */
	  //void selectNet();
	  void getNet();
	  /* Net Entry */
	  void doNetEntry();
	  /* Assign Seed */
	  u_int8_t assignSeed();
      /* Allocate data slot */
      void allocateDataSlots();
      void sortingMembers();
      int assignSlots(int id, int msg_t, int rate);
      //void assignSlots(int id,int msg_t);
      /* sorting algorithm */
      void bubbleSort(int *arr1, int *arr2, int len);
      void bubbleSort2(int *arr1, char *arr2, int len);
      /* Access control slots for transmission update or net entry */
      void accessControlSlots();
      /* compute hash */
      int hashSeed(int slot_num, int seed);
      /* Find winning node for a net control slot */
      void findHashAndSort(int slot_num, int vslot_num);
      char findWinningNode(int pos);
      int checkSlotsWinner(char node_id,int slot_type);     // slot_type: 1 is NC, 2 is CT
      /* Send Net Control message */
      void sendNetControl();
      /* Send Net Entry message */
      void sendNetEntry();
      /* Send Net entry ACK */
      //void createNetEntryACK();
      void sendNetEntryACK();
      /* send update information */
      void sendControl();
      /* send polling */
      void sendPolling();
      /* check poll list, return order in the list */
      int checkPollList(Packet *p); // return 0 if a node is not in the poll list
      /* Update Net Table */
      void updateNetTable(Packet *p);
      /* Update Net's Neighbor table */
      void updateNetNeighborTable(Packet *p,u_int8_t netID);
      /* Update Node's Neighbor table */
      void updateNeighborTable(Packet *p);
      /* Update net's neighbor */
      void updateNetNeighbor(char id, MsgType msg_t, u_int8_t seed, u_int8_t netID);
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
      /* Determine whether to stay or switch net */
      void isSwitchNet(Packet *p);



	  inline int	is_idle(void);


	  void mac_log(Packet *p) {
	    logtarget_->recv(p, (Handler*) 0);
	  }

	  inline double TX_Time(Packet *p) {
	    double t = DATA_Time((HDR_CMN(p))->size());

	    //    printf("<%d>, packet size: %d, tx-time: %f\n", index_, (HDR_CMN(p))->size(), t);
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
      /* Indicate if an entry node is waiting for ACK */
      int is_ack_waiting;
      int waiting_slot_count;
      /* Indicate required control message */
      int is_control_msg_required;
      /* Indicate a node is in back off period */
      int is_back_off;
      /* Indicate a node receive transmission during back off period */
      int is_rec_in_back_off;

      int is_seed_sent;

	  int		tx_active_;	// transmitter is ACTIVE

	  NsObject*	logtarget_;

      /* control packet buffer */
      Packet *ctrlPkt_;
      Packet *nePkt_;

	  /* TDMA scheduling state.
	   */


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

	  // Net Table structure
	  u_int8_t net_IDs[MAX_NET_NO];
	  u_int8_t net_Found[MAX_NET_NO];
	  u_int8_t net_vslots[MAX_NET_NO][NUM_VSLOTS];
      //u_int8_t net_vslots[MAX_NET_NO][NUM_VSLOTS];
	  char net_nb_id[MAX_NET_NO][MAX_NODE_NUM-1];
	  u_int8_t net_nb_seed[MAX_NET_NO][MAX_NODE_NUM-1];
	  MsgType net_nb_msg_type[MAX_NET_NO][MAX_NODE_NUM-1];

      int num_alloc_;

      // Data structure for net entry slots
      char ne_vslot;


	  // Keep track of current slot.
	  // When slot_count_ = active_nodes_, it indicates a new cycle.
	  int slot_count_;


      // Hash Table order from high to low
      char hashID[MAX_NODE_NUM];
      int hashValue[MAX_NODE_NUM];
	  // keep track of nodes and vslots that win NC or control slots
	  // max number of vslot that could win NC slot is 3 in a frame (vslot will not be further considered in finding winning node when vslots have win 3 NC slot)
	  char nc_winner[NUM_NC_SLOTS];
	  int nc_slot_count;
	  // max number of vslot that could win control slot is 5, which make max number of NE slot is 5 in a frame
      char ct_winner[NUM_CT_SLOTS];
      int ct_slot_count;

      //keep track of 1-hop and 2-hop neighbor in current frame
      char onehop_nb_id[MAX_NODE_NUM-1];
      u_int8_t onehop_nb_found[MAX_NODE_NUM-1];
      char twohop_nb_id[MAX_NODE_NUM-1];
      u_int8_t twohop_nb_found[MAX_NODE_NUM-1];
	  // How many packets has been sent out?
	  static int tdma_ps_;
	  // How many packets has been received?
	  static int tdma_pr_;

};

double MacDynamicTdma::slot_time_ = 0;
double MacDynamicTdma::data_time_ = 0;
double MacDynamicTdma::start_time_ = 0;



int MacDynamicTdma::tdma_ps_ = 0;
int MacDynamicTdma::tdma_pr_ = 0;

#endif

