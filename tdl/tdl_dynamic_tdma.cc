
#include "delay.h"
#include "connector.h"
#include "packet.h"
#include "random.h"
#include "ip.h"
#include "arp.h"
#include "ll.h"
#include "mac.h"
#include "tdl_dynamic_tdma.h"
#include "wireless-phy.h"
#include "cmu-trace.h"
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

int net_control_info::offset_;
// Net Control Info Class
static class NetControlInfoClass : public PacketHeaderClass {
public:
	NetControlInfoClass() : PacketHeaderClass("PacketHeader/NetControlInfo",
						    sizeof(net_control_info)) {
		bind_offset(&net_control_info::offset_);
	}
} class_netcontrolinfo;

int net_entry_msg::offset_;
// Net entry message Class
static class NetEntryMsgClass : public PacketHeaderClass {
public:
	NetEntryMsgClass() : PacketHeaderClass("PacketHeader/NetEntryMsg",
						    sizeof(net_entry_msg)) {
		bind_offset(&net_entry_msg::offset_);
	}
} class_netentrymsg;

int polling_msg::offset_;
// Polling message Class
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
static int netctrlPktCnt = 0;

MacDynamicTdma::MacDynamicTdma(PHY_MIB* p) :
	Mac(), ctrlPkt_(0), nePkt_(0), mhSlot_(this), mhTxPkt_(this), mhRxPkt_(this), mhBkOff_(this) {
	/* Global variables setting. */
	// Assign Node ID
	node_ID_ = nodeID++;
	node_seed_ = assignSeed();

	printf("MAC created for node %i with seed %i\n",node_ID_,node_seed_);

	// Setup the phy specs.
	phymib_ = p;

	/* Get the parameters of the link (which in bound in mac.cc, 2M by default),
	   and the max number of slots (400).*/
	bind("max_slot_num_", &max_slot_num_);
	bind("control_channel_", &control_channel_);
	bind("assigned_Net_",&assigned_Net_);




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

        // assign net control slots (4 slots in each frame)
        for(int i=0;i<20*4;i=i+20){
        tdma_schedule_[i] = -3;
        }
        // assign control slots (16 slots in each frame)
        for(int i=20*4;i<max_slot_num_;i=i+20){
        tdma_schedule_[i] = -2;
        }

    //initialize neighbor table
    for(int i=0;i<MAX_NODE_NUM-1;i++){
        table_nb_id[i] = 0x00;          // initially no neighbor in table
        table_nb_msg_type[i] = MSG_0;
        table_nb_hops[i] = 3;           // initially all neighbor are over 2 hops
    }
    //initialize neighbor table in net table structure
    for(int i=0;i<MAX_NET_NO;i++) {
        for(int j=0;j<MAX_NODE_NUM-1;j++){
            net_nb_id[i][j] = 0x00;          // initially no neighbor in table
            net_nb_msg_type[i][j] = MSG_0;
            net_nb_seed[i][j] = 0;
        }
    }

    //initialize slot winner table
    for(int i=0;i<NUM_NC_SLOTS;i++) {
        nc_winner[i] = 0x00;
    }
    for(int i=0;i<NUM_CT_SLOTS;i++) {
        ct_winner[i] = 0x00;
    }
    // Assign ID to VSLOTs
    vslotIDs[0] = 0x61;
    vslotIDs[1] = 0x62;
    vslotIDs[2] = 0x63;
    vslotIDs[3] = 0x64;
    vslotIDs[4] = 0x65;
    //vslotIDs[4] = 0x65;

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
    //net_ID_ = 0;
    // Initially assume it is first node
    is_first_node = 1;
    // Initially not in net entry phase
    is_net_entry = 0;
    is_ack_waiting = 0;
    //is_ne_slot_selected = 0;

	slot_count_ = FIRST_ROUND;

	//initialize Net table
	for(int i=0;i<MAX_NET_NO;i++){
        net_IDs[i] = i+1;
        net_Found[i] = 0;
    }
	//Start the Slot timer..
	mhSlot_.start((Packet *) (& intr_), 0);
}

u_int8_t MacDynamicTdma::assignSeed()
{
    // Assign seed
	u_int8_t val = (rand() % 256);
	//unsigned char* vall = ((unsigned char*) val) & 0xFF;
	printf("New Seed for Node %i: init seed is %i and random seed is %i\n",node_ID_,INIT_SEED,val);
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

/* Do the slot re-scheduling:

*/
void MacDynamicTdma::re_schedule() {
	// Record the start time of the new schedule.
	start_time_ = NOW;

}

/* To handle incoming packet. */
void MacDynamicTdma::recv(Packet* p, Handler* h) {

	struct hdr_cmn *ch = HDR_CMN(p);
    struct hdr_tdldata *tdlh = hdr_tdldata::access(p);
	/* Incoming packets from phy layer, send UP to ll layer.
	   Now, it is in receiving mode.
	*/
    //printf("MAC %i in node %i receive some packet\n",index_,node_ID_);
	if (ch->direction() == hdr_cmn::UP) {
		// Since we can't really turn the radio off at lower level,
		// we just discard the packet.
        //printf("MAC %i in node %i receive incoming packet %i\n",index_,node_ID_,ch->uid());
		if (!radio_active_) {
		    printf("node %i receive incoming packet with radio off. Discard packet\n",node_ID_);
			free(p);
			//printf("<%d>, %f, I am sleeping...\n", index_, NOW);
			return;
		}
		if (ch->ptype() == PT_TDLNETCTRL) {
		   struct net_control_info *nc = net_control_info::access(p);
		   printf("node %i receive net control message with netID %i\n",node_ID_,nc->net_ID);
           updateNetTable(p);
           updateNetNeighborTable(p,nc->net_ID);
           // check if a node should update neighbor table in current net or switch net.
           if(is_in_net || is_net_entry) {
               // if it detect net control with the same as its net, stay in the current net and update neighbor table
               if(net_ID_==nc->net_ID)
                    updateNeighborTable(p);
               else
                    isSwitchNet(p);
           }
           //printf("node %i add node %i as its neighbor\n",node_ID_,net_nb_id[nc->net_ID-1][0]);
           return;
		}

		if (ch->ptype() == PT_TDLNETENT) {
		    struct net_entry_msg *ne = net_entry_msg::access(p);
		    struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(p);
		    if(ne->entry_msg == 1) {
                printf("$$$$ Node %i receive net entry request from %i\n",node_ID_,mh->srcID);

                // Add ne to queue, and wait for next available control slot to create and send ACK and also update neighbor
                nePkt_ = p;
		    } else {
		        if(ne->entry_ID==node_ID_) {
                    printf("$$$$**** Node %i receive net entry ACK from %i at time %f\n",node_ID_,mh->srcID,Scheduler::instance().clock());
                    is_net_entry = 0;
                    is_ack_waiting = 0;
                    is_in_net = 1;
                    //update neighbor table
                    updateNeighborTable(p);
                    //allocate data slot
                    allocateDataSlots();
		        } else {
		        // existing neighbor receive ACK
                    if(is_in_net) {
                        //update neighbor table
                        updateNeighborTable(p);
                        //allocate data slot
                        allocateDataSlots();
                    }
		        }
		    }

            return;
		}
		if(ch->ptype() == PT_TDLCONTROL) {
            // if a node in the net receive tdl message, send to upper layer
            if(is_in_net) {
                // receive control message during backoff
                if(is_back_off)
                    is_rec_in_back_off = 1;
                //update neighbor
                updateNeighborTable(p);
                //reallocate data slot
                allocateDataSlots();
                //printf("<%d> packet recved: %d\n", index_, tdma_pr_++);
                return;
            } else {
                // if a node dose not operate in the net, discard packet
                free(p);
                return;
            }
		}
		if(ch->ptype() == PT_TDLDATA) {
            // if a node in the net receive tdl message, send to upper layer
            if(is_in_net) {
                //update neighbor
                updateNeighborTable(p);
                sendUp(p);
                printf("Node %i receive TDL data packet %i\n", node_ID_, hdr_cmn::access(p)->uid());
                return;
            } else {
                // if a node dose not operate in the net, discard packet
                free(p);
                return;
            }
		}
		if(ch->ptype() == PT_TDLPOLL) {
		    if(is_in_net && is_control_msg_required) {
                int backOffOrder = checkPollList(p);
                if(backOffOrder>0) {
                    // each polled node set backoff time in the interval of 3 ms i.e. 1st,2nd,3rd polled nodes set back off time to 3, 6, 9 ms respectively
                    is_back_off = 1;
                    mhBkOff_.start(p,backOffOrder*0.003);
                }
		    }
		}
        return;
	}

    //Node remain silence until the application start to send its first message
    //That is datalink radio is turned on first but neither tx or rx
    //until the tdl application starts, then it can tx or rx in the slot

    printf("MAC in %i receive packet %i size %d from Upper Layer at time %f\n",node_ID_,ch->uid(),ch->size(),Scheduler::instance().clock());
    if(!is_app_start)
        is_app_start = 1;
    // Check if control message needed to be sent when
    if((MsgType) tdlh->type != node_msg_type_) {
        if(is_in_net) {
            // control message is required to be sent before sending any data
            printf("****** require control message\n");
            is_control_msg_required = 1;
        }
    }
    node_msg_type_ = (MsgType) tdlh->type;
    node_msg_size_ = tdlh->nbytes;
    /* Packets coming down from ll layer (from ifq actually),
    send them to phy layer.
    Now, it is in transmitting mode. */
    callback_ = h;
    state(MAC_SEND);
    sendDown(p);
    return;


	//printf("<%d> packet sent down: %d\n", index_, tdma_ps_++);
}

void MacDynamicTdma::sendUp(Packet* p)
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
		Phy *ph;
		ph = netif_;
		printf("Node %i, receiving packet %i, but the channel in %f in slot %i is not idle....???\n", node_ID_,ch->uid(),((WirelessPhy *)ph)->getFreq(), slot_count_);
	}
}

/* Actually receive data packet when RxPktTimer times out. */
void MacDynamicTdma::recvDATA(Packet *p){
	/*Adjust the MAC packet size: strip off the mac header.*/
	struct hdr_cmn *ch = HDR_CMN(p);
	//struct hdr_mac_dynamic_tdma* dh = HDR_MAC_DYNAMIC_TDMA(p);

	//if((FrameType)dh->frame_type == NETCONTROL_FRAME) {
	//	updateTable(p);
	//} else {
		ch->size() -= DYNAMIC_MAC_HDR_LEN;
		ch->num_forwards() += 1;

		/* Pass the packet up to the link-layer.*/
		uptarget_->recv(p, (Handler*) 0);
	//}
}



/* Send packet down to the physical layer.
   Need to calculate a certain time slot for transmission. */
void MacDynamicTdma::sendDown(Packet* p) {
	u_int32_t size;
    int pId;
	//struct hdr_cmn* ch = HDR_CMN(p);

	/* Update the MAC header */
	hdr_cmn* ch = HDR_CMN(p);
	hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
	hdr_nb_info* nb = hdr_nb_info::access(p);
	size = ch->size();
	pId = ch->uid();
	//printf("Node %i add header to packet %i\n", node_ID_,ch->uid());
    //hdr_src((char*)mh, addr());
	//hdr_type((char*)mh, ETHERTYPE_IP);
    //hdr_dst((char*)mh, MAC_BROADCAST);

    mh->frame_type = DATA_FRAME;
    mh->srcID = node_ID_;
    mh->srcMsgType = node_msg_type_;
    mh->srcSeed = node_seed_;
    printf("Node %i add header src seed %i\n", node_ID_,mh->srcSeed);
    nb->nrNB = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && table_nb_hops[i]==1) {
            nb->nbID[i] = table_nb_id[i];
            nb->nbMsgType[i] = table_nb_msg_type[i];
            nb->nbSeed[i] = table_nb_seed[i];
            nb->nrNB++;
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }


    ch->size() = size + DYNAMIC_MAC_HDR_LEN;
    //ch->uid() = pId;
	/* buffer the packet to be sent. in mac.h */

        //printf("Node %i add packet %i with size %i bytes nb %i seed is %i %i %i %i %i %i to buffer\n", node_ID_,&ch->uid(),ch->size(),nb->nbID[1],&nb->nbSeed[0],&nb->nbSeed[1],nb->nbSeed[2],nb->nbSeed[3],nb->nbSeed[4],nb->nbSeed[5]);

	pktTx_ = p;
}

/* Actually send the packet.
   Packet including header should fit in one slot time
*/
void MacDynamicTdma::send()
{
	u_int32_t size;
	struct hdr_cmn* ch;
	struct hdr_mac_dynamic_tdma* dh;
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
	dh = HDR_MAC_DYNAMIC_TDMA(pktTx_);

	//dst = ETHER_ADDR(dh->dh_da);
	//src = ETHER_ADDR(dh->dh_sa);
	size = ch->size();
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
	mhTxPkt_.start(pktTx_->copy(), stime);
	downtarget_->recv(pktTx_, this);
    is_seed_sent = 1;
	pktTx_ = 0;
}

// Turn on / off the radio
void MacDynamicTdma::radioSwitch(int i)
{
	radio_active_ = i;
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

void MacDynamicTdma::switchToControlChannel() {
	Phy *p;
	p = netif_;
	((WirelessPhy *)p)->setFreq(control_channel_);
	//printf("Node %i switch to control channel %f\n", node_ID_,((WirelessPhy *)p)->getFreq());
	if(!is_in_net) {
	    listenForNC();
    } else {
		accessNCToTxRx();
	}
    return;
}

void MacDynamicTdma::switchToNetChannel() {
	Phy *p;
	p = netif_;
	((WirelessPhy *)p)->setFreq(net_freq_);
	//printf("Node %i switch to net %i in frequency %f\n", node_ID_, net_ID_, net_freq_);
    radioSwitch(ON);	//Turn on radio
    return;
}

void MacDynamicTdma::listenForNC() {
    printf("Node %i Listen for NC\n", node_ID_);
	radioSwitch(ON);	//Turn on radio
	return;
}

void MacDynamicTdma::accessNCToTxRx() {
    //printf("Node %i Access NC\n", node_ID_);
    findHashAndSort(slot_count_,3);
    char winning_node_ = findWinningNode(0);
    nc_winner[nc_slot_count] = winning_node_;
    printf("#######Winner is %i in net %i at time %f\n",winning_node_,net_ID_,Scheduler::instance().clock());
    if(winning_node_==node_ID_)
        sendNetControl();
    else
        listenForNC();
    //radioSwitch(ON);
    return;
}

void MacDynamicTdma::sendNetControl() {
    Packet* p;
    double stime;
    p = Packet::alloc();

    net_control_info::access(p)->net_ID = net_ID_;
    for(int i=0;i<NUM_VSLOTS;i++) {
        //int vslotWins = checkSlotsWinner(vslotIDs[i],2);
        net_control_info::access(p)->vslot_seed[i] = vslots[i];
        //net_control_info::access(p)->vslot_wins[i] = vslotWins;
    }



    hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
    hdr_src((char*)mh, addr());
	hdr_type((char*)mh, ETHERTYPE_IP);
    hdr_dst((char*)mh, MAC_BROADCAST);

    mh->frame_type = NETCONTROL_FRAME;
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
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 10;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = netctrlPktCnt++;
    ch->ptype() = PT_TDLNETCTRL;
	ch->timestamp() = Scheduler::instance().clock();
	ch->iface() = UNKN_IFACE.value();
	ch->size() = NET_CONTROL_PAYLOAD_SIZE + DYNAMIC_MAC_HDR_LEN;


    stime = TX_Time(p);
	if(stime > data_time_)
		stime = data_time_;
	ch->txtime() = stime;

	/* Turn on the radio and transmit! */
	SET_TX_STATE(MAC_SEND);
	radioSwitch(ON);
    printf("Node %i send net control packet(%i) %i with size %i bytes for net %i with vslot %i at time %f\n",node_ID_,ch->ptype(),ch->uid(),ch->size(),net_control_info::access(p)->net_ID,net_control_info::access(p)->vslot_seed[0],ch->timestamp());


	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);
	is_seed_sent = 1;

}
void MacDynamicTdma::sendNetEntry() {
    Packet* p;
    double stime;
    p = Packet::alloc();

    net_entry_msg::access(p)->entry_msg = 1;
    net_entry_msg::access(p)->entry_ID = node_ID_;
//    net_entry_msg::access(p)->entry_msg_t = node_msg_type_;
//    net_entry_msg::access(p)->entry_seed = node_seed_;


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
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = netctrlPktCnt++;
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
    //struct hdr_mac_dynamic_tdma* mh_ne = HDR_MAC_DYNAMIC_TDMA(nePkt_);
    net_entry_msg::access(p)->entry_msg = 2;
    net_entry_msg::access(p)->entry_ID = ne->entry_ID;
//    net_entry_msg::access(p)->entry_msg_t = node_msg_type_;
//    net_entry_msg::access(p)->entry_seed = node_seed_;
    //updateNeighbor(mh_ne->srcID,mh_ne->srcMsgType,mh_ne->srcSeed,1);

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
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = netctrlPktCnt++;
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




	printf("***Node %i include %i neighbors in ACK message",node_ID_,nb->nrNB);
    printf("Node %i send net entry ACK packet %i size %d bytes in slot %d at time %f\n",node_ID_,ch->uid(),ch->size(),slot_count_,Scheduler::instance().clock());

	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);

	nePkt_ = 0;

	return;
}

void MacDynamicTdma::sendControl() {
    Packet* p;
    double stime;
    p = Packet::alloc();

    struct hdr_mac_dynamic_tdma* mh = HDR_MAC_DYNAMIC_TDMA(p);
    hdr_src((char*)mh, addr());
	hdr_type((char*)mh, ETHERTYPE_IP);
    hdr_dst((char*)mh, MAC_BROADCAST);

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
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = netctrlPktCnt++;
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




	//printf("***Node %i include %i neighbors in ACK message",node_ID_,mh->nrNB);
    printf("Node %i send update control packet %i size %d bytes in slot %d at time %f\n",node_ID_,ch->uid(),ch->size(),slot_count_,Scheduler::instance().clock());

	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);


	return;
}

void MacDynamicTdma::sendPolling() {
    Packet* p;
    double stime;
    p = Packet::alloc();
    struct polling_msg* ph = polling_msg::access(p);
    char firstRunner = findWinningNode(1);
    char secondRunner = findWinningNode(2);
    char thirdRunner = findWinningNode(3);
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
        } else {
            nb->nbID[i] = 0x00;
            nb->nbMsgType[i] = MSG_0;
            nb->nbSeed[i] = 0;

        }
    }

    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = netctrlPktCnt++;
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




	//printf("***Node %i include %i neighbors in ACK message",node_ID_,mh->nrNB);
    printf("Node %i send polling packet %i size %d bytes in slot %d at time %f\n",node_ID_,ch->uid(),ch->size(),slot_count_,Scheduler::instance().clock());

	/* Start a timer that expires when the packet transmission is complete. */
    mhTxPkt_.start(p->copy(), stime);
	downtarget_->recv(p, this);


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
    for(int i=0;i<MAX_NODE_NUM;i++) {
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
            //printf("****************found nb %i with seed %i\n",table_nb_id[i],table_nb_seed[i]);
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
    for(int i=0;i<vslot_num;i++) {
            printf("vslot %i seed %i, ",i+1,vslots[i]);
    }
//    for(int i=0;i<MAX_NODE_NUM;i++) {
//        printf("node %i hash %i\n",hashID[i],hashValue[i]);
//    }
//
}
char MacDynamicTdma::findWinningNode(int pos) {
//    int nodeHash = hashSeed(slot_num,node_seed_);
//    //printf("****************Node %i with seed %i is finding winning node\n",node_ID_,node_seed_);
//    int maxHash = 0;
//    char winner  = 0x00;
//    if(is_in_net) {
//        maxHash = nodeHash;
//        winner = node_ID_;
//    }
//
//
//    // find winning neighbor
//    for(int i=0;i<MAX_NODE_NUM-1;i++) {
//        if(table_nb_id[i]!=0x00 && table_nb_hops[i]<3) {
//            //printf("****************found nb %i with seed %i\n",table_nb_id[i],table_nb_seed[i]);
//            int nbHash = hashSeed(slot_num,table_nb_seed[i]);
//            //printf("nb %i get hash %i\n",table_nb_id[i],nbHash);
//            if(nbHash>maxHash) {
//
//                    maxHash = nbHash;
//                    winner = table_nb_id[i];
//
//            }
//        }
//
//
//    }
//
//    // find winning vslot
//    for(int i=0;i<vslot_num;i++) {
//            int vslotHash = hashSeed(slot_num,vslots[i]);
//
//            //int vslot_found = 0;
//            //vslot_found = checkSlotsWinner(vslotIDs[i],slot_type);
//            //printf("VSLOT %i get hash %i. Current Max Hash is %i. this vslot have won %i slots in current frame\n",vslotIDs[i], vslotHash, maxHash,vslot_found);
//            if(vslotHash>maxHash) {
//                //printf("max hash change to %i vslots %i\n",vslotHash,vslotIDs[i]);
//                maxHash = vslotHash;
//                winner = vslotIDs[i];
//
//            }
//    }

    //int count_winners = checkSlotsWinner(winner,slot_type);
    printf("Node %i with seed %i found node %i as %i winning node for slot %i\n",node_ID_,node_seed_,hashID[pos],pos,slot_count_);
//    for(int i=0;i<MAX_NODE_NUM;i++) {
//        printf("node %i hash %i, ",hashID[i],hashValue[i]);
//    }

    return hashID[pos];


}

int MacDynamicTdma::checkSlotsWinner(char node_id,int slot_type) {
    int count_wins = 0;
    // check for winner of NC slots
    if(slot_type==1) {
        for(int i=0;i<NUM_NC_SLOTS;i++) {
            if(nc_winner[i]==node_id)
                count_wins++;
        }
        return count_wins;
    } else {
        for(int i=0;i<NUM_CT_SLOTS;i++) {
            if(ct_winner[i]==node_id)
                count_wins++;
        }
        return count_wins;
    }
}
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

void MacDynamicTdma::selectNet() {
    //printf("Do net entry\n");
    // check for existing of define net
    int nrExisting = 0;
    //int isExist =0;
    for(int i=0;i<MAX_NET_NO;i++) {
        //check if assigned net is detected
        if(net_Found[i]==1&&net_IDs[i])
            nrExisting++;
    }
    if(nrExisting>0) {
    //if(isExist) {
        int existing_nets[nrExisting];
        int k = 0;
        is_first_node = 0;
        for(int i=0;i<MAX_NET_NO;i++) {
            if(net_Found[i]==1) {
                existing_nets[k] = net_IDs[i];
                k++;
            }
        }

        // select existing net randomly
        int idx = (rand() % nrExisting);
        net_ID_ = existing_nets[idx];
        net_freq_ = control_channel_ + net_ID_*CHANNEL_SPACING; //Compute channel frequency
        int idx1 = 0;
        for(int i=0;i<MAX_NET_NO;i++) {
            if(net_IDs[i]==net_ID_)
                idx1 = i;
        }
        for(int i=0;i<NUM_VSLOTS;i++) {
            vslots[i] = net_vslots[idx1][i];
        }
        // copy net's neighbor table to node's neighbor table
        for(int i=0;i<MAX_NODE_NUM-1;i++) {
            table_nb_id[i] = 0x00;
            table_nb_msg_type[i] = MSG_0;
            table_nb_seed[i] = 0;
            table_nb_hops[i] = 3;
            if(net_nb_id[idx1][i]!=0x00) {
                //printf("select net %i with neighbor %i",net_ID_,net_nb_id[idx1][i]);
                table_nb_id[i] = net_nb_id[idx1][i];
                table_nb_msg_type[i] = net_nb_msg_type[idx1][i];
                table_nb_seed[i] = net_nb_seed[idx1][i];
                table_nb_hops[i] = 1;
            }
        }
        //mark that a node will be in net entry phase
        is_net_entry = 1;
        //select vslot to use for NE randomly
        idx = (rand() % NUM_VSLOTS);
        ne_vslot = vslotIDs[idx];
        printf("Node %i try to enter net %i with freq %f \n",node_ID_,net_ID_,net_freq_);
        printf("----- Node %i select vslot %i as entry slot \n",node_ID_,ne_vslot);
    } else {
        // If no net exist, start new net
        int idx = (rand() % MAX_NET_NO);
        net_ID_ = net_IDs[idx];
        net_freq_ = control_channel_ + net_ID_*CHANNEL_SPACING; //Compute channel frequency
        printf("assign %i vslot seed\n",NUM_VSLOTS);
        for(int i=0;i<NUM_VSLOTS;i++) {
            vslots[i] = assignSeed();
        }
        // reset neighbor table for new net
        for(int i=0;i<MAX_NODE_NUM-1;i++) {
            table_nb_id[i] = 0x00;
            table_nb_msg_type[i] = MSG_0;
            table_nb_seed[i] = 0;
            table_nb_hops[i] = 3;
        }
        printf("Node %i start net %i with freq %f \n",node_ID_,net_ID_,net_freq_);
    }
    //is_in_net = 1;

    //radioSwitch(ON);
    return;
}

void MacDynamicTdma::getNet() {
    //printf("Do net entry\n");
    // check for existing of define net
    int isExist =0;
    for(int i=0;i<MAX_NET_NO;i++) {
        //check if assigned net is detected
        if(net_Found[i]==1&&net_IDs[i]==(u_int8_t)assigned_Net_)
            isExist = 1;
    }
    if(isExist) {

        is_first_node = 0;


        // set net frequency
        net_ID_ = (u_int8_t) assigned_Net_;
        net_freq_ = control_channel_ + net_ID_*CHANNEL_SPACING; //Compute channel frequency

        // retrieve vslots
        int idx1 = 0;
        for(int i=0;i<MAX_NET_NO;i++) {
            if(net_IDs[i]==net_ID_)
                idx1 = i;
        }
        for(int i=0;i<NUM_VSLOTS;i++) {
            vslots[i] = net_vslots[idx1][i];
        }
        // copy net's neighbor table to node's neighbor table
        for(int i=0;i<MAX_NODE_NUM-1;i++) {
            table_nb_id[i] = 0x00;
            table_nb_msg_type[i] = MSG_0;
            table_nb_seed[i] = 0;
            table_nb_hops[i] = 3;
            if(net_nb_id[idx1][i]!=0x00) {
                //printf("select net %i with neighbor %i",net_ID_,net_nb_id[idx1][i]);
                table_nb_id[i] = net_nb_id[idx1][i];
                table_nb_msg_type[i] = net_nb_msg_type[idx1][i];
                table_nb_seed[i] = net_nb_seed[idx1][i];
                table_nb_hops[i] = 1;
            }
        }
        //mark that a node will be in net entry phase
        is_net_entry = 1;
        //select vslot to use for NE randomly
        int idx = (rand() % NUM_VSLOTS);
        ne_vslot = vslotIDs[idx];
        printf("Node %i try to enter net %i with freq %f \n",node_ID_,net_ID_,net_freq_);
        printf("----- Node %i select vslot %i as entry slot \n",node_ID_,ne_vslot);
    } else {
        // If no assigned net exist, start new net
        //int idx = (rand() % MAX_NET_NO);
        //net_ID_ = net_IDs[idx];
        net_ID_ = (u_int8_t) assigned_Net_;
        net_freq_ = control_channel_ + net_ID_*CHANNEL_SPACING; //Compute channel frequency
        printf("assign %i vslot seed\n",NUM_VSLOTS);
        for(int i=0;i<NUM_VSLOTS;i++) {
            vslots[i] = assignSeed();
        }
        // reset neighbor table for new net
        for(int i=0;i<MAX_NODE_NUM-1;i++) {
            table_nb_id[i] = 0x00;
            table_nb_msg_type[i] = MSG_0;
            table_nb_seed[i] = 0;
            table_nb_hops[i] = 3;
        }
        printf("Node %i start net %i with freq %f \n",node_ID_,net_ID_,net_freq_);
    }
    //is_in_net = 1;

    //radioSwitch(ON);
    return;
}

void MacDynamicTdma::updateNetTable(Packet* p) {
    struct net_control_info *nc = net_control_info::access(p);
    for(int i=0;i<MAX_NET_NO;i++) {
        if(net_IDs[i]==(u_int8_t)nc->net_ID) {
            // set status of detected net
            net_Found[i]=1;
            //printf("node %i found net %i\n",node_ID_,nc->net_ID);
            // set vslot seeds of detected net
            for(int k = 0;k<NUM_VSLOTS;k++) {
            net_vslots[i][k] = (u_int8_t) nc->vslot_seed[k];

            //printf("node %i record vslot seed %i associated with net %i\n",node_ID_,nc->vslot_seed[k],nc->net_ID);
            }
        }
    }

    return;
}
void MacDynamicTdma::updateNetNeighborTable(Packet* p,u_int8_t netID) {
    struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(p);
    struct hdr_nb_info* nb = hdr_nb_info::access(p);
    // access frame header for Tx node info
    char s_id = (char) mh->srcID;
    MsgType s_msg = (MsgType) mh->srcMsgType;
    u_int8_t s_seed = (u_int8_t) mh->srcSeed;
    recordOneHop(s_id);
    updateNetNeighbor(s_id,s_msg,s_seed,netID);

    // access frame header for Tx node's neighbors info
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(nb->nbID[i] != 0x00 && nb->nbID[i] != node_ID_) {
            //printf("found a neighbor from net control msg\n");
            char nb_id = (char) nb->nbID[i];
            MsgType nb_msg = (MsgType) nb->nbMsgType[i];
            u_int8_t nb_seed = (u_int8_t) nb->nbSeed[i];
            updateNetNeighbor(nb_id,nb_msg,nb_seed,netID);
        }
    }
    return;
}
void MacDynamicTdma::updateNetNeighbor(char id,MsgType msg_t,u_int8_t seed,u_int8_t netID) {
    //find tx node in net's neighbor table
    int idx = 0;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(net_nb_id[netID-1][i]==id) {
            idx = i;
            break;
        }
        if(net_nb_id[netID-1][i]!=0x00) {
            idx = i+1;
        }
    }
    // set net's neighbor id
    net_nb_id[netID-1][idx] = id;
    // set net's neighbor msg type
    net_nb_msg_type[netID-1][idx] = msg_t;
    // set net's neighbor seed
    net_nb_seed[netID-1][idx] = seed;

    return;

}
void MacDynamicTdma::updateNeighborTable(Packet* p) {
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
            //printf("found a neighbor from net control msg\n");
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
//    printf("Node %i has following neighbor\n",node_ID_);
//    for(int i=0;i<MAX_NODE_NUM-1;i++) {
//        printf("nb %i with seed %i\n",table_nb_id[i],table_nb_seed[i]);
//    }
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
    //u_int8_t temp3;             // holding variable
    for(i = 1; (i <= len) && flag; i++)
    {
      flag = 0;
      for (j=0; j < (len -1); j++)
     {
           if (arr1[j+1] < arr1[j] || (arr1[j+1]==arr1[j] && arr2[j+1]<arr2[j]))      // ascending order simply changes to <
          {
                temp1 = arr1[j];             // swap elements
                temp2 = arr2[j];
                //temp3 = arr3[j];
                arr1[j] = arr1[j+1];
                arr2[j] = arr2[j+1];
                //arr3[j] = arr3[j+1];
                arr1[j+1] = temp1;
                arr2[j+1] = temp2;
                //arr3[j+1] = temp3;
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
    //u_int8_t temp3;             // holding variable
    for(i = 1; (i <= len) && flag; i++)
    {
      flag = 0;
      for (j=0; j < (len -1); j++)
     {
           if (arr1[j+1] > arr1[j])      // ascending order simply changes to <
          {
                temp1 = arr1[j];             // swap elements
                temp2 = arr2[j];
                //temp3 = arr3[j];
                arr1[j] = arr1[j+1];
                arr2[j] = arr2[j+1];
                //arr3[j] = arr3[j+1];
                arr1[j+1] = temp1;
                arr2[j+1] = temp2;
                //arr3[j+1] = temp3;
                flag = 1;               // indicates that a swap occurred.
           }
      }
    }
    return;   //arrays are passed to functions by address; nothing is returned
}

int MacDynamicTdma::assignSlots(int id,int msg_t,int rate) {
    int slotPeriod = 0;
    int max_msg_size = 0;
    printf("***Do data slot assignment at %f for node %i with rate %i\n",Scheduler::instance().clock(),id,rate);
    switch(msg_t) {
        case 1:
            // Message Type 1 RAP message require 1/10 s update rate with highest priority
            printf("Current message is type 1\n");
            slotPeriod = (int) (rate/slot_time_);
            max_msg_size = 5000;
            break;
        case 2:
            // Message Type 2 target track message require 1/2 s update rate with second highest priority
            printf("Current message is type 2\n");
            slotPeriod = (int) (rate/slot_time_);
            max_msg_size = 1200;
            break;
        case 3:
            // Message Type 3 position report message require 1/2 s update rate with lowest priority
            printf("Current message is type 3\n");
            slotPeriod = (int) (rate/slot_time_);
            max_msg_size = 50;
            break;

        case 0:
            printf("invalid message type\n");
            return 0;
            break;
    }
            int datagramSize = 200;
            // in this algorithm, we only assign slot based on maximum allow size of each message. Hence, we reserve slots
            // more than it actually requires. To improve slot utilization, we may improve slot assignment based on size of each
            // message. A node will reserve only sufficient amount of slots to transmit message with specific size. The disadvantage
            // is that it requires node and neighbor's message size to be included in frame header, and this will be quite a large overhead.
            int slotsReq = (int) ceil((double) (max_msg_size/datagramSize));
            if(slotsReq<1)
                slotsReq=1;
            int periodCount = (int) (max_slot_num_/slotPeriod);
            int blockCount = (int) (slotPeriod/20);

            //printf("Current message require %i bytes requires slot period = %i slots. %i periods in a cycle. %i blocks in a period. %i slot reserved in a period\n", max_msg_size,slotPeriod, periodCount, blockCount, slotsReq);
            for(int i = 0;i<periodCount;i++) {
                int current_block = 0;
                int slotPointer = 1;
                int slotsAssigned = 0;
                while(slotsAssigned<slotsReq) {
                    // if slot is already reserved
                    if(tdma_schedule_[i*slotPeriod+20*current_block+slotPointer]>0) {
                        //move slot pointer
                        slotPointer++;
                    } else {
                    // if slot is free
                        // assign 1 slot
                        tdma_schedule_[i*slotPeriod+20*current_block+slotPointer] = id;
                        //printf("slot %i is assigned to node %i in block %i\n",i*slotPeriod+20*current_block+slotPointer, id, current_block);
                        slotsAssigned++;

                        // move to next block
                        if(current_block<blockCount-1) {
                            current_block++;
                            slotPointer = 1;
                        } else {
                            current_block = 0;
                            slotPointer = 1;
                        }

                    }
                    // if slot pointer point to control slot in the next block, it means a node can not reserve slots as required.
                    // Reset slot reserve by this node and return to calling procedure
                    if(((i*slotPeriod+20*current_block+slotPointer) % 20) == 0) {
                        if(current_block<blockCount-1) {
                            current_block++;
                            slotPointer = 1;
                        } else {
                            for(int j=0;j<max_slot_num_;j++) {
                                if(tdma_schedule_[j]==id)
                                    tdma_schedule_[j]=-1;
                            }
                            return 0;
                        }
                    }
//                    if(((i*slotPeriod+20*current_block+slotPointer) % 20) == 0) {
//                        if(current_block<blockCount-1) {
//                            current_block++;
//                            slotPointer = 1;
//                        } else {
//                            //all slots in period are filled
//
//                            // if number of slot reserved does not meet the requirement
//                            if(slotsAssigned<slotsReq) {
//                                // change slot period
//                                slotPeriod = slotPeriod*2;
//                                periodCount = (int) (max_slot_num_/slotPeriod);
//                                blockCount = (int) (slotPeriod/20);
//                                current_block++;    // move to next block
//                                slotPointer = 19;   // move pointer to the end of next block
//                                //free some slots and then assign slot
//                                int slotLeft = slotsReq - slotsAssigned;
//                                while(slotLeft>0) {
//                                    if(tdma_schedule_[i*slotPeriod+20*current_block+slotPointer]>0 && tdma_schedule_[i*slotPeriod+20*current_block+slotPointer]!=id) {
//                                        //check if this slot can be free
//                                        int cid = tdma_schedule_[i*slotPeriod+20*current_block+slotPointer];
//                                        MsgType cType = MSG_0;
//                                        if(cid == (int)node_ID_) {
//                                            cType = node_msg_type_;
//                                        } else {
//                                            for(int j=0;j<MAX_NODE_NUM-1;j++) {
//                                                if(table_nb_id[j]==cid) {
//                                                    cType = table_nb_msg_type[j];
//                                                }
//                                            }
//                                        }
//                                        int maxSlotReq = 0;
//                                        if(cType==MSG_1) {
//                                            maxSlotReq = 25;
//                                        } else if(cType==MSG_2) {
//                                            maxSlotReq = 6;
//                                        } else {
//                                            maxSlotReq = 1;
//                                        }
//                                        int countRes = 0;
//                                        for(int j=i*slotPeriod;j<(i+1)*slotPeriod;j++) {
//                                            if(tdma_schedule_[j]==cid)
//                                                countRes++;
//                                        }
//
//                                        if(countRes>maxSlotReq) {
//                                            tdma_schedule_[i*slotPeriod+20*current_block+slotPointer] = id;
//                                            //printf("slot %i is assigned to node %i\n",i*slotPeriod+20*current_block+slotPointer, id);
//                                            slotLeft--;
//                                            if(current_block<blockCount-1) {
//                                                current_block++;
//                                                slotPointer = 20;
//                                            } else {
//                                                current_block--;
//                                                slotPointer = 20;
//                                            }
//                                        }
//
//
//
//
//                                    }
//                                    if(tdma_schedule_[i*slotPeriod+20*current_block+slotPointer]==-1) {
//                                            tdma_schedule_[i*slotPeriod+20*current_block+slotPointer] = id;
//                                            //printf("slot %i is assigned to node %i\n",i*slotPeriod+20*current_block+slotPointer, id);
//                                            slotLeft--;
//                                            if(current_block<blockCount-1) {
//                                                current_block++;
//                                                slotPointer = 20;
//                                            } else {
//                                                current_block--;
//                                                slotPointer = 20;
//                                            }
//                                    }
//                                    slotPointer--;
//                                }
//                            }
//
//                        }
//                    }



                }
            }
    return 1;
}
void MacDynamicTdma::allocateDataSlots() {
    // test data
//    table_nb_id[0] = 0x42;
//    table_nb_id[1] = 0x43;
//    table_nb_id[2] = 0x44;
//    table_nb_id[3] = 0x45;
//    table_nb_id[4] = 0x46;
//    table_nb_id[5] = 0x47;
//    table_nb_id[6] = 0x48;
//    table_nb_id[7] = 0x49;
//    table_nb_id[8] = 0x4A;
//    table_nb_id[9] = 0x4B;
//
//    table_nb_msg_type[0] = MSG_1;
//    table_nb_msg_type[1] = MSG_1;
//    table_nb_msg_type[2] = MSG_2;
//    table_nb_msg_type[3] = MSG_2;
//    table_nb_msg_type[4] = MSG_3;
//    table_nb_msg_type[5] = MSG_3;
//    table_nb_msg_type[6] = MSG_3;
//    table_nb_msg_type[7] = MSG_3;
//    table_nb_msg_type[8] = MSG_3;
//    table_nb_msg_type[9] = MSG_3;

    //find nb
    int memberCnt = 1;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00)
            memberCnt++;
    }
    int *members = new int[memberCnt];
    //u_int8_t *memberSeed = new u_int8_t[memberCnt];
    int *memberMsgT = new int[memberCnt];
    int *memberReqRate = new int[memberCnt];
    int *memberRateAdj = new int[memberCnt];
    //add a node to the list
    int idx = 0;
    members[idx] = node_ID_;
    memberRateAdj[idx] = 0;
    if(node_msg_type_==MSG_1) {
        memberMsgT[idx] = 1;
        //memberReqRate[idx] = 10;
    } else if(node_msg_type_==MSG_2) {
        memberMsgT[idx] = 2;
        //memberReqRate[idx] = 2;
    } else if(node_msg_type_==MSG_3) {
        memberMsgT[idx] = 3;
        //memberReqRate[idx] = 2;
    } else {
        memberMsgT[idx] = 0;
        //memberReqRate[idx] = 0;
    }
    idx++;
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00 && (table_nb_hops[i]==1 || table_nb_hops[i]==2)) {
                members[idx] = (int) table_nb_id[i];
                memberRateAdj[idx] = 0;
                //memberSeed[idx] = table_nb_seed[i];
                if(table_nb_msg_type[i]==MSG_1) {
                    memberMsgT[idx] = 1;
                    //memberReqRate[idx] = 10;
                } else if(table_nb_msg_type[i]==MSG_2) {
                    memberMsgT[idx] = 2;
                    //memberReqRate[idx] = 2;
                } else if(table_nb_msg_type[i]==MSG_3) {
                    memberMsgT[idx] = 3;
                    //memberReqRate[idx] = 2;
                } else {
                    memberMsgT[idx] = 0;
                    //memberReqRate[idx] = 0;
                }
                idx++;
        }

    }
    //sorting
    //printf("Sorting members\n");
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
        //printf("Priority %i is for node %i with message type %i with rate %i\n",i+1,members[i],memberMsgT[i],memberReqRate[i]);
    }
    // reset tdma schedule
    for(int i=0;i<max_slot_num_;i++){
        int m = i % 20;
        if(m!=0)
            tdma_schedule_[i] = -1;
    }
    //assign slots
    int nrAlloc = 0;
    int allocated = 0;
    int adjust1 = 0;
    int adjust2 = 0;
    idx = 0;
    while(nrAlloc<memberCnt) {
        allocated = assignSlots(members[idx],memberMsgT[idx],memberReqRate[idx]);
        // if current member can not reserve require slot
        if(!allocated) {
            //printf("cannot allocate member %i\n",members[idx]);
            if(!adjust1) {
                // try to allocate again with double rate
                memberReqRate[idx] = 2*memberReqRate[idx];
                memberRateAdj[idx]++;
                adjust1 = 1;
                //allocated = assignSlots(members[idx],memberMsgT[idx],memberReqRate[idx]);
            } else {
                // if it is still fail after double rate
                // search for higher priority node to adjust rate
                int foundN = 0;
                //int nrAdjust = 0;
                //int idx2 = idx-1;
                while(!foundN) {
                    idx--;
                    //for(int j=idx;j>-1;j--) {
                        for(int k=0;k<max_slot_num_;k++) {
                            if(tdma_schedule_[k]==members[idx])
                                tdma_schedule_[k] = -1;
                        }
                        nrAlloc--;
                        printf("rate adjust %i \n",memberRateAdj[idx]);
                        if(memberRateAdj[idx]==0) {
                            memberReqRate[idx] = 2*memberReqRate[idx];
                            memberRateAdj[idx]++;
                            foundN = 1;
                            //break;
                        }
                    //}
                }

                adjust1 = 0;
                adjust2 = 0;
            }

        } else {
            adjust1 = 0;
            adjust2 = 0;
            nrAlloc++;
            idx++;
        }
    }
    //for(int i=0;i<memberCnt;i++) {
        //assignSlots(members[i],memberMsgT[i]);
    //}
//    for(int i=0;i<200;i++) {
//        printf("slot %i is assigned for %i\n",i,tdma_schedule_[i]);
//    }
//    for(int i = 0;i<memberCnt;i++) {
//
//        printf("After: Priority %i is for node %i with message type %i with rate %i\n",i+1,members[i],memberMsgT[i],memberReqRate[i]);
//    }
    return;
}



void MacDynamicTdma::accessControlSlots() {
    findHashAndSort(slot_count_,NUM_VSLOTS);
    char winning_node = findWinningNode(0);
    ct_winner[ct_slot_count] = winning_node;
    if(!is_in_net) {
        if(is_net_entry && !is_ack_waiting) {
            if(ne_vslot==winning_node) {
                //printf("****Node %i will send net entry message in slot %i\n",node_ID_,slot_count_);
                sendNetEntry();
                is_ack_waiting = 1;
                waiting_slot_count = 0;
            }

        } else if(is_net_entry && is_ack_waiting) {
            printf("****Node %i is waiting ACK message for %i slot\n",node_ID_,waiting_slot_count);
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
                //reallocate slot
                allocateDataSlots();
                // send ACK
                sendNetEntryACK();

                return;
            }
            if(is_control_msg_required) {
                // send control message
                printf("Node %i win control slot %i and have something to send\n",node_ID_,slot_count_);
                sendControl();
                is_control_msg_required = 0;
                //reallocate data slot
                allocateDataSlots();
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
    for(int i=0;i<MAX_NODE_NUM-1;i++) {
        if(table_nb_id[i]!=0x00) {
            if(checkOneHop(table_nb_id[i]))
                table_nb_hops[i] = 1;
            else if(checkTwoHop(table_nb_id[i]))
                table_nb_hops[i] = 2;
            else
                table_nb_hops[i] = 3;
        }
    }
}

void MacDynamicTdma::isSwitchNet(Packet *p) {
    struct hdr_mac_dynamic_tdma *mh = HDR_MAC_DYNAMIC_TDMA(p);
    struct net_control_info *ne = net_control_info::access(p);
    char txID = mh->srcID;
    // comparing source ID of packet and node's ID
    if((int) txID<(int) node_ID_) {
        // if ID is lower (lower priority), switch net
        is_in_net = 0;

        net_ID_ = ne->net_ID;
        net_freq_ = control_channel_ + net_ID_*CHANNEL_SPACING; //Compute channel frequency
        int idx1 = 0;
        for(int i=0;i<MAX_NET_NO;i++) {
            if(net_IDs[i]==net_ID_)
                idx1 = i;
        }
        for(int i=0;i<NUM_VSLOTS;i++) {
            vslots[i] = net_vslots[idx1][i];
        }
        // copy net's neighbor table to node's neighbor table
        for(int i=0;i<MAX_NODE_NUM-1;i++) {
            table_nb_id[i] = 0x00;
            table_nb_msg_type[i] = MSG_0;
            table_nb_seed[i] = 0;
            table_nb_hops[i] = 3;
            if(net_nb_id[idx1][i]!=0x00) {
                //printf("select net %i with neighbor %i",net_ID_,net_nb_id[idx1][i]);
                table_nb_id[i] = net_nb_id[idx1][i];
                table_nb_msg_type[i] = net_nb_msg_type[idx1][i];
                table_nb_seed[i] = net_nb_seed[idx1][i];
                table_nb_hops[i] = 1;
            }
        }
        //mark that a node will be in net entry phase
        is_net_entry = 1;
        //select vslot to use for NE randomly
        int idx = (rand() % NUM_VSLOTS);
        ne_vslot = vslotIDs[idx];

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

	//radioSwitch(OFF);
    //printf("node %i has seed %i",node_ID_,node_seed_);
	// reset slot count for next frame.
	if ((slot_count_ == max_slot_num_) || (slot_count_ == FIRST_ROUND)) {
		//printf("<%d>, %f, make the new preamble now.\n", index_, NOW);
		// We should turn the radio on for the whole slot time.
		//radioSwitch(ON);

		//reallocate data slot at the end of cycle, if a node is operating in the net
		if(is_in_net) {
		    // find leaving node at the end of cycle and update neighbor table
		    findLeavingNodes();
		    // reallocate data slot
            allocateDataSlots();
		}
		slot_count_ = 0;
		nc_slot_count = 0;
		ct_slot_count = 0;
		is_seed_sent = 0;
		node_last_seed_ = node_seed_;
		if(!is_net_entry && slot_count_ == max_slot_num_)
            node_seed_ = assignSeed();


		//reset
		//is_ne_slot_selected = 0;
		//for(int i=0;i<MAX_NET_NO;i++) {
        //        net_Found[i]=0;
        //}

        //reset slot winner table
        for(int i=0;i<NUM_NC_SLOTS;i++) {
            nc_winner[i] = 0x00;
        }
        for(int i=0;i<NUM_CT_SLOTS;i++) {
            ct_winner[i] = 0x00;
        }
        //reset 1-hop and 2-hop neighbors found in previous frame
        for(int i=0;i<MAX_NODE_NUM-1;i++) {
            onehop_nb_id[i] = 0x00;
            onehop_nb_found[i] = 0;
            twohop_nb_id[i] = 0x00;
            twohop_nb_found[i] = 0;
        }
	}

    if(is_ack_waiting)
        waiting_slot_count++;
    // waiting for ack message for 199 slots
    if(waiting_slot_count==199) {
            printf("*****Waiting time for ACK for Node %i has ended.\n",node_ID_);
            net_ID_ = 0;
            is_net_entry = 0;
            is_ack_waiting = 0;
            waiting_slot_count = 0;
    }
    // if app start but it still does not participate in any net
    if(!is_in_net && is_app_start) {
       //if net id has not been assigned, select net randomly
       //if(!assigned_Net_)
       //     selectNet();
       //if net id has been assigned, check if there is any node operate the net. If there is one, a node will perform net entry. Otherwise, it will allocate data slot directly
       //else
            getNet();

       // If it is a first node in network, register node to the net and allocate data slot
        if(is_first_node) {
            is_in_net = 1;

            allocateDataSlots();

        }
    }





	if (tdma_schedule_[slot_count_] == -1) {
		//printf("slot %i is unreserved data slot ", slot_count_);
        radioSwitch(OFF);

	}
	if (tdma_schedule_[slot_count_] == -2) {
		//printf("slot %i used for control slot ", slot_count_);
		if(is_app_start && net_ID_ != 0) {
		    switchToNetChannel();
            accessControlSlots();
		}
        ct_slot_count++;
	}
	if (tdma_schedule_[slot_count_] == -3) {
		//printf("slot %i used for net control slot ", slot_count_);

		switchToControlChannel();
        nc_slot_count++;
	}
	if (tdma_schedule_[slot_count_] == (int) node_ID_) {
		//printf("Node %i will transmit in slot %i\n", node_ID_,slot_count_);
		//if a node is operating in the net and there is no control message to send, send data packet.
        if(is_in_net && !is_control_msg_required) {
            switchToNetChannel();
            send();
        } else {
            radioSwitch(OFF);
        }

	}
	// Data slot is reserved for other node, listen in this slot
	if (tdma_schedule_[slot_count_] > 0 && tdma_schedule_[slot_count_] != (int) node_ID_) {
		//printf("Node %i will transmit in slot %i\n", node_ID_,slot_count_);
        if(is_in_net) {
            switchToNetChannel();
            radioSwitch(ON);
        } else {
            radioSwitch(OFF);
        }

	}

    slot_count_++;
    return;

}

void MacDynamicTdma::recvHandler(Event *e)
{
	u_int32_t dst, src;
	int size;
	struct hdr_cmn *ch = HDR_CMN(pktRx_);
	struct hdr_mac_dynamic_tdma *dh = HDR_MAC_DYNAMIC_TDMA(pktRx_);

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

    recvDATA(pktRx_);
}

/* After transmission a certain packet. Turn off the radio. */
void MacDynamicTdma::sendHandler(Event *e)
{
	/* Once transmission is complete, drop the packet.
	   p  is just for schedule a event. */
	SET_TX_STATE(MAC_IDLE);
	Packet::free((Packet *)e);

	// Turn off the radio after sending the whole packet
	radioSwitch(OFF);
    //printf("Tx completed in slot %i\n", slot_count_);
	/* unlock IFQ. */
	if(callback_) {
		Handler *h = callback_;
		callback_ = 0;
		h->handle((Event*) 0);
	}
}

void MacDynamicTdma::backoffHandler(Event *e)
{
    //if(is_control_msg_required) {
                // send control message
                if(!is_rec_in_back_off) {
                    printf("Back off time for node %i has ended and it will send control message in slot %i at time %f\n",node_ID_,slot_count_,Scheduler::instance().clock());
                    sendControl();
                    is_control_msg_required = 0;

                    //reallocate data slot
                    allocateDataSlots();
                }

                is_back_off = 0;
                is_rec_in_back_off = 0;
      //      }
}


