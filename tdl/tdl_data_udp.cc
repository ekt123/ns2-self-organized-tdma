/*
tdl_data_udp.cc
Note: constructor and function definitions for tdl_data_udp class
Usage: transport agent for TDL application
*/
#include "tdl_data_udp.h"
#include "rtp.h"
#include "address.h"
#include "ip.h"
#include <string.h>

int hdr_tdldata::offset_;

// Header Class
static class TdlDataHeaderClass : public PacketHeaderClass {
public:
	TdlDataHeaderClass() : PacketHeaderClass("PacketHeader/TdlData",
						    sizeof(hdr_tdldata)) {
		bind_offset(&hdr_tdldata::offset_);
	}
} class_tdldatahdr;

int hdr_tdlmsgupdate::offset_;
static class TdlMsgUpdateClass : public PacketHeaderClass {
public:
	TdlMsgUpdateClass() : PacketHeaderClass("PacketHeader/TdlMsgUpdate",
						    sizeof(hdr_tdlmsgupdate)) {
		bind_offset(&hdr_tdlmsgupdate::offset_);
	}
} class_hdrtdlmsgupdate;

int hdr_tdlnetupdate::offset_;
static class TdlNetUpdateClass : public PacketHeaderClass {
public:
	TdlNetUpdateClass() : PacketHeaderClass("PacketHeader/TdlNetUpdate",
						    sizeof(hdr_tdlnetupdate)) {
		bind_offset(&hdr_tdlnetupdate::offset_);
	}
} class_hdrtdlnetupdate;



//OTcl linkage class
static class TdlDataUdpAgentClass : public TclClass {
public:
	TdlDataUdpAgentClass() : TclClass("Agent/UDP/TdlDataUDP") {}
	TclObject* create(int, const char*const*) {
		return (new TdlDataUdpAgent());
	}
} class_tdldataudp_agent;

// Constructor (with no arg)

TdlDataUdpAgent::TdlDataUdpAgent() : Agent(PT_TDLDATA)
{
	bind("packetSize_", &size_);
	support_tdldata_ = 0;
	for(int i=0;i<16;i++) {
        asm_info[i].seq = -1;
	}
	seqno_ = -1;
    ctrlMsgCnt_ = 0;
}

TdlDataUdpAgent::TdlDataUdpAgent(packet_t type) : Agent(type)
{
	bind("packetSize_", &size_);
	support_tdldata_ = 0;
	for(int i=0;i<16;i++) {
        asm_info[i].seq = -1;
	}
	seqno_ = -1;
	ctrlMsgCnt_ = 0;
}

// Add Support of TDL data Application to UdpAgent::sendmsg
void TdlDataUdpAgent::sendmsg(int nbytes, const char* flags)
{
	Packet *p;
	int n, remain;

    // segment message to datagram
	if (size_) {
		n = (nbytes/size_ + (nbytes%size_ ? 1 : 0));
		remain = nbytes%size_;
	}
	else
		printf("Error: UDP size = 0\n");

	if (nbytes == -1) {
		printf("Error:  sendmsg() for UDP should not be -1\n");
		return;
	}
	double local_time =Scheduler::instance().clock();
	while (n-- > 0) {
		p = allocpkt();
		hdr_tdldata* tdlh = hdr_tdldata::access(p);
		if(n==0 && remain>0) {
		    hdr_cmn::access(p)->size() = remain+TDL_UDP_HDR_LEN;
		} else {
		    hdr_cmn::access(p)->size() = size_+TDL_UDP_HDR_LEN;
		}

		hdr_cmn::access(p)->timestamp() = Scheduler::instance().clock();
		// transport layer header
		tdlh->seq = 0;
		tdlh->nbytes = 0;
		tdlh->time = 0;
		tdlh->messagesize = 0;
		tdlh->datasize = 0;
		// Application layer header
		tdlh->type = MSG_0;
		tdlh->appID = 0;
		// tdl udp packets are distinguished by setting the ip
		// priority bit to 15 (Max Priority).
		if(support_tdldata_) {
			hdr_ip* ih = hdr_ip::access(p);
			ih->daddr() = IP_BROADCAST;
			ih->prio_ = 15;
			// copy header information
			if(flags)
				memcpy(tdlh, flags, sizeof(hdr_tdldata));

			if(n==0 && remain>0) {
                tdlh->datasize = remain;
            } else {
                tdlh->datasize = size_;
            }
		}
		target_->recv(p);
	}
	idle();
}

void TdlDataUdpAgent::sendcontrol(int ctype, int nbytes, const char* flags)
{
    Packet *p;

    p = Packet::alloc();
    //if control message is message update
    if(ctype==1) {
        hdr_tdlmsgupdate* tdlh = hdr_tdlmsgupdate::access(p);
        hdr_cmn::access(p)->size() = TDL_UPDATE_MSG_LEN;
        hdr_cmn::access(p)->timestamp() = (u_int32_t)(Scheduler::instance().clock());

        tdlh->type = MSG_4;
        tdlh->newtype = MSG_0;
        tdlh->newbytes = 0;

        if(support_tdldata_) {
			hdr_ip* ih = hdr_ip::access(p);
			ih->daddr() = IP_BROADCAST;
			ih->prio_ = 15;
			if(flags)
				memcpy(tdlh, flags, sizeof(hdr_tdlmsgupdate));


		}


    } else if(ctype==2) {
        hdr_tdlnetupdate* tdlh = hdr_tdlnetupdate::access(p);
        hdr_cmn::access(p)->size() = TDL_UPDATE_NET_LEN;
        hdr_cmn::access(p)->timestamp() = (u_int32_t)(Scheduler::instance().clock());

        tdlh->type = MSG_5;
        tdlh->newnet = 1;


        if(support_tdldata_) {
			hdr_ip* ih = hdr_ip::access(p);
			ih->daddr() = IP_BROADCAST;
			ih->prio_ = 15;
			if(flags) // MM Seq Num is passed as flags
				memcpy(tdlh, flags, sizeof(hdr_tdlnetupdate));


		}


    }

    // define header and send to lower layer
    hdr_cmn* ch = hdr_cmn::access(p);
    ch->direction() = hdr_cmn::DOWN;
    ch->uid() = ctrlMsgCnt_++;
    if(ctype==1)
        ch->ptype() = PT_TDLMSGUPDATE;
    else if(ctype==2)
        ch->ptype() = PT_TDLNETUPDATE;
	ch->iface() = UNKN_IFACE.value();

	target_->recv(p);
    idle();
}
// Support Packet Re-Assembly
void TdlDataUdpAgent::recv(Packet* p, Handler*)
{
	hdr_ip* ih = hdr_ip::access(p);
	int bytes_to_deliver = hdr_cmn::access(p)->size()-TDL_UDP_HDR_LEN-20;  // must substract app+udp header and 20 bytes for IP header

	// if it is a tdl packet
	if(ih->prio_ == 15) {

		if(app_) {  // if tdl Application exists
			// re-assemble tdl Application packet if segmented

			hdr_tdldata* tdlh = hdr_tdldata::access(p);
			//printf("UDP receive packet %d with data part size %i and total msg size is %i from node with app ID %i\n",hdr_cmn::access(p)->uid(),tdlh->datasize,tdlh->messagesize,tdlh->appID);
			u_int8_t aIdx = tdlh->appID;
			if(tdlh->seq == asm_info[aIdx].seq)
				asm_info[aIdx].rbytes += tdlh->datasize;
			else {
				asm_info[aIdx].seq = tdlh->seq;
				asm_info[aIdx].tbytes = tdlh->messagesize;
				asm_info[aIdx].rbytes = bytes_to_deliver;
			}
			// if fully reassembled, pass the packet to application
			if(asm_info[aIdx].tbytes <= asm_info[aIdx].rbytes) {
				//printf("receive message type %d\n",tdlh->type);
				hdr_tdldata tdlh_buf;

				memcpy(&tdlh_buf, tdlh, sizeof(hdr_tdldata));
				app_->recv_msg(tdlh_buf.nbytes, (char*) &tdlh_buf);
			}
		}
		Packet::free(p);
	}
	// if it is a normal data packet
	else {
		if (app_) app_->recv(bytes_to_deliver);
		Packet::free(p);
	}
}

