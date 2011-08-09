/*-------------------------------------------------------------------------*/
/* SocketTcp: subclass of AdvwTcpAgent that implements the services        */
/* provided by real system TCP socket, e.g.,                               */
/*      limited sender buffer,                                             */
/*      spawning new connections on request,                               */
/*      real payload transfer.                                             */ 
/* This is a backend of the NSSocket interface				   */
/* Author: Qi He <http://www.cc.gatech.edu/~qhe> 01Aug2003                 */
/* $Revision:$ $Name:$ $Date:$                                             */
/*-------------------------------------------------------------------------*/
#ifndef __NS_SOCKTCP
#define __NS_SOCKTCP
#include "tcp-advw.h"
#include "nilist.h"

class NSSocket;
class QueSocket;
class PrioSocket;

// packet buffer (send/recv) maintained by an agent
class PktDataEntry: public slink {
 public:
  PktDataEntry(int, PacketData *);
  
  PacketData *pktdata_;
};

// the backend TCP agent for NSSocket
class SocketTcp: public AdvwTcpAgent {
  friend NSSocket;
  friend QueSocket;
  friend PrioSocket;
public:
	SocketTcp();
	
	int snd_wnd_;	// current congestion window 
	int listen_only_; //is the socket used for listen() and accept() only
	int max_conn_; //maximum number of conns to be accepted, set by listen() 
	Islist<PktDataEntry> rcv_buf_; //socket receive buffer
	Islist<PktDataEntry> snd_buf_; //socket send buffer
	// insert into the rcv_buf_, like Reassembly Queue
	void insert(Islist<PktDataEntry>*, PktDataEntry *); 
	// packet handler
	void recv(Packet *, Handler *);

	// internal functions
	virtual int r_send(int bytes);
	virtual int r_send(PacketData *);
	virtual int r_sendmsg(int nbytes, const char *flags=0);
	virtual int r_sendmsg(PacketData *p, const char *flags=0);
	void sendpacket(int seqno, int ackno, int pflags, int datalen, PacketData *data, int reason);
	virtual int r_advance_bytes(int nb);
	virtual int r_advance_pkt(PacketData *p);
	void ack_syn(Packet *);

	// APIs to Application
	virtual int send_dummy(int nbytes);
	int sendmsg(PacketData *, int len);
	void listen(int max);
 protected:
	void recvBytes(int);
	void newack(Packet *);
	void output(int seqno, int reason = 0);
	int command(int argc, const char*const* argv);


};

#endif
