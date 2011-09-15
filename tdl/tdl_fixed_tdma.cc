
#include "delay.h"
#include "connector.h"
#include "packet.h"
#include "random.h"
#include "arp.h"
#include "ll.h"
#include "mac.h"
#include "tdl_fixed_tdma.h"
#include "wireless-phy.h"
#include "cmu-trace.h"
#include "tdl_data_udp.h"
#include <stddef.h>
#include <iostream>
#include <sstream>
#include <fstream>

// Set Tx/Rx MacState
#define SET_RX_STATE(x)			\
{					\
	rx_state_ = (x);			\
}

#define SET_TX_STATE(x)				\
{						\
	tx_state_ = (x);				\
}

// Physical spec
static PHY_MIB PMIB = {
	Phy_SlotTime, Phy_GuardTime, Phy_RxTxTurnaround
};

/* Timers */

// timer to start sending packet up or down in a time slot
void MacFixTdmaTimer::start(Packet *p, double time)
{
	Scheduler &s = Scheduler::instance();
	assert(busy_ == 0);

	busy_ = 1;            //set flag for transmitting
	paused_ = 0;
	stime = s.clock();   //set start time of a time slot to current time
	rtime = time;        //set remaining time for the next time slot
	assert(rtime >= 0.0);

	s.schedule(this, p, rtime); //timer expires after rtime
}


// timer to stop sending packet and release time slot
void MacFixTdmaTimer::stop(Packet *p)
{
	Scheduler &s = Scheduler::instance();
	assert(busy_);   //check if transmitting

	if(paused_ == 0)
		s.cancel((Event *)p);   //stop timer

	// Should free the packet p.
	Packet::free(p);

	busy_ = 0;      //release slot
	paused_ = 0;
	stime = 0.0;    //reset start time of a time slot
	rtime = 0.0;    //reset ramaining time
}

/* Slot timer for TDMA scheduling. */
void SlotFixTdmaTimer::handle(Event *e)
{
	busy_ = 0;
	paused_ = 0;
	stime = 0.0;
	rtime = 0.0;

	mac->slotHandler(e);  //mac is MacFixTdma object
}

/* Receive Timer */
void RxPktFixTdmaTimer::handle(Event *e)
{
	busy_ = 0;
	paused_ = 0;
	stime = 0.0;
	rtime = 0.0;

	mac->recvHandler(e);
}

/* Send Timer */
void TxPktFixTdmaTimer::handle(Event *e)
{
	busy_ = 0;
	paused_ = 0;
	stime = 0.0;
	rtime = 0.0;

	mac->sendHandler(e);
}

void RecordFixTimer::expire(Event*)
{
  t_->recordHandler();
}
/* ======================================================================
   TCL Hooks for the simulator
   ====================================================================== */
static class MacFixTdmaClass : public TclClass {
public:
	MacFixTdmaClass() : TclClass("Mac/FixTdma") {}
	TclObject* create(int, const char*const*) {
		return (new MacFixTdma(&PMIB));
	}
} class_mac_fix_tdma;

static char nodeID = 0x41;
MacFixTdma::MacFixTdma(PHY_MIB* p) :
	Mac(), mhSlot_(this), mhTxPkt_(this), mhRxPkt_(this), recT_(this){
	/* Global variables setting. */
	// Setup the phy specs.
	phymib_ = p;
    node_ID_ = nodeID++;
	/* Get the parameters of the link (which in bound in mac.cc, 2M by default),
	   the packet length within one TDMA slot (300 byte by default),
	   and the max number of slots (200), and number of nodes (16) in the simulations.*/
	//bind("slot_packet_len_", &slot_packet_len_);
	bind("max_slot_num_", &max_slot_num_);
	bind("is_active_", &is_active_);



	// Calculate the slot time based on the MAX allowed data length exclude guard time.
	slot_time_ = Phy_SlotTime;
	data_time_ = Phy_SlotTime - Phy_GuardTime;


	/* Much simplified centralized scheduling algorithm for single hop
	   topology, like WLAN etc.
	*/
	// Initualize the tdma schedule and preamble data structure.
	tdma_schedule_ = new int[max_slot_num_];  //store time slot table with node id
	tdma_preamble_ = new int[max_slot_num_];  //store time slot table with indication of transmitting or not transmitting on that time slot
	for(int i=0;i<max_slot_num_;i++){
        tdma_schedule_[i] = -1;
        }



	// Initial channel / transceiver states.
	tx_state_ = rx_state_ = MAC_IDLE;
	tx_active_ = 0;

	// Initialy, the radio is off. NOTE: can't use radioSwitch(OFF) here.
	radio_active_ = 0;
        printf("mac created for %d with slot time %f\n",node_ID_,slot_time_);

	// Do slot scheduling.
	re_schedule();
    //printf("slot are allocated\n");
	/* Deal with preamble. */
	// Can't send anything in the first frame.
	slot_count_ = FIRST_ROUND;
	//tdma_preamble_[slot_num_] = NOTHING_TO_SEND;




	//Start the Slot timer..
	mhSlot_.start((Packet *) (& intr_), 0);
	//Start record
    if(is_active_) {
        record_time = 0;
        recordHandler();
	}
}

void MacFixTdma::recordHandler()
{
    //record momentary avg delay
    char out[100];
    sprintf(out, "recordAvgPacketDelay %i %i %f", node_ID_,record_time,avg_delay);
    Tcl& tcl = Tcl::instance();
    tcl.eval(out);

    //record individual throughput
    char out2[100];
    sprintf(out2, "recordThroughput %i %i %i %i %i", node_ID_,record_time,num_packets_sent,num_bytes_sent,num_tbytes_sent);
    tcl.eval(out2);


    //record individual time slot utilization
    char out3[100];
    sprintf(out3, "recordSlotUtil %i %i %i %i", node_ID_,record_time,num_slots_reserved,num_slots_used);
    tcl.eval(out3);

    record_time++;
    recT_.resched(1);
}

int MacFixTdma::command(int argc, const char*const* argv)
{
	if (argc == 3) {
		if (strcmp(argv[1], "log-target") == 0) {
			logtarget_ = (NsObject*) TclObject::lookup(argv[2]);
			if(logtarget_ == 0)
				return TCL_ERROR;
			return TCL_OK;
		}

	}
	return Mac::command(argc, argv);
}

/* ======================================================================
   Debugging Routines
   ====================================================================== */
void MacFixTdma::trace_pkt(Packet *p)
{
	struct hdr_cmn *ch = HDR_CMN(p);
	struct hdr_mac_fix_tdma* dh = HDR_MAC_FIX_TDMA(p);
	//u_int16_t *t = (u_int16_t*) &dh->dh_fc;

	fprintf(stderr, "\t[ %2x %2x %2x ] %x %s %d\n",
		dh->dh_duration,
		ETHER_ADDR(dh->dh_da), ETHER_ADDR(dh->dh_sa),
		index_, packet_info.name(ch->ptype()), ch->size());
}

void MacFixTdma::dump(char *fname)
{
	fprintf(stderr, "\n%s --- (INDEX: %d, time: %2.9f)\n", fname,
		index_, Scheduler::instance().clock());

	fprintf(stderr, "\ttx_state_: %x, rx_state_: %x, idle: %d\n",
		tx_state_, rx_state_, is_idle());
	fprintf(stderr, "\tpktTx_: %lx, pktRx_: %lx, callback: %lx\n",
		(long) pktTx_, (long) pktRx_, (long) callback_);
}

/* ======================================================================
   Packet Headers Routines
   ====================================================================== */
int MacFixTdma::hdr_dst(char* hdr, int dst )
{
	struct hdr_mac_fix_tdma *dh = (struct hdr_mac_fix_tdma*) hdr;
	if(dst > -2)
		STORE4BYTE(&dst, (dh->dh_da));
	return ETHER_ADDR(dh->dh_da);
}

int MacFixTdma::hdr_src(char* hdr, int src )
{
	struct hdr_mac_fix_tdma *dh = (struct hdr_mac_fix_tdma*) hdr;
	if(src > -2)
		STORE4BYTE(&src, (dh->dh_sa));

	return ETHER_ADDR(dh->dh_sa);
}

int MacFixTdma::hdr_type(char* hdr, u_int16_t type)
{
	struct hdr_mac_fix_tdma *dh = (struct hdr_mac_fix_tdma*) hdr;
	if(type)
		STORE2BYTE(&type,(dh->dh_body));
	return GET2BYTE(dh->dh_body);
}

/* Test if the channel is idle. */
int MacFixTdma::is_idle() {
	if(rx_state_ != MAC_IDLE)
		return 0;
	if(tx_state_ != MAC_IDLE)
		return 0;
	return 1;
}

/* Do the slot re-scheduling:

*/
void MacFixTdma::re_schedule() {

	//static int slot_pointer = 0;
	// Record the start time of the new schedule.

	/* Seperate slot_num_ and the node id:
	   we may have flexibility as node number changes.
	*/
//printf("read config file\n");

	ifstream file;
	char out[100];
	int i;
	int k;

	file.open("tdma_table");

	//int line = 0;

	while(!file.eof()) {

		file.getline(out, sizeof(out));
		//line++;
		//printf("file content in line %d: %d\n",line,(int)out[0]);
		if((int)out[0]!=0) {
			//printf("%s\n",out);
			istringstream ins;
			ins.str(out);
			ins>>k;

			//if(k==index_) {
				while(ins>>i) {
				    if(tdma_schedule_[i]<=0 || k==(int) node_ID_)
                        tdma_schedule_[i] = k;
					//printf("node %d has slots %d\n",k,i);
				}
			//}
		}
	}


	file.close();
	//for(int i=0;i<max_slot_num_;i++) {
	//	printf("table for %d slot %i reserved by %d\n",index_,i,tdma_schedule_[i]);
	//}
}

/* To handle incoming packet. */
void MacFixTdma::recv(Packet* p, Handler* h) {
	struct hdr_cmn *ch = HDR_CMN(p);
	//hdr_tdldata* tdlh = hdr_tdldata::access(p);
	//printf("MAC in %d receive packet %i\n",node_ID_,ch->uid());
	if(ch->direction() == hdr_cmn::DOWN) {
		//Node remain silence until the application start to send its first message
		//That is datalink radio is turned on first but neither tx or rx
		//until the tdl application starts, then it can tx or rx in the slot
		if(!radio_active_)
			radio_active_ = 1;
		printf("MAC in %d receive packet %i size %d from Upper Layer\n",node_ID_,ch->uid(),ch->size());
	}
	/* Incoming packets from phy layer, send UP to ll layer.
	   Now, it is in receiving mode.
	*/
	if (ch->direction() == hdr_cmn::UP) {
		// Since we can't really turn the radio off at lower level,
		// we just discard the packet.
		if (!radio_active_) {
			free(p);
			//printf("<%d>, %f, I am sleeping...\n", index_, NOW);
			return;
		}

		sendUp(p);
		//printf("<%d> packet recved: %d\n", index_, tdma_pr_++);
		return;
	}

	/* Packets coming down from ll layer (from ifq actually),
	   send them to phy layer.
	   Now, it is in transmitting mode. */
	if(ch->ptype() == PT_TDLDATA) {

	    packet_arr_time =  Scheduler::instance().clock();
        callback_ = h;
        state(MAC_SEND);
        sendDown(p);
	} else {
	    printf("receive control\n");
        free(p);
        h->handle((Event*) 0);
        return;
	}
	//printf("<%d> packet sent down: %d\n", index_, tdma_ps_++);
}

void MacFixTdma::sendUp(Packet* p)
{
	struct hdr_cmn *ch = HDR_CMN(p);

	/* Can't receive while transmitting. Should not happen...?*/
	if (tx_state_ && ch->error() == 0) {
		printf("<%d>, can't receive while transmitting!\n", index_);
		ch->error() = 1;
	};

	/* Detect if there is any collision happened. should not happen...?*/
	if (rx_state_ == MAC_IDLE) {
		SET_RX_STATE(MAC_RECV);     // Change the state to recv.
		pktRx_ = p;                 // Save the packet for timer reference.

		/* Schedule the reception of this packet,
		   since we just see the packet header. */
		double rtime = TX_Time(p);
		if(rtime > data_time_)
			rtime = data_time_;
		assert(rtime >= 0);

		/* Start the timer for receiving, will end when receiving finishes. */
		mhRxPkt_.start(p, rtime);
	} else {
		/* Note: we don't take the channel status into account,
		   as collision should not happen...
		*/
		printf("<%d>, receiving, but the channel is not idle....???\n", index_);
	}
}

/* Actually receive data packet when RxPktTimer times out. */
void MacFixTdma::recvDATA(Packet *p){
	/*Adjust the MAC packet size: strip off the mac header.*/
	struct hdr_cmn *ch = HDR_CMN(p);
	ch->size() -= ETHER_HDR_LEN;
	ch->num_forwards() += 1;

	/* Pass the packet up to the link-layer.*/
	uptarget_->recv(p, (Handler*) 0);
}

/* Send packet down to the physical layer.
   Need to calculate a certain time slot for transmission. */
void MacFixTdma::sendDown(Packet* p) {
	u_int32_t dst, src, size;

	struct hdr_cmn* ch = HDR_CMN(p);
	struct hdr_mac_fix_tdma* dh = HDR_MAC_FIX_TDMA(p);

	/* Update the MAC header */
	ch->size() += ETHER_HDR_LEN;



	if((u_int32_t)ETHER_ADDR(dh->dh_da) != MAC_BROADCAST)
		dh->dh_duration = DATA_DURATION;
	else
		dh->dh_duration = 0;

	dst = ETHER_ADDR(dh->dh_da);
	src = ETHER_ADDR(dh->dh_sa);
	size = ch->size();

	/* buffer the packet to be sent. in mac.h */

	pktTx_ = p;
}

/* Actually send the packet.
   Packet including header should fit in one slot time
*/
void MacFixTdma::send()
{
	u_int32_t dst, src, size;
	struct hdr_cmn* ch;
	struct hdr_mac_fix_tdma* dh;
	double stime;

	/* Check if there is any packet buffered. */
	if (!pktTx_) {
		printf("<%d>, %f, no packet buffered.\n", index_, NOW);
		return;
	}

	/* Perform carrier sence...should not be collision...? */
	if(!is_idle()) {
		/* Note: we don't take the channel status into account, ie. no collision,
		   as collision should not happen...
		*/
		printf("<%d>, %f, transmitting, but the channel is not idle...???\n", index_, NOW);
		return;
	}

	ch = HDR_CMN(pktTx_);
	dh = HDR_MAC_FIX_TDMA(pktTx_);

	dst = ETHER_ADDR(dh->dh_da);
	src = ETHER_ADDR(dh->dh_sa);
	size = ch->size();
	stime = TX_Time(pktTx_);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);

	/* Start a timer that expires when the packet transmission is complete. */
	printf("%d send packet %i size %d bytes in slot %d\n",node_ID_,ch->uid(),ch->size(),slot_count_);
	//record delay
	num_packets_sent++;
	num_bytes_sent += ch->size()-ETHER_HDR_LEN;
	num_tbytes_sent += ch->size();
    double send_packet_time = Scheduler::instance().clock();
    double pkt_delay = send_packet_time-packet_arr_time;
    cumulative_delay += pkt_delay;
    avg_delay = cumulative_delay/num_packets_sent;
    char out[100];
    sprintf(out, "recordPacketDelay %i %i %f", node_ID_,ch->uid(),send_packet_time-packet_arr_time);
    Tcl& tcl = Tcl::instance();
    tcl.eval(out);

    num_slots_used++;
	mhTxPkt_.start(pktTx_->copy(), stime);
	downtarget_->recv(pktTx_, this);

	pktTx_ = 0;
}

// Turn on / off the radio
void MacFixTdma::radioSwitch(int i)
{
	//radio_active_ = i;
	//EnergyModel *em = netif_->node()->energy_model();
	if (i == ON) {
		//if (em && em->sleep())
		//em->set_node_sleep(0);
		//    printf("<%d>, %f, turn radio ON\n", index_, NOW);
		Phy *p;
		p = netif_;
		((WirelessPhy *)p)->node_wakeup();
		return;
	}

	if (i == OFF) {
		//if (em && !em->sleep()) {
		//em->set_node_sleep(1);
		//    netif_->node()->set_node_state(INROUTE);
		Phy *p;
		p = netif_;
		((WirelessPhy *)p)->node_sleep();
		//    printf("<%d>, %f, turn radio OFF\n", index_, NOW);
		return;
	}
}

// make the new preamble.
void MacFixTdma::makePreamble()
{
	u_int32_t dst;
	struct hdr_mac_fix_tdma* dh;

	// If there is a packet buffered, file its destination to preamble.
	if (pktTx_) {
		dh = HDR_MAC_FIX_TDMA(pktTx_);
		dst = ETHER_ADDR(dh->dh_da);
		//printf("<%d>, %f, write %d to slot %d in preamble\n", index_, NOW, dst, slot_num_);
		tdma_preamble_[slot_num_] = dst;
	} else {
		//printf("<%d>, %f, write NO_PKT to slot %d in preamble\n", index_, NOW, slot_num_);
		tdma_preamble_[slot_num_] = NOTHING_TO_SEND;
	}
}

/* Timers' handlers */
/* Slot Timer:
   For the preamble calculation, we should have it:
   occupy one slot time,
   radio turned on for the whole slot.
*/
void MacFixTdma::slotHandler(Event *e)
{
	// Restart timer for next slot.
	mhSlot_.start((Packet *)e, slot_time_);

	// reset slot count for next frame.
	if ((slot_count_ == max_slot_num_) || (slot_count_ == FIRST_ROUND)) {
		//printf("<%d>, %f, make the new preamble now.\n", index_, NOW);
		// We should turn the radio on for the whole slot time.
		//radioSwitch(ON);

		//makePreamble();
		slot_count_ = 0;
		//return;
	}

	// If it is the sending slot for me.
	//if (slot_count_ == slot_num_) {

	if (tdma_schedule_[slot_count_] == (int)node_ID_) {
		//printf("slot %i reserved by %i\n",slot_count_,index_);
		//printf("<%d>, %f, time to send.\n", index_, NOW);
		// We have to check the preamble first to avoid the packets coming in the middle.
		//if (tdma_preamble_[slot_num_] != NOTHING_TO_SEND)
		num_slots_reserved++;
		if(radio_active_)
			send();
		//else
		//	radioSwitch(OFF);

		slot_count_++;
		return;
	} else {
		//else listen for other transmission
		slot_count_++;
		if(radio_active_)
			radioSwitch(ON);
		return;
	}

	// If I am supposed to listen in this slot
	//if ((tdma_preamble_[slot_count_] == index_) || ((u_int32_t)tdma_preamble_[slot_count_] == MAC_BROADCAST)) {
		//printf("<%d>, %f, preamble[%d]=%d, I am supposed to receive now.\n", index_, NOW, slot_count_, tdma_preamble_[slot_count_]);
		//slot_count_++;

		// Wake up the receive packets.
		//radioSwitch(ON);
		//return;
	//}

	// If I dont send / recv, do nothing.
	//printf("<%d>, %f, preamble[%d]=%d, nothing to do now.\n", index_, NOW, slot_count_, tdma_preamble_[slot_count_]);
	//radioSwitch(OFF);
	//slot_count_++;
	//return;
}

void MacFixTdma::recvHandler(Event *e)
{
	u_int32_t dst, src;
	int size;
	struct hdr_cmn *ch = HDR_CMN(pktRx_);
	struct hdr_mac_fix_tdma *dh = HDR_MAC_FIX_TDMA(pktRx_);

	/* Check if any collision happened while receiving. */
	if (rx_state_ == MAC_COLL)
		ch->error() = 1;

	SET_RX_STATE(MAC_IDLE);

	/* check if this packet was unicast and not intended for me, drop it.*/
	dst = ETHER_ADDR(dh->dh_da);
	src = ETHER_ADDR(dh->dh_sa);
	size = ch->size();

	//printf("<%d>, %f, recv a packet [from %d to %d], size = %d\n", index_, NOW, src, dst, size);

	// Turn the radio off after receiving the whole packet
	radioSwitch(OFF);

	/* Ordinary operations on the incoming packet */
	// Not a pcket destinated to me.
	if ((dst != MAC_BROADCAST) && (dst != (u_int32_t)index_)) {
		drop(pktRx_);
		return;
	}

	/* Now forward packet upwards. */
	recvDATA(pktRx_);
}

/* After transmission a certain packet. Turn off the radio. */
void MacFixTdma::sendHandler(Event *e)
{
	//  printf("<%d>, %f, send a packet finished.\n", index_, NOW);

	/* Once transmission is complete, drop the packet.
	   p  is just for schedule a event. */
	SET_TX_STATE(MAC_IDLE);
	Packet::free((Packet *)e);

	// Turn off the radio after sending the whole packet
	radioSwitch(OFF);

	/* unlock IFQ. */
	if(callback_) {
		Handler *h = callback_;
		callback_ = 0;
		h->handle((Event*) 0);
	}
}


