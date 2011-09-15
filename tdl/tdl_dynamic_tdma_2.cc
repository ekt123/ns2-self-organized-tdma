/*
tdl_dynamic_tdma.cc
Note: constructor and functions for tdl_dynamic_tdma class
Usage: self-organized TDMA protocol
*/

#include "delay.h"
#include "connector.h"
#include "packet.h"
#include "random.h"
#include "ip.h"
#include "arp.h"
#include "ll.h"
#include "mac.h"
#include "tdl_dynamic_tdma_2.h"
#include "wireless-phy.h"
#include "cmu-trace.h"
#include "tclcl.h"
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
int hdr_nb_info::offset_;
static class HdrNbInfoClass : public PacketHeaderClass {
public:
	HdrNbInfoClass() : PacketHeaderClass("PacketHeader/HdrNbInfo",
						    sizeof(hdr_nb_info)) {
		bind_offset(&hdr_nb_info::offset_);
	}
} class_hdrnbinfo;

// Control message Class
int control_msg::offset_;
static class ControlMsgClass : public PacketHeaderClass {
public:
	ControlMsgClass() : PacketHeaderClass("PacketHeader/ControlMsg",
						    sizeof(control_msg)) {
		bind_offset(&control_msg::offset_);
	}
} class_controlmsg;


// Net entry message Class
int net_entry_msg::offset_;
static class NetEntryMsgClass : public PacketHeaderClass {
public:
	NetEntryMsgClass() : PacketHeaderClass("PacketHeader/NetEntryMsg",
						    sizeof(net_entry_msg)) {
		bind_offset(&net_entry_msg::offset_);
	}
} class_netentrymsg;


// Polling message Class
int polling_msg::offset_;
static class PollingMsgClass : public PacketHeaderClass {
public:
	PollingMsgClass() : PacketHeaderClass("PacketHeader/PollingMsg",
						    sizeof(polling_msg)) {
		bind_offset(&polling_msg::offset_);
	}
} class_pollingmsg;

/* Timers */

// timer to start sending packet up or down in a time slot
void MacDynamicTdmaTimer::start(Packet *p, double time)
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
void MacDynamicTdmaTimer::stop(Packet *p)
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
void SlotDynamicTdmaTimer::handle(Event *e)
{
	busy_ = 0;
	paused_ = 0;
	stime = 0.0;
	rtime = 0.0;

	mac->slotHandler(e);  //mac is MacFixTdma object
}

/* Receive Timer */
void RxPktDynamicTdmaTimer::handle(Event *e)
{
	busy_ = 0;
	paused_ = 0;
	stime = 0.0;
	rtime = 0.0;

	mac->recvHandler(e);
}

/* Send Timer */
void TxPktDynamicTdmaTimer::handle(Event *e)
{
	busy_ = 0;
	paused_ = 0;
	stime = 0.0;
	rtime = 0.0;

	mac->sendHandler(e);
}

/* Back Off Timer */
void BackOffTimer::handle(Event *e)
{

    busy_ = 0;
    paused_ = 0;
    stime = 0.0;
    rtime = 0.0;

    mac->backoffHandler(e);
}

void RecordDynTimer::expire(Event*)
{
  t_->recordHandler();
}
/* ======================================================================
   TCL Hooks for the simulator
   ====================================================================== */
static class MacDynamicTdmaClass : public TclClass {
public:
	MacDynamicTdmaClass() : TclClass("Mac/DynamicTdma") {}
	TclObject* create(int, const char*const*) {
		return (new MacDynamicTdma(&PMIB));
	}
} class_mac_dynamic_tdma;

static char nodeID = 0x41;
static int ctrlPktCnt = 0;
static int nodeInNetCnt = 0;
static int activeNodes = 0;

MacDynamicTdma::MacDynamicTdma(PHY_MIB* p) :
	Mac(), ctrlPkt_(0), nePkt_(0), mhSlot_(this), mhTxPkt_(this), mhRxPkt_(this), mhBkOff_(this), recT_(this) {
	/* Global variables setting. */
	// Assign Node ID
	node_ID_ = nodeID++;
	activeNodes++;
	node_seed_ = assignSeed();

	// Setup the phy specs.
	phymib_ = p;

	/* Get the parameters of the link (which in bound in mac.cc, 2M by default),
	   and the max number of slots (400).*/
	bind("max_slot_num_", &max_slot_num_);
	bind("assigned_Net_",&assigned_Net_);
    bind("is_active_",&is_active_);



	// Calculate the slot time based on the MAX allowed data length exclude guard time on physical layer.
	slot_time_ = Phy_SlotTime;
	data_time_ = Phy_SlotTime - Phy_GuardTime;


	/* Much simplified centralized scheduling algorithm for single hop
	   topology, like WLAN etc.
	*/
	// Initualize the tdma schedule and preamble data structure.
	tdma_schedule_ = new int[max_slot_num_];  //store time slot table with node id
	table_nb_id = new char[MAX_NODE_NUM-1];
	table_nb_msg_type = new MsgType[MAX_NODE_NUM-1];
	table_nb_seed = new u_int8_t[MAX_NODE_NUM-1];
	table_nb_hops = new u_int8_t[MAX_NODE_NUM-1];

	// flag slots for different type
	// -1 is free data slot
	// -2 is control slot
	// -3 is net control slot

	// assign all slot to be free data slot
	for(int i=0;i<max_slot_num_;i++){
        tdma_schedule_[i] = -1;
    }


    // assign control slots
    for(int i=0;i<max_slot_num_;i=i+20){
        tdma_schedule_[i] = -2;
    }

    //initialize neighbor table
    for(int i=0;i<MAX_NODE_NUM-1;i++){
        table_nb_id[i] = 0x00;          // initially no neighbor in table
        table_nb_msg_type[i] = MSG_0;
        table_nb_hops[i] = 3;           // initially all neighbor are over 2 hops
    }



    // Assign ID to VSLOTs
    char init_vslot_ID = 0x61;
    for(int i=0;i<NUM_VSLOTS;i++) {
        vslotIDs[i] = init_vslot_ID++;
        vslots[i] = (assigned_Net_*i+(int) vslotIDs[i]) % 256;
    }



	// Initial channel / transceiver states.
	tx_state_ = rx_state_ = MAC_IDLE;
	tx_active_ = 0;

	// Initially deactivate radio
	radio_active_ = 0;
    // A node does not start application at the beginning
    is_app_start = 0;
    node_msg_type_ = MSG_0;
    node_msg_size_ = 0;
    is_control_msg_required = 0;
    // Initialy a node is not in any net
    is_in_net = 0;
    net_ID_ = assigned_Net_;
    net_freq_ = BASE_FREQ + (net_ID_-1)*CHANNEL_SPACING;
    // Initially assume it is first node
    is_first_node = 1;
    // Initially not in net entry phase
    is_net_entry = 0;
    is_ack_waiting = 0;
    //is_ne_slot_selected = 0;

	slot_count_ = FIRST_ROUND;


	//Start the Slot timer..
	mhSlot_.start((Packet *) (& intr_), 0);

	//Start record
	if(is_active_) {
        record_time = 0;
        recordHandler();
	}
}

void MacDynamicTdma::recordHandler()
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

u_int8_t MacDynamicTdma::assignSeed()
{
    // Assign seed
	u_int8_t val = (rand() % 256);
	return (INIT_SEED + val);
}
int MacDynamicTdma::command(int argc, const char*const* argv)
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
   Packet Headers Routines
   ====================================================================== */
int MacDynamicTdma::hdr_dst(char* hdr, int dst )
{
	struct hdr_mac_dynamic_tdma *dh = (struct hdr_mac_dynamic_tdma*) hdr;

	if(dst > -2)
		STORE4BYTE(&dst, (dh->dh_da));
	return ETHER_ADDR(dh->dh_da);
}

int MacDynamicTdma::hdr_src(char* hdr, int src )
{
	struct hdr_mac_dynamic_tdma *dh = (struct hdr_mac_dynamic_tdma*) hdr;

	if(src > -2)
		STORE4BYTE(&src, (dh->dh_sa));

	return ETHER_ADDR(dh->dh_sa);
}

int MacDynamicTdma::hdr_type(char* hdr, u_int16_t type)
{
	struct hdr_mac_dynamic_tdma *dh = (struct hdr_mac_dynamic_tdma*) hdr;
	if(type)
		STORE2BYTE(&type,(dh->dh_body));
	return GET2BYTE(dh->dh_body);
}

/* Test if the channel is idle. */
int MacDynamicTdma::is_idle() {
	if(rx_state_ != MAC_IDLE)
		return 0;
	if(tx_state_ != MAC_IDLE)
		return 0;
	return 1;
}

/* To handle incoming packet. */
void MacDynamicTdma::recv(Packet* p, Handler* h) {

	struct hdr_cmn *ch = HDR_CMN(p);

	/* Incoming packets from phy layer, send UP to ll layer.
	   Now, it is in receiving mode.
	*/
    if (ch->direction() == hdr_cmn::UP) {
		// Since we can't really turn the radio off at lower level,
		// we just discard the packet.
        if (!radio_active_) {
		    printf("node %i receive incoming packet with radio off in slot %i. Discard packet\n",node_ID_,slot_count_);
			free(p);
			return;
		}

        sendUp(p);
        return;
	}

    //Node remain silence until the application start to send its first message
    //That is datalink radio is turned on first but neither tx or rx
    //until the tdl application starts, then it can tx or rx in the slot
    if(ch->ptype() == PT_TDLDATA) {
    packet_arr_time =  Scheduler::instance().clock();
    //printf("MAC in %i receive packet %i size %d from Upper Layer at time %f\n",node_ID_,ch->uid(),ch->size(),Scheduler::instance().clock());
    char out[100];
    sprintf(out, "recordPktArrTime %i %f", ch->uid(),Scheduler::instance().clock());
    Tcl& tcl = Tcl::instance();
    tcl.eval(out);
    // if it is first packet
    if(!is_app_start) {
        is_app_start = 1;
        first_pkt_atime = Scheduler::instance().clock();
    }

    struct hdr_tdldata *tdlh = hdr_tdldata::access(p);
    node_msg_type_ = (MsgType) tdlh->type;
    node_msg_size_ = tdlh->nbytes;
    /* Packets coming down from ll layer (from ifq actually),
    send them to phy layer.
    Now, it is in transmitting mode. */
    callback_ = h;
    state(MAC_SEND);

    sendDown(p);
    return;
    }
    if(ch->ptype() == PT_TDLMSGUPDATE) {
        //printf("MAC in %i receive message update packet %i from Upper Layer at time %f\n",node_ID_,ch->uid(),Scheduler::instance().clock());
        struct hdr_tdlmsgupdate *tdlh = hdr_tdlmsgupdate::access(p);
        node_msg_type_ = (MsgType) tdlh->newtype;
        node_msg_size_ = tdlh->newbytes;
        if(is_in_net) {
            // control message is required to be sent before sending any data
            printf("****** require control message\n");
            is_control_msg_required = 1;
            update_msg_arr_time = Scheduler::instance().clock();
        }
        h->handle((Event*) 0);
        return;
    }

    if(ch->ptype() == PT_TDLNETUPDATE) {
        //printf("MAC in %i receive net update packet %i from Upper Layer at time %f\n",node_ID_,ch->uid(),Scheduler::instance().clock());
        struct hdr_tdlnetupdate *tdlh = hdr_tdlnetupdate::access(p);
        assigned_Net_ = tdlh->newnet;
        if(is_in_net) {
            // control message is required to be sent before sending any data
            //printf("****** require net entry to new net\n");
            is_in_net = 0;
        }
        h->handle((Event*) 0);
        return;
    }

}

void MacDynamicTdma::sendUp(Packet* p)
{
	struct hdr_cmn *ch = HDR_CMN(p);

    //struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(p);
	/* Can't receive while transmitting. Should not happen...?*/
	if (tx_state_ && ch->error() == 0) {
		printf("Node %i can't receive while transmitting!\n", node_ID_);
		ch->error() = 1;
		if(ch->ptype() == PT_TDLDATA) {
            is_conflict_in_frame = 1;
            num_conflicts++;
		}
		//if detect other net entry while transmitting its own net entry, reset net entry phase
		if(ch->ptype() == PT_TDLNETENT && !is_in_net) {
            is_net_entry = 0;
            is_ack_waiting = 0;
            waiting_ack_ctslot_count = 0;
		}
	};

	/* Detect if there is any collision happened. should not happen...?*/
	if (rx_state_ == MAC_IDLE) {
		SET_RX_STATE(MAC_RECV);     // Change the state to recv.

	} else {
		/* Note: we don't take the channel status into account,
		   as collision should not happen...
		*/
		SET_RX_STATE(MAC_COLL);
		Phy *ph;
		ph = netif_;
		printf("Node %i, receiving packet %i, but the channel in %f in slot %i is not idle....???\n", node_ID_,ch->uid(),((WirelessPhy *)ph)->getFreq(), slot_count_);
		if(ch->ptype() == PT_TDLDATA) {
            is_conflict_in_frame = 1;
            num_conflicts++;
		}
	}

        pktRx_ = p;                 // Save the packet for timer reference.

		/* Schedule the reception of this packet,
		   since we just see the packet header. */
		double rtime = TX_Time(p);
		if(rtime > data_time_)
			rtime = data_time_;
		assert(rtime >= 0);

		/* Start the timer for receiving, will end when receiving finishes. */
		mhRxPkt_.start(p, rtime);
}

/* Actually receive data packet when RxPktTimer times out. */
void MacDynamicTdma::recvDATA(Packet *p){
	/*Adjust the MAC packet size: strip off the mac header.*/
	struct hdr_cmn *ch = HDR_CMN(p);
	ch->size() -= DYNAMIC_MAC_HDR_LEN;
    ch->num_forwards() += 1;
	/* Pass the packet up to the link-layer.*/
	uptarget_->recv(p, (Handler*) 0);

}



/* Send packet down to the physical layer.
   Need to calculate a certain time slot for transmission. */
void MacDynamicTdma::sendDown(Packet* p) {

    /* buffer the packet to be sent. in mac.h */
    struct hdr_cmn *ch = HDR_CMN(p);
    if(pktTx_) {
        struct hdr_cmn *ch2 = HDR_CMN(pktTx_);
        printf("Packet %i is arriving at MAC and packet %i is dropped\n",ch->uid(),ch2->uid());
    }

	pktTx_ = p;

}

/* Actually send the packet.
   Packet including header should fit in one slot time
*/
void MacDynamicTdma::send()
{
	u_int32_t size;
	struct hdr_cmn* ch;
	struct hdr_mac_dynamic_tdma* mh;
	struct hdr_nb_info* nb;
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


	/* Update the MAC header */
	ch = HDR_CMN(pktTx_);
	mh = HDR_MAC_DYNAMIC_TDMA(pktTx_);
	nb = hdr_nb_info::access(pktTx_);
    int pId;

	size = ch->size();
	pId = ch->uid();

    mh->frame_type = DATA_FRAME;
    mh->srcID = node_ID_;
    mh->srcMsgType = node_msg_type_;
    mh->srcSeed = node_seed_;
    nb->nrNB = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]==1) {
            nb->nbID[i] = table_nb_id[i];
            nb->nbMsgType[i] = table_nb_msg_type[i];
            nb->nbSeed[i] = table_nb_seed[i];
            nb->nrNB++;
            int nr_hops = 2;
            if(checkOneHop(table_nb_id[i])) {
                nr_hops = 1;
            } else {
                recordTwoHop(table_nb_id[i]);
                table_nb_hops[i] = nr_hops;
            }
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }


    ch->size() = size + DYNAMIC_MAC_HDR_LEN;
	stime = TX_Time(pktTx_);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);

	/* Start a timer that expires when the packet transmission is complete. */
    Phy *ph;
    ph = netif_;
	printf("Node %i send packet %i size %d bytes in slot %d with freqency %f at time %f\n",node_ID_,ch->uid(),ch->size(),slot_count_,((WirelessPhy *)ph)->getFreq(),Scheduler::instance().clock());
	//record delay
	num_packets_sent++;
	num_bytes_sent += ch->size()-DYNAMIC_MAC_HDR_LEN;
	num_tbytes_sent += ch->size();
    double send_packet_time = Scheduler::instance().clock();
    double pkt_delay = send_packet_time-packet_arr_time;
    cumulative_delay += pkt_delay;
    avg_delay = cumulative_delay/num_packets_sent;
    char out[100];
    sprintf(out, "recordPacketDelay %i %i %f", node_ID_,ch->uid(),pkt_delay);
    Tcl& tcl = Tcl::instance();
    tcl.eval(out);

    num_slots_used++;
	mhTxPkt_.start(pktTx_->copy(), stime);
	downtarget_->recv(pktTx_, this);
    is_seed_sent = 1;
	pktTx_ = 0;



}

// Turn on / off the radio
void MacDynamicTdma::radioSwitch(int i)
{
	radio_active_ = i;
	if (i == ON) {
		Phy *p;
		p = netif_;
		((WirelessPhy *)p)->node_wakeup();
		return;
	}

	if (i == OFF) {
		Phy *p;
		p = netif_;
		((WirelessPhy *)p)->node_sleep();
		return;
	}
}

void MacDynamicTdma::switchToNetChannel() {
	Phy *p;
	p = netif_;
	((WirelessPhy *)p)->setFreq(net_freq_);
	radioSwitch(ON);	//Turn on radio
    return;
}




void MacDynamicTdma::sendNetEntry() {
    Packet* p;
    double stime;
    p = Packet::alloc();

    net_entry_msg::access(p)->entry_msg = 1;
    net_entry_msg::access(p)->entry_ID = node_ID_;
    hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
    hdr_src((char*)mh, addr());
	hdr_type((char*)mh, ETHERTYPE_IP);
    hdr_dst((char*)mh, MAC_BROADCAST);

    mh->frame_type = NETENTRY_FRAME;
    mh->srcID = node_ID_;
    mh->srcMsgType = node_msg_type_;
    mh->srcSeed = node_seed_;
    hdr_nb_info* nb = hdr_nb_info::access(p);
    nb->nrNB = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]==1) {
            nb->nbID[i] = table_nb_id[i];
            nb->nbMsgType[i] = table_nb_msg_type[i];
            nb->nbSeed[i] = table_nb_seed[i];
            nb->nrNB++;
            int nr_hops = 2;
            if(checkOneHop(table_nb_id[i])) {
                nr_hops = 1;
            } else {
                recordTwoHop(table_nb_id[i]);
                table_nb_hops[i] = nr_hops;
            }
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = ctrlPktCnt++;
    ch->ptype() = PT_TDLNETENT;
	ch->timestamp() = Scheduler::instance().clock();
	ch->iface() = UNKN_IFACE.value();
	ch->size() = NET_ENTRY_PAYLOAD_SIZE + DYNAMIC_MAC_HDR_LEN;


    stime = TX_Time(p);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);
    printf("***Node %i send net entry packet(%i) %i with size %i bytes in slot %i at time %f\n",node_ID_,ch->ptype(),ch->uid(),ch->size(),slot_count_,ch->timestamp());


	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);

}

void MacDynamicTdma::sendNetEntryACK() {
    Packet* p;
    double stime;
    p = Packet::alloc();
    struct net_entry_msg *ne = net_entry_msg::access(nePkt_);
    net_entry_msg::access(p)->entry_msg = 2;
    net_entry_msg::access(p)->entry_ID = ne->entry_ID;
    struct hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
    hdr_src((char*)mh, addr());
	hdr_type((char*)mh, ETHERTYPE_IP);
    hdr_dst((char*)mh, MAC_BROADCAST);

    mh->frame_type = NETENTRY_FRAME;
    mh->srcID = node_ID_;
    mh->srcMsgType = node_msg_type_;
    mh->srcSeed = node_seed_;
    hdr_nb_info* nb = hdr_nb_info::access(p);
    nb->nrNB = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]==1) {
            nb->nbID[i] = table_nb_id[i];
            nb->nbMsgType[i] = table_nb_msg_type[i];
            nb->nbSeed[i] = table_nb_seed[i];
            nb->nrNB++;
            int nr_hops = 2;
            if(checkOneHop(table_nb_id[i])) {
                nr_hops = 1;
            } else {
                recordTwoHop(table_nb_id[i]);
                table_nb_hops[i] = nr_hops;
            }
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = ctrlPktCnt++;
    ch->ptype() = PT_TDLNETENT;
	ch->timestamp() = Scheduler::instance().clock();
	ch->iface() = UNKN_IFACE.value();
	ch->size() = NET_ENTRY_PAYLOAD_SIZE + DYNAMIC_MAC_HDR_LEN;


    stime = TX_Time(p);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);




	//printf("***Node %i include %i neighbors in ACK message",node_ID_,nb->nrNB);
    printf("Node %i send net entry ACK packet %i size %d bytes in slot %d at time %f\n",node_ID_,ch->uid(),ch->size(),slot_count_,Scheduler::instance().clock());

	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);

    is_seed_sent = 1;
	nePkt_ = 0;

	return;
}

void MacDynamicTdma::sendControl() {
    Packet* p;
    double stime;
    p = Packet::alloc();

    struct hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
    struct control_msg* cm = control_msg::access(p);
    hdr_src((char*)mh, addr());
	hdr_type((char*)mh, ETHERTYPE_IP);
    hdr_dst((char*)mh, MAC_BROADCAST);

    cm->control_ID = node_ID_;
    cm->control_msgtype = 1;

    mh->frame_type = CONTROL_FRAME;
    mh->srcID = node_ID_;
    mh->srcMsgType = node_msg_type_;
    mh->srcSeed = node_seed_;
    hdr_nb_info* nb = hdr_nb_info::access(p);
    nb->nrNB = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]==1) {
            nb->nbID[i] = table_nb_id[i];
            nb->nbMsgType[i] = table_nb_msg_type[i];
            nb->nbSeed[i] = table_nb_seed[i];
            nb->nrNB++;
            int nr_hops = 2;
            if(checkOneHop(table_nb_id[i])) {
                nr_hops = 1;
            } else {
                recordTwoHop(table_nb_id[i]);
                table_nb_hops[i] = nr_hops;
            }
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = ctrlPktCnt++;
    ch->ptype() = PT_TDLCONTROL;
	ch->timestamp() = Scheduler::instance().clock();
	ch->iface() = UNKN_IFACE.value();
	ch->size() = DYNAMIC_MAC_HDR_LEN;   // no payload


    stime = TX_Time(p);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);




	printf("Node %i send update control packet %i size %d bytes in slot %d at time %f\n",node_ID_,ch->uid(),ch->size(),slot_count_,Scheduler::instance().clock());

	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);
    is_seed_sent = 1;
    is_cack_waiting = 1;
	return;
}

void MacDynamicTdma::sendControlACK() {
    Packet* p;
    double stime;
    p = Packet::alloc();

    struct hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
    struct control_msg* cm = control_msg::access(p);
    struct control_msg* cm2 = control_msg::access(ctrlPkt_);
    hdr_src((char*)mh, addr());
	hdr_type((char*)mh, ETHERTYPE_IP);
    hdr_dst((char*)mh, MAC_BROADCAST);

    cm->control_ID = cm2->control_ID;
    cm->control_msgtype = 2;

    mh->frame_type = CONTROL_FRAME;
    mh->srcID = node_ID_;
    mh->srcMsgType = node_msg_type_;
    mh->srcSeed = node_seed_;
    hdr_nb_info* nb = hdr_nb_info::access(p);
    nb->nrNB = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]==1) {
            nb->nbID[i] = table_nb_id[i];
            nb->nbMsgType[i] = table_nb_msg_type[i];
            nb->nbSeed[i] = table_nb_seed[i];
            nb->nrNB++;
            int nr_hops = 2;
            if(checkOneHop(table_nb_id[i])) {
                nr_hops = 1;
            } else {
                recordTwoHop(table_nb_id[i]);
                table_nb_hops[i] = nr_hops;
            }
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = ctrlPktCnt++;
    ch->ptype() = PT_TDLCONTROL;
	ch->timestamp() = Scheduler::instance().clock();
	ch->iface() = UNKN_IFACE.value();
	ch->size() = DYNAMIC_MAC_HDR_LEN;   // no payload


    stime = TX_Time(p);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);




	printf("Node %i send update control packet ACK %i for node %i size %d bytes in slot %d at time %f\n",node_ID_,ch->uid(),cm2->control_ID,ch->size(),slot_count_,Scheduler::instance().clock());

	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);
    is_seed_sent = 1;
    ctrlPkt_ = 0;
	return;
}



void MacDynamicTdma::sendPolling() {
    Packet* p;
    double stime;
    p = Packet::alloc();
    struct polling_msg* ph = polling_msg::access(p);

    char firstRunner = 0x00;

    char secondRunner = 0x00;

    char thirdRunner = 0x00;


    //reset poll list
    for(int i=0;i<POLL_SIZE;i++) {
        poll_list_[i] = 0x00;
    }
    firstRunner = findRunnerUpNode(1);
    poll_list_[0] = firstRunner;
    secondRunner = findRunnerUpNode(2);
    poll_list_[1] = secondRunner;
    thirdRunner = findRunnerUpNode(3);
    poll_list_[2] = thirdRunner;
    ph->runner_ups[0] = firstRunner;
    ph->runner_ups[1] = secondRunner;
    ph->runner_ups[2] = thirdRunner;


    struct hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
    hdr_src((char*)mh, addr());
	hdr_type((char*)mh, ETHERTYPE_IP);
    hdr_dst((char*)mh, MAC_BROADCAST);

    mh->frame_type = POLLING_FRAME;
    mh->srcID = node_ID_;
    mh->srcMsgType = node_msg_type_;
    mh->srcSeed = node_seed_;
    hdr_nb_info* nb = hdr_nb_info::access(p);
    nb->nrNB = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]==1) {
            nb->nbID[i] = table_nb_id[i];
            nb->nbMsgType[i] = table_nb_msg_type[i];
            nb->nbSeed[i] = table_nb_seed[i];
            nb->nrNB++;
            int nr_hops = 2;
            if(checkOneHop(table_nb_id[i])) {
                nr_hops = 1;
            } else {
                recordTwoHop(table_nb_id[i]);
                table_nb_hops[i] = nr_hops;
            }
            //printf("attach nb %i\n",table_nb_id[i]);
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = ctrlPktCnt++;
    ch->ptype() = PT_TDLPOLL;
	ch->timestamp() = Scheduler::instance().clock();
	ch->iface() = UNKN_IFACE.value();
	ch->size() = DYNAMIC_MAC_HDR_LEN + POLLING_PAYLOAD_SIZE;


    stime = TX_Time(p);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);




	printf("Node %i send polling packet %i (%i,%i,%i) size %d bytes in slot %d at time %f\n",node_ID_,ch->uid(),ph->runner_ups[0],ph->runner_ups[1],ph->runner_ups[2],ch->size(),slot_count_,Scheduler::instance().clock());

	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);
    is_seed_sent = 1;

	return;
}

int MacDynamicTdma::checkPollList(Packet *p) {
    struct polling_msg *ph = polling_msg::access(p);
    char firstRunner = ph->runner_ups[0];
    char secondRunner = ph->runner_ups[1];
    char thirdRunner = ph->runner_ups[2];

    if(node_ID_==firstRunner)
        return 1;
    else if(node_ID_==secondRunner)
        return 2;
    else if(node_ID_==thirdRunner)
        return 3;
    else
        return 0;
}
void MacDynamicTdma::findHashAndSort(int slot_num, int vslot_num) {
    int nodeHash;
    // reset hash table
    for(int i=0;i<MAX_NODE_NUM+NUM_VSLOTS;i++) {
        hashID[i] = 0x00;
        hashValue[i] = 0;
    }
    if(is_seed_sent)
        nodeHash = hashSeed(slot_num,node_seed_);
    else
        nodeHash = hashSeed(slot_num,node_last_seed_);

    int idx = 0;
    if(is_in_net) {
        hashID[idx] = node_ID_;
        hashValue[idx] = nodeHash;
    }

    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]<3) {
            idx++;
            //if(is_seed_sent)
            //    printf("****************node %i with new seed %i found nb %i with seed %i\n",node_ID_,node_seed_,table_nb_id[i],table_nb_seed[i]);
            //else
            //    printf("****************node %i with last seed %i found nb %i with seed %i\n",node_ID_,node_last_seed_,table_nb_id[i],table_nb_seed[i]);
            int nbHash = hashSeed(slot_num,table_nb_seed[i]);
            hashID[idx] = table_nb_id[i];
            hashValue[idx] = nbHash;
        }


    }

    // find winning vslot
    for(int i=0;i<vslot_num;i++) {
            idx++;
            int vslotHash = hashSeed(slot_num,vslots[i]);

            hashID[idx] = vslotIDs[i];
            hashValue[idx] = vslotHash;
    }

    bubbleSort2(hashValue,hashID,idx+1);

}
char MacDynamicTdma::findRunnerUpNode(int pos) {
    int is_vslot, is_in_poll;
    is_vslot = 0;
    is_in_poll = 0;
    for(int i=0;i<NUM_VSLOTS;i++) {
        if(vslotIDs[i]==hashID[pos])
            is_vslot = 1;
    }
    for(int i=0;i<POLL_SIZE;i++) {
        if(poll_list_[i]==hashID[pos])
            is_in_poll = 1;
    }
    if(!is_vslot && !is_in_poll)
        return hashID[pos];

    pos=1;

    while(pos < MAX_NODE_NUM+NUM_VSLOTS) {
        is_vslot = 0;
        is_in_poll = 0;
        for(int i=0;i<NUM_VSLOTS;i++) {
            if(vslotIDs[i]==hashID[pos])
                is_vslot = 1;
        }
        for(int i=0;i<POLL_SIZE;i++) {
            if(poll_list_[i]==hashID[pos])
                is_in_poll = 1;
        }
        if(!is_vslot && !is_in_poll)
            return hashID[pos];

        pos++;

    }

    return 0x00;

}

// Calculate hash using bitwise operation
int MacDynamicTdma::hashSeed(int slot_num, int seed) {

    // create key
    long key = (long) slot_num;
    key = key << 32;
    key = key + (long) seed;

    // hash key
    key = (~key) + (key << 21);
    key = key ^ (key >> 24);
    key = (key + (key << 3)) + (key << 8); // key * 265
    key = key ^ (key >> 14);
    key = (key + (key << 2)) + (key << 4); // key * 21
    key = key ^ (key >> 28);
    key = key + (key << 31);
    return abs((int) key);


}





void MacDynamicTdma::updateNeighborTable(Packet* p) {
    //printf("$$$$ Node %i is updating neighbor at time %f\n",node_ID_,Scheduler::instance().clock());
    struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(p);
    struct hdr_nb_info* nb = hdr_nb_info::access(p);
    char s_id = (char) mh->srcID;
    MsgType s_msg = (MsgType) mh->srcMsgType;
    u_int8_t s_seed = (u_int8_t) mh->srcSeed;
    recordOneHop(s_id);

    updateNeighbor(s_id,s_msg,s_seed,1);

    // access frame header for Tx node's neighbors info
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(nb->nbID[i] != 0x00 && nb->nbID[i] != node_ID_) {
            char nb_id = (char) nb->nbID[i];
            MsgType nb_msg = (MsgType) nb->nbMsgType[i];
            u_int8_t nb_seed = (u_int8_t) nb->nbSeed[i];
            int nr_hops = 2;
            if(checkOneHop(nb_id)) {
                nr_hops = 1;
            } else {
                recordTwoHop(nb_id);
                updateNeighbor(nb_id,nb_msg,nb_seed,nr_hops);
            }
        }
    }

    return;
}
void MacDynamicTdma::updateNeighbor(char id,MsgType msg_t,u_int8_t seed,u_int8_t hops) {
    int idx = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]==id) {
            idx = i;
            break;
        }
        if(table_nb_id[i]!=0x00) {
            idx = i+1;
        }
    }
    // set neighbor id
    table_nb_id[idx] = id;
    // set neighbor msg type
    table_nb_msg_type[idx] = msg_t;
    // set neighbor seed
    table_nb_seed[idx] = seed;
    // set hops
    table_nb_hops[idx] = hops;

    if(is_seed_sent && seed==node_seed_) {
        node_seed_ = assignSeed();
    }

    return;
}
void MacDynamicTdma::recordOneHop(char id) {
    int idx = 0;

    for(int i=0;i<MAX_NODE_NUM-1;i++) {

        if(onehop_nb_id[i]==id) {
            idx = i;
            break;
        }
        if(onehop_nb_id[i]!=0x00) {
            idx = i+1;
        }

    }

    onehop_nb_id[idx] = id;
    onehop_nb_found[idx] = 1;



}
int MacDynamicTdma::checkOneHop(char id) {
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(onehop_nb_id[i]==id) {
            if(onehop_nb_found[i]==1)
                return 1;
            else
                return 0;
        }
    }
    return 0;
}

void MacDynamicTdma::recordTwoHop(char id) {
    int idx = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(twohop_nb_id[i]==id) {
            idx = i;
            break;
        }
        if(twohop_nb_id[i]!=0x00) {
            idx = i+1;
        }
    }

    twohop_nb_id[idx] = id;
    twohop_nb_found[idx] = 1;
}
int MacDynamicTdma::checkTwoHop(char id) {
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(twohop_nb_id[i]==id) {
            if(twohop_nb_found[i]==1)
                return 1;
            else
                return 0;
        }
    }
    return 0;
}

void MacDynamicTdma::bubbleSort(int *arr1,int *arr2, int len)
{
    int i, j, flag = 1;    // set flag to 1 to start first pass
    int temp1, temp2;

    for(i = 1; (i <= len) && flag; i++)
    {
      flag = 0;
      for (j=0; j < (len -1); j++)
     {
           if (arr1[j+1] < arr1[j] || (arr1[j+1]==arr1[j] && arr2[j+1]<arr2[j]))
          {
                temp1 = arr1[j];             // swap elements
                temp2 = arr2[j];
                arr1[j] = arr1[j+1];
                arr2[j] = arr2[j+1];
                arr1[j+1] = temp1;
                arr2[j+1] = temp2;
                flag = 1;               // indicates that a swap occurred.
           }
      }
    }
    return;   //arrays are passed to functions by address; nothing is returned
}

void MacDynamicTdma::bubbleSort2(int *arr1,char *arr2, int len)
{
    int i, j, flag = 1;    // set flag to 1 to start first pass
    int temp1;
    char temp2;
    for(i = 1; (i <= len) && flag; i++)
    {
      flag = 0;
      for (j=0; j < (len -1); j++)
     {
           if (arr1[j+1] > arr1[j])
          {
                temp1 = arr1[j];             // swap elements
                temp2 = arr2[j];
                arr1[j] = arr1[j+1];
                arr2[j] = arr2[j+1];
                arr1[j+1] = temp1;
                arr2[j+1] = temp2;
                flag = 1;               // indicates that a swap occurred.
           }
      }
    }
    return;   //arrays are passed to functions by address; nothing is returned
}

int MacDynamicTdma::assignSlots(int id,int msg_t,int rate) {
    int slotPeriod = 0;
    int max_msg_size = 0;
    switch(msg_t) {
        case 1:
            // Message Type 1 RAP message require 1/10 s update rate with highest priority
            slotPeriod = (int) (rate/slot_time_);
            max_msg_size = 10005;   // double max. size when double BW
            break;
        case 2:
            // Message Type 2 target track message require 1/2 s update rate with second highest priority
            slotPeriod = (int) (rate/slot_time_);
            max_msg_size = 2005;
            break;
        case 3:
            // Message Type 3 position report message require 1/2 s update rate with lowest priority
            slotPeriod = (int) (rate/slot_time_);
            max_msg_size = 55;
            break;

        case 0:
            return 1;
            break;
    }
            int slotPayloadSize = 516;  // use 516 for double bandwidth
            // in this algorithm, we only assign slot based on maximum allow size of each message.
            int slotsReq = (int) ceil((double) (max_msg_size/slotPayloadSize)+0.5);
            if(slotsReq<1)
                slotsReq=1;
            int periodCount = (int) (max_slot_num_/slotPeriod);
            int blockCount = (int) (slotPeriod/20);

            int is_assigned_in_frame = 0;
            printf("Node %i Current message require %i bytes requires slot period = %i slots. %i periods in a cycle. %i blocks in a period. %i slot reserved in a period\n", id,max_msg_size,slotPeriod, periodCount, blockCount, slotsReq);
            //guarantee 1 slot every 2 seconds
            tdma_schedule_[(id-65)+1] = id;
            tdma_schedule_[(id-65)+1+40] = id;
            tdma_schedule_[(id-65)+1+80] = id;
            tdma_schedule_[(id-65)+1+120] = id;
            tdma_schedule_[(id-65)+1+160] = id;
            for(int i = 0;i<periodCount;i++) {
                int current_block = 0;
                int slotPointer = 1;
                int slotsAssigned = 0;
                int is_period_full = 0;
                int is_assigned_in_period = 0;
                for(int n = 0;n<slotPeriod;n++) {
                    if(tdma_schedule_[i*slotPeriod+n]==id)
                        slotsAssigned++;
                }
                int f_pos = (id-65) % 16;
                slotPointer = f_pos;
                //int startP = 0;
                while(slotsAssigned<slotsReq && !is_period_full) {

                    //printf("p %i ",slotPointer);
                    //printf("r %i ",tdma_schedule_[i*slotPeriod+20*current_block+slotPointer+1]);
                    if(tdma_schedule_[i*slotPeriod+20*current_block+slotPointer+1]>0) {
                        int cnt_blk = 0;
                        for(int l=1;l<20;l++) {
                            if(tdma_schedule_[i*slotPeriod+20*current_block+l]==-1)
                                cnt_blk++;

                        }
                        if(cnt_blk==0) { //if this block full, move to next block
                            if(current_block<blockCount-1) {
                                current_block++;
                                slotPointer = f_pos;
                            } else {
                                current_block = 0;
                                slotPointer = f_pos;
                            }
                        } else {
                            //move slot pointer
                            slotPointer = (slotPointer+16) % 19;
                        }

                    } else {
                        // if slot is free
                        // assign 1 slot
                        tdma_schedule_[i*slotPeriod+20*current_block+slotPointer+1] = id;
                        slotsAssigned++;
                        is_assigned_in_period = 1;
                        is_assigned_in_frame = 1;
                        // move to next block
                        if(current_block<blockCount-1) {
                            current_block++;
                            slotPointer = f_pos;
                        } else {
                            current_block = 0;
                            slotPointer = f_pos;
                        }
                    }

                    int cnt = 0;


                    for(int n = 0;n<slotPeriod;n++) {

                    if(tdma_schedule_[i*slotPeriod+n]==-1)
                        cnt++;
                    }
                    //printf("fp %i ",cnt);

                    if(cnt==0)
                        is_period_full = 1;



                }

            }

    if(is_assigned_in_frame)
        return 1;
    else
        return 0;
}

int MacDynamicTdma::findNrHops(char id) {
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]==id)
            return table_nb_hops[i];
    }
    return 3;
}
void MacDynamicTdma::allocateDataSlots() {

    //find nb
    int memberCnt = 1;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && (table_nb_hops[i]==1 || table_nb_hops[i]==2)) {
            memberCnt++;
        }
    }
    int *members = new int[memberCnt];
    int *memberMsgT = new int[memberCnt];
    int *memberReqRate = new int[memberCnt];
    int *memberRateAdj = new int[memberCnt];
    //add a node to the list
    int idx = 0;
    members[idx] = node_ID_;
    memberRateAdj[idx] = 0;
    if(node_msg_type_==MSG_1) {
        memberMsgT[idx] = 1;

    } else if(node_msg_type_==MSG_2) {
        memberMsgT[idx] = 2;

    } else if(node_msg_type_==MSG_3) {
        memberMsgT[idx] = 3;

    } else {
        memberMsgT[idx] = 0;

    }
    idx++;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && (table_nb_hops[i]==1 || table_nb_hops[i]==2)) {
                members[idx] = (int) table_nb_id[i];
                memberRateAdj[idx] = 0;
                if(table_nb_msg_type[i]==MSG_1) {
                    memberMsgT[idx] = 1;

                } else if(table_nb_msg_type[i]==MSG_2) {
                    memberMsgT[idx] = 2;

                } else if(table_nb_msg_type[i]==MSG_3) {
                    memberMsgT[idx] = 3;

                } else {
                    memberMsgT[idx] = 0;

                }
                idx++;
        }

    }
    //sorting

    bubbleSort(memberMsgT,members,memberCnt);

    for(int i = 0;i<memberCnt;i++) {
        if(memberMsgT[i] == 1) {
            memberReqRate[i] = 10;
        } else if(memberMsgT[i] == 2) {
            memberReqRate[i] = 2;
        } else if(memberMsgT[i] == 3) {
            memberReqRate[i] = 2;
        } else {
            memberReqRate[i] = 0;
        }

    }
    // reset tdma schedule
    for(int i=0;i<max_slot_num_;i++){
        int m = i % 20;
        if(m!=0)
            tdma_schedule_[i] = -1;
    }



    int allocated = 0;
    idx = 0;
    num_alloc_ = 0;
    for(int idx=0;idx<memberCnt;idx++) {
        allocated = assignSlots(members[idx],memberMsgT[idx],memberReqRate[idx]);
        if(allocated)
            num_alloc_++;
    }


    printf("Node %i reserve slot ",node_ID_);
    for(int j=0;j<max_slot_num_;j++) {
            if(tdma_schedule_[j]==(int) node_ID_)
                printf("%i, ",j);

    }
    printf("\n");

    return;
}



void MacDynamicTdma::accessControlSlots() {
    findHashAndSort(slot_count_,NUM_VSLOTS);
    char winning_node = hashID[0];
    printf("Node %i found %i as winning node for control slot %i\n",node_ID_,winning_node,slot_count_);
    if(!is_in_net) {
        if(is_net_entry && !is_ack_waiting) {
            int is_vslot_win = 0;
            for(int i=0;i<NUM_VSLOTS;i++) {
                if(vslotIDs[i]==winning_node)
                    is_vslot_win = 1;
            }
            if(is_vslot_win) {
                sendNetEntry();
                is_ack_waiting = 1;
                waiting_ack_ctslot_count = 0;
            }

        } else if(is_net_entry && is_ack_waiting) {
            printf("****Node %i is waiting ACK message for %i slot\n",node_ID_,waiting_ack_ctslot_count);
        }
    } else {
        // if a node is already in the net
        // if a node win a slot
        if(winning_node==node_ID_) {
            // if there is ne req in queue, send ACK first
            if(nePkt_) {
                struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(nePkt_);
                char reqID = mh->srcID;
                MsgType reqMsgT = mh->srcMsgType;
                u_int8_t reqSeed = mh->srcSeed;
                // update neighbor table with new entry
                updateNeighbor(reqID,reqMsgT,reqSeed,1);
                recordOneHop(reqID);
                //reallocate slot
                //printf("******* node %i reallocate data slot after send net entry ACK\n",node_ID_);
                allocateDataSlots();
                // send ACK
                sendNetEntryACK();

                return;
            }

            // if there is ne req in queue, send ACK first
            if(ctrlPkt_) {
                //reallocate slot
                //printf("******* node %i reallocate data slot after send control ACK\n",node_ID_);
                allocateDataSlots();
                // send ACK
                sendControlACK();

                return;
            }

            if(is_control_msg_required) {
                // send control message
                printf("Node %i win control slot %i and have something to send\n",node_ID_,slot_count_);
                sendControl();
                is_control_msg_required = 0;
                //reallocate data slot
                //allocateDataSlots();
            } else {
                // send polling message
                printf("Node %i win control slot %i but have nothing to send\n",node_ID_,slot_count_);
                sendPolling();
            }
        } else {
        // listen for control message from other nodes
            radioSwitch(ON);
        }
    }
    return;
}

void MacDynamicTdma::findLeavingNodes() {
    double nl_time;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00) {
            if(checkOneHop(table_nb_id[i])) {
                table_nb_hops[i] = 1;
            } else if(checkTwoHop(table_nb_id[i])) {
                table_nb_hops[i] = 2;
            } else {
                if(table_nb_hops[i] < 3) {
                    table_nb_hops[i] = 3;
                    //record leaving time
                    char out[100];
                    nl_time = Scheduler::instance().clock();
                    sprintf(out, "recordNLTime %i %i %f", node_ID_,table_nb_id[i],nl_time);
                    Tcl& tcl = Tcl::instance();
                    tcl.eval(out);
                }
            }
        }
    }
}



/* Timers' handlers */
/* Slot Timer:
   For the preamble calculation, we should have it:
   occupy one slot time,
   radio turned on for the whole slot.
*/
void MacDynamicTdma::slotHandler(Event *e)
{
	// Restart timer for next slot.
	mhSlot_.start((Packet *)e, slot_time_);

	// reset slot count for next frame.
	if ((slot_count_ == max_slot_num_) || (slot_count_ == FIRST_ROUND)) {
		// We should turn the radio on for the whole slot time.

		//reallocate data slot at the end of cycle, if a node is operating in the net
		if(is_in_net) {

            printf("******* node %i update neighbor table in new frame\n",node_ID_);
		    findLeavingNodes();
		    // reallocate data slot
		    //printf("******* node %i reallocate data slot in new frame\n",node_ID_);
            allocateDataSlots();
		}

		is_seed_sent = 0;
		node_last_seed_ = node_seed_;
		if(!is_net_entry && slot_count_ == max_slot_num_) {
            node_seed_ = assignSeed();
            printf("Node %i has new seed %i\n",node_ID_,node_seed_);
		}
		if(is_conflict_in_frame) {
            char out[100];
            double ctime = Scheduler::instance().clock();

            if(num_conflicts==0) {
                is_conflict_in_frame = 0;
                sprintf(out, "recordResolveTime %i %i %i %f", node_ID_,2,num_conflicts,ctime);
                Tcl& tcl = Tcl::instance();
                tcl.eval(out);
            } else {
                sprintf(out, "recordResolveTime %i %i %i %f", node_ID_,1,num_conflicts,ctime);
                Tcl& tcl = Tcl::instance();
                tcl.eval(out);
            }
		}
		num_conflicts = 0;


        slot_count_ = 0;

        //reset 1-hop and 2-hop neighbors found in previous frame
        for(int i=0;i<MAX_NODE_NUM-1;i++) {
            onehop_nb_id[i] = 0x00;
            onehop_nb_found[i] = 0;
            twohop_nb_id[i] = 0x00;
            twohop_nb_found[i] = 0;
        }
	}


    // if app start but it still does not participate in any net
    if(!is_in_net && is_app_start) {

        if(!is_net_entry) {
            is_net_entry = 1;
            waiting_ct_slot_cnt = 0;
            waiting_ct_slot = (rand() % 5);
            if(waiting_ct_slot==0)
                waiting_ct_slot=1;

            found_exist_node = 0;
            //printf("Node %i will enter net with waiting slots %i\n",node_ID_, waiting_ct_slot);
        }


    }




    radioSwitch(OFF);
	if (tdma_schedule_[slot_count_] == -1) {
		//if a node is going to enter net, turn on radio to monitor net
		switchToNetChannel();


	}
	if (tdma_schedule_[slot_count_] == -2) {
		// only do something in control slot, if app is running
		if(is_app_start) {
            switchToNetChannel();
		    if(!is_in_net) {
		        if(is_ack_waiting)
                    waiting_ack_ctslot_count++;
                // waiting for ack message
                if(waiting_ack_ctslot_count>=5) {
                        printf("*****Waiting time for ACK for Node %i has ended.\n",node_ID_);
                        is_net_entry = 0;
                        is_ack_waiting = 0;
                        waiting_ack_ctslot_count = 0;
                }
                if(is_net_entry && waiting_ct_slot_cnt<waiting_ct_slot) {
                    waiting_ct_slot_cnt++;

                } else if(is_net_entry && waiting_ct_slot_cnt>=waiting_ct_slot) {
                    if(found_exist_node) {
                        printf("Node %i found exist node in net %i\n",node_ID_,net_ID_);
                        accessControlSlots();
                    } else {
                        printf("Node %i is the first node in net %i\n",node_ID_,net_ID_);
                        is_in_net = 1;
                        is_net_entry = 0;
                        //printf("******* node %i allocate data slot as first node\n",node_ID_);
                        allocateDataSlots();
                        nodeInNetCnt++;
                        char out[100];
                        ne_ack_rtime = Scheduler::instance().clock();
                        sprintf(out, "recordNETime %i %i %f", node_ID_,nodeInNetCnt,ne_ack_rtime-first_pkt_atime);
                        Tcl& tcl = Tcl::instance();
                        tcl.eval(out);
                    }
                }
		    } else {
                if(is_cack_waiting) {
                    waiting_cack_count++;
                }
                if(waiting_cack_count>=5) {
                    printf("*****Waiting time for control ACK for Node %i has ended.\n",node_ID_);
                    is_control_msg_required = 1;
                    is_cack_waiting = 0;
                    waiting_cack_count = 0;
                }
                accessControlSlots();
		    }
		}

	}

	if (tdma_schedule_[slot_count_] == (int) node_ID_) {
		//if a node is operating in the net and there is no control message to send, send data packet.
		num_slots_reserved++;
        if(is_in_net) {
            switchToNetChannel();
            send();
        } else {
            radioSwitch(OFF);
        }

	}
	// Data slot is reserved for other node, listen in this slot
	if (tdma_schedule_[slot_count_] > 0 && tdma_schedule_[slot_count_] != (int) node_ID_) {
		switchToNetChannel();

	}

    slot_count_++;
    return;

}

void MacDynamicTdma::recvHandler(Event *e)
{
	u_int32_t dst, src;
	int size, is_coll_;
	struct hdr_cmn *ch = HDR_CMN(pktRx_);
	struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(pktRx_);
    is_coll_ = 0;
	/* Check if any collision happened while receiving. */
	if (rx_state_ == MAC_COLL) {
		ch->error() = 1;
		is_coll_ = 1;
	}



	/* check if this packet was unicast and not intended for me, drop it.*/
	dst = ETHER_ADDR(mh->dh_da);
	src = ETHER_ADDR(mh->dh_sa);
	size = ch->size();

	SET_RX_STATE(MAC_IDLE);
	// Turn the radio off after receiving the whole packet
	radioSwitch(OFF);

	/* Ordinary operations on the incoming packet */
	// Not a pcket destinated to me.
	if ((dst != MAC_BROADCAST) && (dst != (u_int32_t)index_)) {
		drop(pktRx_);
		return;
	}
    if (ch->ptype() == PT_TDLNETENT && !ch->error()) {
		    struct net_entry_msg *ne = net_entry_msg::access(pktRx_);
		    //struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(p);
		    // check type of net entry message whether it is request(1) or ACK(2)
		    if(ne->entry_msg == 1) {
		        if(is_in_net) {
                printf("$$$$ Node %i receive net entry request from %i\n",node_ID_,mh->srcID);

                // Add ne to queue, and wait for next available control slot to create and send ACK and also update neighbor
                // if there is already another ne request in queue, ignore incoming new ne request
                if(nePkt_==0)
                    nePkt_ = pktRx_;
		        }
		    } else {
		        updateNeighborTable(pktRx_);

		        if(ne->entry_ID==node_ID_) {
                    printf("$$$$**** Node %i allocate data slot after receive net entry ACK from %i at time %f\n",node_ID_,mh->srcID,Scheduler::instance().clock());
                    is_net_entry = 0;
                    is_ack_waiting = 0;
                    is_in_net = 1;
                    //allocate data slot
                    allocateDataSlots();
                    nodeInNetCnt++;
                    char out[100];
                    ne_ack_rtime = Scheduler::instance().clock();
                    sprintf(out, "recordNETime %i %i %f", node_ID_,nodeInNetCnt,ne_ack_rtime-first_pkt_atime);
                    Tcl& tcl = Tcl::instance();
                    tcl.eval(out);
		        } else {
		        // existing neighbor receive ACK
                    if(is_in_net) {
                        // If a node receive ACK has net entry request in buffer
                        if(nePkt_) {
                            struct net_entry_msg *ne2 = net_entry_msg::access(nePkt_);
                            // check if ACK that a node received and net entry request in buffer is for the same entry node
                            // If so, remove request from buffer and no need to send another ACK
                            if(ne->entry_ID==ne2->entry_ID)
                                nePkt_ = 0;

                        }
                        //allocate data slot
                        //printf("******* existing node %i reallocate data slot in after receive net entry ACK\n",node_ID_);
                        allocateDataSlots();
                    }
		        }
		    }

            return;
		}
		if(ch->ptype() == PT_TDLCONTROL && !ch->error()) {
		    updateNeighborTable(pktRx_);
            // if a node in the net receive tdl message, send to upper layer
            if(is_in_net) {
                // receive control message during backoff
                if(is_back_off)
                    is_rec_in_back_off = 1;
                //reallocate data slot
                struct control_msg* cm = control_msg::access(pktRx_);
                if(cm->control_msgtype==1 && ctrlPkt_==0) {
                    // if request is received and no other request in buffer, add request to buffer
                    ctrlPkt_ = pktRx_;
                } else if(cm->control_msgtype==2) {
                    printf("******* node %i reallocate data slot after receive control ACK for update in %i\n",node_ID_,cm->control_ID);
                    allocateDataSlots();
                    if(ctrlPkt_) {
                            struct control_msg *cm2 = control_msg::access(ctrlPkt_);
                            // check if ACK that a node received and net entry request in buffer is for the same entry node
                            // If so, remove request from buffer and no need to send another ACK
                            if(cm->control_ID==cm2->control_ID)
                                ctrlPkt_ = 0;

                    }
                    if(cm->control_ID == node_ID_) {
                        is_cack_waiting = 0;
                        waiting_cack_count = 0;
                        is_control_msg_required = 0;
                        printf("record update time for node %i at time %f\n",node_ID_,Scheduler::instance().clock());
                        char out[100];
                        double update_time = Scheduler::instance().clock();
                        sprintf(out, "recordMsgUpdateTime %i %i %f", node_ID_,0,update_time-update_msg_arr_time);
                        Tcl& tcl = Tcl::instance();
                        tcl.eval(out);
                    }
                }
                return;
            } else {
                // if a node dose not operate in the net, but in net entry phase, update neighbor
                // if a node dose not operate in the net, and not in net entry phase, discard packet
                if(is_net_entry) {
                    found_exist_node = 1;
                } else {
                    drop(pktRx_);
                }
                return;
            }
		}
		if(ch->ptype() == PT_TDLDATA && !tx_state_ && !is_coll_) {
		    updateNeighborTable(pktRx_);
            // if a node in the net receive tdl message, send to upper layer
            if(is_in_net) {
                recvDATA(pktRx_);
                return;
            } else {
                // if a node dose not operate in the net, but in net entry phase, mark that it found some exist node
                // if a node dose not operate in the net, and not in net entry phase, just update neighbor and discard
                // packet
                if(is_net_entry) {
                    found_exist_node = 1;

                } else {
                    drop(pktRx_);
                }
                return;
            }
		}
		if(ch->ptype() == PT_TDLPOLL && !ch->error()) {
		    //printf("### Node %i Receive Poll at time %f\n",node_ID_,Scheduler::instance().clock());
		    updateNeighborTable(pktRx_);

		    radioSwitch(ON);

		    struct polling_msg *ph = polling_msg::access(pktRx_);
            char firstRunner = ph->runner_ups[0];
            char secondRunner = ph->runner_ups[1];
            char thirdRunner = ph->runner_ups[2];
            int backOffOrder = checkPollList(pktRx_);
		    if(is_in_net && (is_control_msg_required || nePkt_)) {


                if(is_control_msg_required)
                    printf("Node %i in pos %i of poll receives poll and have control to send\n",node_ID_,backOffOrder);
                else if(nePkt_)
                    printf("Node %i in pos %i of poll receives poll and have net entry ACK to send\n",node_ID_,backOffOrder);
                if(backOffOrder>0) {
                    // each polled node set backoff time in the interval of 3 ms i.e. 1st,2nd,3rd polled nodes set back off time to 3, 6, 9 ms respectively
                    if(backOffOrder == 1) {
                        is_back_off = 1;
                        mhBkOff_.start(pktRx_,0);
                    } else if(backOffOrder == 2 && findNrHops(firstRunner)==1) {
                        is_back_off = 1;
                        mhBkOff_.start(pktRx_,1*0.010);
                    } else if(backOffOrder == 3 && findNrHops(firstRunner)==1 && findNrHops(secondRunner)==1) {
                        is_back_off = 1;
                        mhBkOff_.start(pktRx_,2*0.010);
                    }
                }
		    } else if(!is_in_net && is_net_entry && !is_ack_waiting) {
                //printf("New Node %i receives poll and have net entry to send\n",node_ID_,backOffOrder);
                if(findNrHops(firstRunner)==1 && findNrHops(secondRunner)==1 && findNrHops(thirdRunner)==1) {
                    is_back_off = 1;
                    mhBkOff_.start(pktRx_,3*0.010);
                }
		    }
		}

}

/* After transmission a certain packet. Turn off the radio. */
void MacDynamicTdma::sendHandler(Event *e)
{
	/* Once transmission is complete, drop the packet.
	   p  is just for schedule a event. */
	SET_TX_STATE(MAC_IDLE);

	hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA((Packet *)e);



	// Turn off the radio after sending the whole packet. Except when polling is sent, radio must still be on
	if(mh->frame_type != POLLING_FRAME)
        radioSwitch(OFF);
    /* if data packet has been sent, unlock IFQ. */
    if((FrameType) mh->frame_type==DATA_FRAME) {
        Packet::free((Packet *)e);
        if(callback_) {
            Handler *h = callback_;
            callback_ = 0;
            h->handle((Event*) 0);
        }
    } else {
        Packet::free((Packet *)e);
    }
}

void MacDynamicTdma::backoffHandler(Event *e)
{
    // send control message
    if(!is_rec_in_back_off) {
        if(is_control_msg_required) {
            printf("Back off time for node %i has ended and it will send control message in slot %i at time %f\n",node_ID_,slot_count_,Scheduler::instance().clock());
            sendControl();
            is_control_msg_required = 0;

            //reallocate data slot
            //printf("******* node %i reallocate data slot in after send net control\n",node_ID_);
            allocateDataSlots();
        } else if(nePkt_) {
            sendNetEntryACK();
            //printf("******* node %i reallocate data slot in after send net entry ACK\n",node_ID_);
            allocateDataSlots();
        } else if(!is_in_net && is_net_entry && !is_ack_waiting) {
            sendNetEntry();
            //printf("******* node %i send net entry after receiving poll\n",node_ID_);
        }
    }

    is_back_off = 0;
    is_rec_in_back_off = 0;

}


