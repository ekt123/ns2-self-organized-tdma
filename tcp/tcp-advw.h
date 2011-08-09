/*-------------------------------------------------------------------------*/
/* AdvwTcpAgent: subclass of FullTcpAgent that has receiver advertised     */
/* window and application-limited consumption on receiver                  */
/* Author: Qi He <http://www.cc.gatech.edu/~qhe> 01Aug2003                 */
/* $Revision:$ $Name:$ $Date:$                                             */
/*-------------------------------------------------------------------------*/
#ifndef NS_TCP_ADVW
#define NS_TCP_ADVW

#include "tcp-full.h"
#include "packet.h"
#include "app.h"

class AdvwTcpApplication;

class AdvwTcpAgent: public FullTcpAgent {
public:
	AdvwTcpAgent();
	~AdvwTcpAgent();

	int rcv_buff_; //receiver buffer limit
	int num_bytes_req_; //number of bytes requested by user so far
	int num_bytes_avail_; //number of bytes in the receiver buffer
	int infinite_rcv_;	//whether the app will receive infinitely
	int rcv_wnd_;	//available receiver buffer (rcv_buff_ - not consumed)
        AdvwTcpApplication *app_;	

	// target->recv(), called by node classifier
	virtual void recv(Packet *pkt, Handler*); 
	// app_ blocking receive (may fail if data not available)
	void tcp_command_block_receive(int num_bytes); 
	// app_ nonblocking receive
	void tcp_command_nonblock_receive(int num_bytes);

	void sendpacket(int seqno, int ackno, int pflags, int datalen, int reason);	
	virtual void attachApp(Application* app);
 protected:

	void newack(Packet *);
	virtual void recvBytes(int);
	int command(int argc, const char*const* argv);
};

// application using AdvwTcpAgent, has to provide upcalls
class AdvwTcpApplication: public Application {
public:
  AdvwTcpApplication();
  ~AdvwTcpApplication();

  //these are placeholders for upcalls from the Agent when connection 
  //status changes
  virtual int upcall_recv(Packet *);	//packet available
  virtual int upcall_recv(int nbytes) { return nbytes;} //number of bytes available
  virtual void upcall_send();	//socket ready for sending	
  virtual void upcall_closing() ; //socket is being closed

protected:
  int command(int argc, const char*const* argv);
  AdvwTcpAgent *awagent_;	//agent_ cast as AdvwTcpAgent

};

#endif
