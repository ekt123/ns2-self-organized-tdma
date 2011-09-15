#ifndef ns_tdl_fixed_tdma_h
#define ns_tdl_fixed_tdma_h

#include "marshall.h"         // contain many getbytes, storebytes functions
#include <delay.h>	      // Link delay functionality
#include <connector.h>        // Base class for Mac
#include <packet.h>	      // Base class for header
#include <random.h>           // random number generetor
#include <arp.h>              // ARP functionality
#include <ll.h>		      // LL functionality
#include <mac.h>	      // Base class for this MAC protocol


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
#define MAC_TDMA_SLOT_NUM     200

// The mode for MacTdma layer's defer timers. */
#define SLOT_SCHE               0
#define SLOT_SEND               1
#define SLOT_RECV               2
#define SLOT_BCAST              3

// Indicate if there is a packet needed to be sent out.
#define NOTHING_TO_SEND         -2
// Indicate if this is the very first time the simulation runs.
#define FIRST_ROUND             -1

// Turn radio on /off
#define ON                       1
#define OFF                      0

#define DATA_DURATION 		5

// MAC frame header structure
struct frame_control {
	u_char		fc_type			: 1;  //Data or Control
	u_char		fc_protocol_version	: 2;



};

struct hdr_mac_fix_tdma {
	u_int16_t		dh_duration;
	u_char			dh_da[ETHER_ADDR_LEN];
	u_char			dh_sa[ETHER_ADDR_LEN];
	u_char			dh_body[1]; // XXX Non-ANSI
};

#define ETHER_HDR_LEN		9
#define DATA_Time(len)	(8 * (len) / bandwidth_)

/* Timers */
class MacFixTdma;

class MacFixTdmaTimer : public Handler {
public:
	MacFixTdmaTimer(MacFixTdma* m, double s = 0) : mac(m) {
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
	MacFixTdma 	*mac;
	int		busy_;
	int		paused_;
	Event		intr;
	double		stime;	// start time
	double		rtime;	// remaining time
	double		slottime_;
};

/* Timers to schedule transmitting and receiving. */
class SlotFixTdmaTimer : public MacFixTdmaTimer {
public:
	SlotFixTdmaTimer(MacFixTdma *m) : MacFixTdmaTimer(m) {}
	void 	handle(Event *e);
};

/* Timers to control packet sending and receiving time. */
class RxPktFixTdmaTimer : public MacFixTdmaTimer {
public:
	RxPktFixTdmaTimer(MacFixTdma *m) : MacFixTdmaTimer(m) {}

	void	handle(Event *e);
};

class TxPktFixTdmaTimer : public MacFixTdmaTimer {
public:
	TxPktFixTdmaTimer(MacFixTdma *m) : MacFixTdmaTimer(m) {}

	void	handle(Event *e);
};

class RecordFixTimer : public TimerHandler {
 public:
	RecordFixTimer(MacFixTdma* t) : TimerHandler(), t_(t) {}
	inline virtual void expire(Event*);
 protected:
	MacFixTdma* t_;
};
// Fix TDMA MAC Layer
class MacFixTdma : public Mac {
	friend class SlotFixTdmaTimer;
	friend class RxPktFixTdmaTimer;
	friend class TxPktFixTdmaTimer;

public:
	MacFixTdma(PHY_MIB* p);
	void		recv(Packet *p, Handler *h);
	inline int 	hdr_dst(char* hdr, int dst = -2);
	inline int	hdr_src(char* hdr, int src = -2);
	inline int	hdr_type(char* hdr, u_int16_t type = 0);

	/* Timer handler */
	void slotHandler(Event *e);
	void recvHandler(Event *e);
	void sendHandler(Event *e);
	void recordHandler();


protected:
	PHY_MIB		*phymib_;

	// Both the slot length and max slot num can be configured
	//int 		slot_packet_len_;
	int		max_node_num_;
	char    node_ID_;


private:
	int command(int argc, const char*const* argv);
	// Do slot scheduling for the active nodes within one cluster.
	  void re_schedule();
	  void makePreamble();	//no preamble
	  void radioSwitch(int i);	//radio always on

	  /* Packet Transmission Functions.*/
	  void    sendUp(Packet* p);
	  void    sendDown(Packet* p);

	  /* Actually receive data packet when rxTimer times out. */
	  void recvDATA(Packet *p);
	  /* Actually send the packet buffered. */
	  void send();

	  inline int	is_idle(void);

	  /* Debugging Functions.*/
	  void		trace_pkt(Packet *p);
	  void		dump(char* fname);

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
	  SlotFixTdmaTimer mhSlot_;
	  TxPktFixTdmaTimer mhTxPkt_;
	  RxPktFixTdmaTimer mhRxPkt_;
	  RecordFixTimer recT_;

	  /* Internal MAC state from mac.h */
	  MacState	rx_state_;	// incoming state (MAC_RECV or MAC_IDLE)
	  MacState	tx_state_;	// outgoing state

	  /* The indicator of the radio. */
	  int radio_active_;

	  int		tx_active_;	// transmitter is ACTIVE

	  NsObject*	logtarget_;

	  /* TDMA scheduling state.
	   */
	  // The max num of slot within one frame.
	  static int max_slot_num_;

	  // The time duration for each slot.
	  static double slot_time_;
	  // Duration for data in each slot
	  static double data_time_;
	  /* The start time for whole TDMA scheduling.
	  	All net should have the same start time
	  */
	  static double start_time_;

	  /* Data structure for tdma scheduling. */
	  //static int active_node_;            // How many nodes needs to be scheduled

	  int *tdma_schedule_;
	  int slot_num_;                      // The slot number it's allocated.

	  static int *tdma_preamble_;        // The preamble data structure. Not used

	  // When slot_count_ = active_nodes_, it indicates a new cycle.
	  int slot_count_;

	  // How many packets has been sent out?
	  static int tdma_ps_;
	  static int tdma_pr_;

      //delay measurement
      double packet_arr_time;
      double cumulative_delay;
      int num_packets_sent;
      double avg_delay;
      //throughput measurement
      int num_bytes_sent;
      int num_tbytes_sent;
      //time slot utilization
      int num_slots_reserved;
      int num_slots_used;
      //schedule record
      int record_time;

      int is_active_;
};

double MacFixTdma::slot_time_ = 0;
double MacFixTdma::data_time_ = 0;
double MacFixTdma::start_time_ = 0;
//int MacFixTdma::active_node_ = 0;
int MacFixTdma::max_slot_num_ = 0;
//int *MacFixTdma::tdma_schedule_ = NULL;
int *MacFixTdma::tdma_preamble_ = NULL;

int MacFixTdma::tdma_ps_ = 0;
int MacFixTdma::tdma_pr_ = 0;

#endif

