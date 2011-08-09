// Definition of RTIRouting agent.
// Used for distributed ns simulations
// George F. Riley, Georgia Tech, Fall 1998

#ifndef __RTIROUTER_H__
#define __RTIROUTER_H__

#include <object.h>
#include <rti/hdr_rti.h>

#include <map>

// Definitions for passthrough route map
typedef struct {
  ipaddr_t     mask;      // Mask for routing info
  ipaddr_t     rlink;     // IPAddr of rlink used to forward packets
  Agent*       pObj;      // NSObject for rtirouter
  ns_addr_t    addr;      // ns address for rtirouter object
  ipaddr_t     srcip;     // ALFRED add source ip/mask
  ipaddr_t     smask;
} PTRoute_t;

// Define a structure for the nodelist cache.
typedef pair<ipaddr_t, ipportaddr_t> NCKey_t;  // Key for the nodecache
typedef pair<int,int>                NCVal_t;  // Value for the nodecache
typedef map<NCKey_t, NCVal_t>        NCMap_t;  // Map for nodecache
typedef NCMap_t::value_type          NCPair_t; // Value Pair

// Define structures for the routing entries
typedef struct ROUTE_ENTRY* RPtr;
typedef struct ROUTE_ENTRY {
  ipaddr_t       ipaddr;  // Ultimate target ip address(s)
  ipaddr_t       mask;    // Mask for route aggregations
  NsObject*      pHead;   // ns object to forward packet to
  // ALFRED add source-based routing support
  ipaddr_t       srcip;
  ipaddr_t       smask;
} RouteEntry;

class RTIRouter : public Agent {
  public :
    RTIRouter();
    ~RTIRouter();
    int command(int, const char*const*);
    void recv(Packet* p, Handler*);  // Receive from local system
    void rrecv(Packet* p, Handler*); // Receive from remote system
    // Static methods for managing passthrough routes
    static void       AddPTRoute(ipaddr_t targ, ipaddr_t mask, ipaddr_t via,
                                 ipaddr_t srcip, ipaddr_t smask);
    static PTRoute_t* GetPTRoute(ipaddr_t targ, ipaddr_t srcip);
 private:
    // Private members
    NCMap_t    nodeCache;        // Cache of nodes
    int        nodeshift_;       // Nodeshift value from Simulator
    RPtr       rTable_;          // Routing table
    int        rTableSize_;      // Size of Routing table
    int        rTableLength_;    // Number used entries in table
    NsObject*  pDefaultRemoteRoute; // Points to head of ns object list
                                    // For off-system routing defaults
 private:
    // Private member functions
    NCVal_t   FindCache( ipaddr_t, ipportaddr_t);
    NCVal_t   AddCache(  ipaddr_t, ipportaddr_t, short, short);
    NsObject* Lookup( ipaddr_t, ipaddr_t );
    void      AddRoute( ipaddr_t, NsObject*, ipaddr_t, ipaddr_t, ipaddr_t );
    void      SetDefaultRemoteRoute(NsObject*);
};

#endif

