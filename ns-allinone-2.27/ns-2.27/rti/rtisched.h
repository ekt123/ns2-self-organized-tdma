/* RTI Scheduler */
/* George F. Riley.  Georgia Tech College of Computing */

/* Part of the ns modifications to allow distributed simulations */
/* RTI Scheduler is subclassed from map scheduler, but uses */
/* RTIKIT to synchronize the timestamps for messages and */
/* calls RTIKIT_Tick periodically to process incomming messages */

#ifndef __RTISCHED_H__
#define __RTISCHED_H__

#include "scheduler-map.h"

#include <vector>

class RTIScheduler : public MapScheduler {
public:
	RTIScheduler();
	virtual int init(int, const char*const*);
	static RTIScheduler& rtiinstance() { return (*rtiinstance_); }
	virtual void run();
  void setclock( double ); // Set new clock value
	int command(int argc, const char*const* argv);
  static char* WhereMessage (long, void*, long); // Used for oob messags
  static void  FreeMessage(char*);
  typedef vector<char*> FreeVec_t;
  static  FreeVec_t FreeVec;

protected:
	static RTIScheduler* rtiinstance_;
  int pd_; // ALFRED pdns debug env var
  void AddIPRoute(ipaddr_t ipaddr, ipaddr_t mask, Agent *pAgent, 
      ipaddr_t srcip, ipaddr_t smask);
  //Agent *GetIPRoute(ipaddr_t ipaddr, ipaddr_t srcip);
};

#endif
