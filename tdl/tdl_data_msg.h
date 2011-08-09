/*
tdl_data_msg.h
Note: header file for tdl_data_msg class
Usage: generator of TDL messages
*/
#ifndef ns_tdl_data_message_h
#define ns_tdl_data_message_h

#include "timer-handler.h"
#include "app.h"
#include "packet.h"
#include "tdl_data_udp.h"


class TdlDataApp;

struct recv_pkt_info {
	int last_seq;
        int lost_pkts;      // number of lost pkts
        int recv_pkts;      // number of received pkts
        double delay;       // propagation delay
};

// Sender uses this timer to
// schedule next app data packet transmission time
class SendTimer : public TimerHandler {
 public:
	SendTimer(TdlDataApp* t) : TimerHandler(), t_(t) {}
	inline virtual void expire(Event*);
 protected:
	TdlDataApp* t_;
};

//Class Definition
class TdlDataApp : public Application {
 public:
	TdlDataApp();
	void send_tdldata_msg();    // called by SendTimer:expire (Sender)
	void send_msgupdate_msg();  // called when receive message update command
	void send_netupdate_msg();  // called when receive net ID changing command. Not used in this simulation
 protected:
 	int command(int argc, const char*const* argv);
	void start();       // Start sending data packets (Sender)
	void stop();        // Stop sending data packets (Sender)
	int appIdx_;        // application ID
 private:
 	void init();
 	virtual void recv_msg(int nbytes, const char *msg = 0);
 	void account_recv_pkt_info(const hdr_tdldata *tdlh);
 	void init_recv_pkt_info();

 	double interval_;       // Application data packet transmission interval
 	int msgType_;           // TDL message type
 	int pktsize_;           // message size
 	int netID_;             // net ID
	int seq_;	            // message sequence number
	int running_;           // If 1 application is running
	recv_pkt_info recv_p_info;
	SendTimer snd_timer_;  // SendTimer
};

#endif
