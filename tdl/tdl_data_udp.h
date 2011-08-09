/*
tdl_data_udp.h
Note: header file for tdl_data_udp class
Usage: transport agent for TDL application
*/
#ifndef ns_tdl_data_udp_h
#define ns_tdl_data_udp_h

#include "agent.h"
#include "trafgen.h"
#include "packet.h"


enum MsgType {
    MSG_0 = 0x0,    // Max  message
	MSG_1 = 0x1,    // RAP
	MSG_2 = 0x2,    // Radar Track
	MSG_3 = 0x3,    // Position Report
	MSG_4 = 0x4,    // Message Changing
	MSG_5 = 0x5     // Net Switching

};
// Data message + UDP header
struct hdr_tdldata {
    // App header
	MsgType type;       // message type
	int nbytes;         // total size of payload

    // UDP header
    u_int8_t appID;     // indicate src app ID
    double time;        // timestamp
    int seq;            // message sequence
    int datasize;       // data part size of a datagram
    int messagesize;    // message payload + app header size

	//Header access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static hdr_tdldata* access(const Packet* p) {
		return (hdr_tdldata*) p->access(offset_);
	}
};
//Message Update message header
struct hdr_tdlmsgupdate {
	MsgType type;       // message type
	MsgType newtype;    // new message type
	int newbytes;       // new message size

	//Header access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static hdr_tdlmsgupdate* access(const Packet* p) {
		return (hdr_tdlmsgupdate*) p->access(offset_);
	}
};

//Net Update message header. Not used in this simulation
struct hdr_tdlnetupdate {
	MsgType type;       // message type
	u_int8_t newnet;    // new net id

	//Header access metods
	static int offset_;
	inline static int& offset() { return offset_; }
	inline static hdr_tdlnetupdate* access(const Packet* p) {
		return (hdr_tdlnetupdate*) p->access(offset_);
	}
};

#define TDL_APP_HDR_LEN 5       // TDL data message header length
#define TDL_UPDATE_MSG_LEN 5    // TDL message update header length
#define TDL_UPDATE_NET_LEN 2    // TDL net update header length
#define TDL_UDP_HDR_LEN 13      // UDP header is 18 bytes
// Used for Re-assemble segmented (by UDP) tdl packet
struct asm_tdldata {
    u_int8_t appID;
	int seq;     // tdl message sequence number
	int rbytes;  // currently received bytes
	int tbytes;  // total bytes to receive for tdl packet
};

// TdlDataUdpAgent Class definition
class TdlDataUdpAgent : public Agent {
public:
	TdlDataUdpAgent();
	TdlDataUdpAgent(packet_t);
	virtual int supportTdlData() { return 1; }
	virtual void enableTdlData() { support_tdldata_ = 1; }
	virtual void sendmsg(int nbytes, const char *flags = 0);
	virtual void sendcontrol(int ctype, int nbytes, const char *flags = 0);
	void recv(Packet*, Handler*);
protected:
	int support_tdldata_; // set to 1 if above is TdlDataApp
	int seqno_;
	int ctrlMsgCnt_;
	u_int8_t agent_ID_;
private:
	asm_tdldata asm_info[16]; // packet re-assembly information
};

#endif
