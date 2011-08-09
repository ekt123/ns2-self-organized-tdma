/*
tdl_data_msg.cc
Note: constructure and function definition for tdl_data_msg class
Usage: generator of TDL messages
*/
#include "tdl_data_msg.h"
#include <string.h>
#include "tclcl.h"
#include <stddef.h>
#include <iostream>
#include <sstream>
#include <fstream>

// OTcl linkage class
static class TdlDataAppClass : public TclClass {
 public:
  TdlDataAppClass() : TclClass("Application/TdlDataApp") {}
  TclObject* create(int, const char*const*) {
    return (new TdlDataApp);
  }
} class_tdl_data_app;


// When snd_timer_ expires call send_tdldata_msg
void SendTimer::expire(Event*)
{
  t_->send_tdldata_msg();
}

// Constructor (also initialize instances of timers)

static int TDLAppIndex = 0;     // keep track of application index

TdlDataApp::TdlDataApp() : running_(0), snd_timer_(this)
{
  appIdx_ = TDLAppIndex++;
  bind("pktsize_", &pktsize_);
  bind("msgType_", &msgType_);
  bind("netID_", &netID_);

}

// OTcl command interpreter
int TdlDataApp::command(int argc, const char*const* argv)
{
  Tcl& tcl = Tcl::instance();

  if (argc == 3) {
    if (strcmp(argv[1], "attach-agent") == 0) {
        agent_ = (Agent*) TclObject::lookup(argv[2]);
        if (agent_ == 0) {
        tcl.resultf("no such agent %s", argv[2]);
        return(TCL_ERROR);
      }

    // Make sure the underlying agent support TDL application
    if(agent_->supportTdlData()) {
        agent_->enableTdlData();
    }
    else {
        tcl.resultf("agent \"%s\" does not support TDL data message", argv[2]);
        return(TCL_ERROR);
    }

    agent_->attachApp(this);
    return(TCL_OK);
    }

    // Handle Change net command
    if(strcmp(argv[1],"change-net")==0) {
        if(atoi(argv[2]) <= 0)
  	  	  	  return(TCL_ERROR);
  	  	netID_ = atoi(argv[2]);
  	  	send_netupdate_msg();
  	  	return(TCL_OK);
    }

  }

  if(argc == 4) {
      // Handle Change message type and size command
  	  if(strcmp(argv[1], "change-message")==0) {
  	  	  if(atoi(argv[2]) <= 0 || atoi(argv[3]) <= 0)
  	  	  	  return(TCL_ERROR);

  	  	  msgType_ = atoi(argv[2]);     //change message type according to command
  	  	  pktsize_ = atoi(argv[3]);     //change message size according to command
  	  	  send_msgupdate_msg();         //send message update request to notify MAC sublayer
  	  	  return(TCL_OK);
  	  }
  }


  return (Application::command(argc, argv));
}

// Execute when tdl data application is created
void TdlDataApp::init()
{
	seq_ = 0;
}
// Execute when tdl data application starts
void TdlDataApp::start()
{
    running_ = 1;           // set running flag
    send_tdldata_msg();     // start sending message with specied interval
}
// Execute when tdl data application stops
void TdlDataApp::stop()
{
    running_ = 0;           // set running flag
}

void TdlDataApp::send_tdldata_msg()
{
	hdr_tdldata tdlh;       // tdl message header

	if(running_) {
	    // set message generation rate according to message type
	    if(msgType_ == 1) {
            tdlh.type = MSG_1;
            interval_ = 10.0;
	    }
        if(msgType_ == 2) {
            tdlh.type = MSG_2;
            interval_ = 2.0;
        }
        if(msgType_ == 3) {
            tdlh.type = MSG_3;
            interval_ = 2.0;
        }

		tdlh.nbytes = pktsize_;                         //payload size
		tdlh.messagesize = pktsize_+TDL_APP_HDR_LEN;    //message size
		tdlh.datasize = 0;                              //data size in a datagram
		tdlh.seq = seq_++;                              //message sequence
		tdlh.appID = appIdx_;                           //application index
		tdlh.time = Scheduler::instance().clock();      //timestamps
		agent_->sendmsg(pktsize_+TDL_APP_HDR_LEN,(char*) &tdlh); //call send message function in transport class

		snd_timer_.resched(interval_);
	}
}

// Handle message update
void TdlDataApp::send_msgupdate_msg()
{
    hdr_tdlmsgupdate tdlh;          //message update request header

    stop();                         //halt transmission

    tdlh.type = MSG_4;              //set message type of message update request
    // Specify new type of message
    if(msgType_ == 1)
        tdlh.newtype = MSG_1;
    else if(msgType_ == 2)
        tdlh.newtype = MSG_2;
    else if(msgType_ == 3)
        tdlh.newtype = MSG_3;
    else
        tdlh.newtype = MSG_0;

    tdlh.newbytes = pktsize_;
    printf("#### A node with app ID %i change message to type %i and size %i\n",appIdx_,msgType_,pktsize_);
    agent_->sendcontrol(1,TDL_UPDATE_MSG_LEN,(char*) &tdlh); //call send control message function in transport class
    start();                        // resume transmission

}

// Handle net update. Not used in this simulation
void TdlDataApp::send_netupdate_msg()
{
    hdr_tdlnetupdate tdlh;

    stop();

    tdlh.type = MSG_5;
    tdlh.newnet = netID_;
    printf("#### A node with app ID %i change net to %i\n",appIdx_,netID_);
    agent_->sendcontrol(2,TDL_UPDATE_NET_LEN,(char*) &tdlh);
    start();

}

// Receive message from underlying transport agent
void TdlDataApp::recv_msg(int nbytes, const char *msg)
{
  Scheduler &s = Scheduler::instance();

  // print out received message information
  if(msg) {
    hdr_tdldata* tdlh = (hdr_tdldata*) msg;
      printf("A node with app ID %i receive tdl message from node with app ID %i at time %f\n", appIdx_, tdlh->appID,s.clock());
      account_recv_pkt_info(tdlh);  // record received message

    }

}

void TdlDataApp::account_recv_pkt_info(const hdr_tdldata *tdlh)
{
	double local_time = Scheduler::instance().clock();

	// Calculate Delay
	if(tdlh->seq == 0) {
	init_recv_pkt_info();

	}
	else
	recv_p_info.delay = local_time - tdlh->time;

	// Count Received packets and Calculate Packet Loss
	recv_p_info.recv_pkts ++;
	recv_p_info.lost_pkts += (tdlh->seq - recv_p_info.last_seq - 1);
	recv_p_info.last_seq = tdlh->seq;

    // record to trace files
	if((MsgType)tdlh->type == MSG_1) {
        char out[100];
        sprintf(out, "recordRAP %i %f %i %i %i", appIdx_,Scheduler::instance().clock(),tdlh->appID,tdlh->seq,tdlh->nbytes);
        Tcl& tcl = Tcl::instance();
        tcl.eval(out);
	} else if((MsgType)tdlh->type == MSG_2) {
        char out[100];
        sprintf(out, "recordRadarTrack %i %f %i %i %i", appIdx_,Scheduler::instance().clock(),tdlh->appID,tdlh->seq,tdlh->nbytes);
        Tcl& tcl = Tcl::instance();
        tcl.eval(out);
	} else if((MsgType)tdlh->type == MSG_3) {
        char out[100];
        sprintf(out, "recordPosReport %i %f %i %i %i", appIdx_,Scheduler::instance().clock(),tdlh->appID,tdlh->seq,tdlh->nbytes);
        Tcl& tcl = Tcl::instance();
        tcl.eval(out);
	}
}

void TdlDataApp::init_recv_pkt_info()
{
  recv_p_info.last_seq = -1;
  recv_p_info.lost_pkts = 0;
  recv_p_info.recv_pkts = 0;
}


