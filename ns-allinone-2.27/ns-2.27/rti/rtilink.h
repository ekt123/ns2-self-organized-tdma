// Definition of RTILink
// Used for distributed ns simulations
// George F. Riley, Georgia Tech, Fall 1998

#ifndef __RTILINK_H__
#define __RTILINK_H__

#include <vector>

#include "delay.h"
#include "rtirouter.h"

class RTILink : public LinkDelay {
  public :
    RTILink();
    void recv(Packet* p, Handler*);  // Receive from local system
    int command(int, const char*const*);
  private :
    NsObject* rtarget_;  // DEBUG! local target
    double bandwidth_;	 /* bandwidth of underlying link (bits/sec) */
    double delay_;	 /* line latency */
    Event intr_;
    RTI_ObjClassDesignator    ObjClass;     /* Class for messages from peers */
    RTI_ObjInstanceDesignator ObjInstance;  /* Instance of above */
    int   off_ip_;
    // Private functions
    void CreateGroup(const char*, const char*, const char*);
    void PublishGroup(const char*, const char*, const char*);
    void JoinGroup(const char*, const char*, const char*);
public:
    // Define the wheremessage callback
    static char* WhereMessage( long, void*, long);
    static void  FreeMessage(char*);
    typedef vector<char*> FreeVec_t;
    static  FreeVec_t FreeVec;
};
#endif

