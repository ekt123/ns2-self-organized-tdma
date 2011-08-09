// New agent by George Riley.  Used to allow running ns simulations in 
// parallel.  Lets a simulation on one system be broken into several
// simulations on several systems

// George F. Riley. Georgia Tech.
// Fall 1998

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "tclcl.h"
#include "packet.h"
#include "ip.h"
#include "rti/hdr_rti.h"
#include "agent.h"
#include "scheduler.h"

#include <map>

#define USE_NIX
#ifdef USE_NIX
#include "nix/hdr_nv.h"
#include "nix/nixnode.h"
#endif

#include "rti/rtirouter.h"
#include "rti/rtioob.h"

// ALFRED - add node manip support
#include "node.h"

// Defines for passthrough routing management
// RouteMap is destination IP/PTROute_t*
// ALFRED change map to multimap due to source-based
typedef multimap<ipaddr_t, PTRoute_t*> PTRouteMap_t;
typedef PTRouteMap_t::iterator    PTRouteMap_it;
typedef PTRouteMap_t::value_type  PTRouteMap_pt;

static PTRouteMap_t ptRoutes; // Map of passthrough routes

extern char         hn[255]; /* Hostname, defined in rtisched1.cc */
#define RTIKIT_nodeid FM_nodeid
extern unsigned long FM_nodeid;

// Prototypes in rtisched.cc
NsObject* GetLocalIP(ipaddr_t ipaddr);
Agent *GetIPRoute(ipaddr_t ipaddr, ipaddr_t srcip);

static class RTIRouterClass : public TclClass {
 public:
	RTIRouterClass() : TclClass("Agent/RTIRouter") {}
	TclObject* create(int, const char*const*) {
		return (new RTIRouter());
	}
} class_RTI_agent;

static double Now()
{ // debug
  return Scheduler::instance().clock();
}

RTIRouter::RTIRouter() : Agent(/*XXX*/PT_MESSAGE)
{
 rTable_  = NULL;
 rTableSize_   = 0; // No routes yet
 rTableLength_ = 0; // No routes yet
 pDefaultRemoteRoute = NULL;
}

RTIRouter::~RTIRouter() 
{
}

int RTIRouter::command(int argc, const char*const* argv)
{
NsObject*  pHead;
ipaddr_t   ipaddr;
ipaddr_t   targmask;

  if(0){printf("RTIRouter command cnt %d ", argc);
    for(int i=0;i<argc;i++)printf("%d - %s ", i, argv[i]);printf("\n");}
  if (argc == 3) 
    {
      if (strcmp(argv[1], "add-default-remote-route") == 0)
        {
          pHead = (NsObject*)TclObject::lookup(argv[2]);
          if (pHead == NULL)
            {
              printf("ADRR - remote target %s not found\n", argv[2]);
              exit(1);
            }
          SetDefaultRemoteRoute(pHead);
          return(TCL_OK);
        }
    }
  // ALFRED modify add-route for source-based support
  if (argc == 7) {
    if (strcmp(argv[1], "add-route") == 0) {
      struct in_addr addr;
      ipaddr_t srcip, smask;
      if (strchr(argv[4], '.') != NULL) { // Dotted notation
        inet_aton(argv[4], &addr);
        ipaddr = ntohl(addr.s_addr);
      } else {
        ipaddr = strtol(argv[4], NULL, 0);
      }
      if (strchr(argv[2], '.') != NULL) { // Dotted notation
        inet_aton(argv[2], &addr);
        srcip = ntohl(addr.s_addr);
      } else {
        srcip = strtol(argv[2], NULL, 0);
      }
      smask = atol(argv[3]);
      targmask = atol(argv[5]);
      pHead = (NsObject*)TclObject::lookup(argv[6]);
      if(0)printf("Adding route %s %s %s %s\n", argv[2], argv[3], 
          argv[4], argv[5]);
      if (pHead == NULL) {
        printf("add-route cannot find object %s\n", argv[6]);
        exit(1);
      }
      AddRoute(ipaddr, pHead, targmask, srcip, smask);
      return(TCL_OK);
    }
    if (strcmp(argv[1], "add-route-passthrough") == 0) {
      ipaddr_t srcip, smask, rlinkip;
      struct in_addr addr;
      if (strchr(argv[2], '.') != NULL) { // Dotted notation
        inet_aton(argv[2], &addr);
        ipaddr = ntohl(addr.s_addr);
      } else {
        ipaddr = strtol(argv[2], NULL, 0);
      }
      if (strchr(argv[3], '.') != NULL) { // Dotted notation
        inet_aton(argv[3], &addr);
        targmask = ntohl(addr.s_addr);
      } else {
        targmask = strtol(argv[3], NULL, 0);
      }
      if (strchr(argv[4], '.') != NULL) { // Dotted notation
        inet_aton(argv[4], &addr);
        srcip = ntohl(addr.s_addr);
      } else {
        srcip = strtol(argv[4], NULL, 0);
      }
      if (strchr(argv[5], '.') != NULL) { // Dotted notation
        inet_aton(argv[5], &addr);
        smask = ntohl(addr.s_addr);
      } else {
        smask = strtol(argv[5], NULL, 0);
      }
      if (strchr(argv[6], '.') != NULL) { // Dotted notation
        inet_aton(argv[6], &addr);
        rlinkip = ntohl(addr.s_addr);
      } else {
        rlinkip = strtol(argv[4], NULL, 0);
      }
      if(0)printf("%s cmd %s %s %s %s\n",
                      name(), argv[1], argv[2], argv[3], argv[4]);
      if(0)printf("addroute-pt dstip %08x mask %08x rlinkip %08x\n",
                      ipaddr, targmask, rlinkip);
      AddPTRoute(ipaddr, targmask, rlinkip, srcip, smask);
      return(TCL_OK);
    }
  }
  return (Agent::command(argc, argv));
}

void RTIRouter::recv(Packet* p, Handler* h)
{

  NsObject *pHead, *pNode;
  hdr_rti *rti = hdr_rti::access(p);
  
  if(0)printf("%s RTIRouter::recv at %-8.5f, srcip %08x, dst %08x:%d\n", 
      name_, Now(), rti->ipsrc(), rti->ipdst(), rti->ipdstport());
  // ALFRED fixed this with GetLocalIP()
  if (rti->ipdst() != 0 && rti->ipdstport() != -1) { 
    pNode = GetLocalIP(rti->ipdst());
    if (pNode == NULL) { 
      // Not a local address, forward to another sysetm via rti
      if(0)printf("%s Forwarding non-local packet to %s\n", name(), \
          target_->name());
      // ALFRED add src for Lookup below
      pHead = Lookup(rti->ipdst(), rti->ipsrc());
      if (pHead == NULL) {
        printf("FATAL: %s no route to %08x\n", name(), rti->ipdst());
        exit(0);
      }
      // Forward it on to the ttl-queue-rlink chain
      if(0)printf("%s routing dst %08x to %s\n", name(), \
          rti->ipdst(), pHead->name());
      pHead->recv(p, h);
    } else {
      // First check if we have a local->remote->local situation
      Agent *tmp = GetIPRoute(rti->ipdst(), rti->ipsrc());
      if (tmp == NULL) {
        rrecv(p, h);
      } else {
        // local->remote->local fix
        pHead = Lookup(rti->ipdst(), rti->ipsrc());
        if (pHead == NULL) {
          printf("FATAL: %s no route to %08x\n", name(), rti->ipdst());
          exit(0);
        }
        pHead->recv(p, h);
      }
    }
  } else {
    rrecv(p, h);
  }
}

static ipportaddr_t TryToCreate(ipaddr_t ipaddr, ipaddr_t daddr,
                                ipportaddr_t dport, int instid,
                                unsigned long ep1, unsigned long ep2)
{
Tcl& tcl = Tcl::instance();
static int firstpass = 1;
static int exists = 0;
int r;
int port = 0;

 if (firstpass) 
   { // See if the create-and-bind proc exists
     tcl.evalf("lsearch -exact [info procs] create-and-bind");
     tcl.resultAs(&r);
     if (r >= 0)
       { // Ok found it
         if(0)printf("create-and-bind exists\n");
         exists = 1;
       }
     else
       { // No create-and-bind, just ignore
         if(0)printf("create-and-bind NOT FOUND\n");
       }
     firstpass = 0;
   }
 if (exists)
   {
     tcl.evalf("create-and-bind 0x%08x 0x%08x %d %d %d",
               ipaddr, daddr, dport, ep1, ep2);
     tcl.resultAs(&port);
     if(0)printf("The bound port is %d\n", port);
   }
 return port;
}

static ipportaddr_t FindDest(ipaddr_t ipaddr, ipaddr_t daddr,
                                ipportaddr_t dport)
{ // On local node ipaddr, find agent with dest addr/port specified,
  // return src port
ipportaddr_t port = -1;
char     work[255];

  NsObject* pNode = GetLocalIP(ipaddr);
  if (pNode)
    { // Node is found, now find the agent (in tcl)
      Tcl& tcl = Tcl::instance();
      sprintf(work, "0x%08x", daddr);
      tcl.evalf("%s find-srcport %s %d", pNode->name(), work, dport);
      tcl.resultAs((int*)&port);
    }
  return port;
}

void SendOOB(int t, int da, int dp, int sa, int sp,
             unsigned long ep1, unsigned long ep2,
             unsigned long ep3, unsigned long ep4); // See rtisched.cc

void RTIRouter::rrecv(Packet* p, Handler*)
{
int           r;
ipaddr_t      ipaddr;
ipportaddr_t  dport, newdport;
NCVal_t       Cache;
char*         pRes;
int           nodeid;
int           portid;
int           IntID;
char          nsnodename[255];
char          nsagentname[255];
bool          no_cache = false; // ALFRED
Tcl&          tcl = Tcl::instance();

  hdr_rti*  rti = hdr_rti::access(p);
  hdr_ip*   ip =  hdr_ip::access(p);
  hdr_cmn*  hc =  hdr_cmn::access(p);

  if(0)printf("%s RTIRouter::rrecv at %-8.5f, srcip %08x, dst %08x:%d\n", 
      name_, Now(), rti->ipsrc(), rti->ipdst(), rti->ipdstport());
  if (rti->RTINodeId() == RTIKIT_nodeid) {
    if(0)printf("rrecv: ignoring own packet %ld\n", RTIKIT_nodeid);
    Packet::free(p);
    return;
  }
  ipaddr = rti->ipdst();
  dport = rti->ipdstport();

  if(0)printf("rrecv dstip %08x dstport %d\n", ipaddr, dport);
  if (dport == 0)
    { // This is special case for connecting to remote agents at port 0
      // The code will try to allocate and bind an agent to the specified
      // ip address
      //      dport = TryToCreate(ipaddr, rti->ipsrc(), rti->ipsrcport(),
      //                    hc->instance_id(), 0, 0);
      newdport = FindDest(ipaddr, rti->ipsrc(), rti->ipsrcport());
      // Might return -1 if passthrough
      if (newdport >= 0)
        {
          dport = newdport;
          if(0)printf("Finddest found port %d\n", dport);
          rti->ipdstport() = dport; // Use the newly creatd one
          // And inform the originator
          if(0)printf("SendingOOB, SetDestPort, dport %d\n", dport);
          SendOOB(RTIM_SetDestPort, ipaddr, dport, rti->ipsrc(),
                  rti->ipsrcport(),
                  0, 0, 0, 0);
        }
    }
  if(0)printf("%s %f: received pkt from remote, src %08x:%d dst %08x:%d\n", 
      name(), Now(), rti->ipsrc(), rti->ipsrcport(), ipaddr, dport);
  // Try to find in cache
  Cache = FindCache(ipaddr, dport);
  if (Cache.first == -1)
    {
      // Not in cache, lookup (slower)
      Node *pNode = (Node*)GetLocalIP(ipaddr);
      if (!pNode)
        { // Can't find target ip addr, must be pass through
          Agent* pAgent = NULL;
          if(0)printf("Can't find targ for ip %08x, assuming passthrough\n", 
              ipaddr);
          // First see if we have a passthrough routing entry
          // ALFRED always use current IP address instead source
          //PTRoute_t* pt = GetPTRoute(ipaddr, rti->ipsrc());
          PTRoute_t* pt = GetPTRoute(ipaddr, my_ipaddr_);
          if (pt) { 
            if(0)printf("Found pt route for %08x\n", ipaddr);
            pAgent = pt->pObj;
          } else {
            // Look for a default
            // ALFRED print warning message
            printf("RTIRouter(%s): Can't find ptroute for %08x!\n", 
                name(), ipaddr);
            printf("  Warning! Using unsupported default-routes!\n");
            tcl.evalf("%s set node_", name());
            if(0)printf("Attached node is %s\n", tcl.result());
            tcl.evalf("%s default-route", tcl.result());
            if(0)printf("Default route is %s\n", tcl.result());
            pAgent = (Agent*)TclObject::lookup(tcl.result());
          }
          if (pAgent == NULL) {
            printf("No agent for default route on node %s\n", name());
            fflush(stdout);
            Packet::free(p);
            return;
          }
          // Add to cache
          Cache = AddCache(ipaddr, dport, pAgent->addr(), pAgent->port());
          // Redirect packet to rtirouter on default route
          ip->daddr() = pAgent->addr();
          ip->dport() = pAgent->port();
          // Now forward to target      
          if (target_ == NULL) {
            printf("%s NO TARGET ON RRECV!\n", name());
            return;
          }
          if(0)printf("Target is %s\n", target_->name());
        }
      else
        { // Is local
	        nodeid = pNode->nodeid();
          if(0)printf("Node id is %d\n", nodeid);
          tcl.evalf("%s findport %d", pNode->name(), dport);
          strcpy(nsagentname, tcl.result());
          if(0)printf("Target agent is %s\n", nsagentname);
          Agent* pAgent = (Agent*)TclObject::lookup(nsagentname);
          if (pAgent == NULL) {
            // ALFRED instead of dropping here, forward to end host droptarget
            //printf("No agent for port %d on node %s\n", dport, pNode->name());
            //Packet::free(p);
            //return;
            tcl.evalf("%s get-droptarget", pNode->name());
            pAgent = (Agent *)TclObject::lookup(tcl.result());
            strcpy(nsagentname, pAgent->name());
            // Bypass the IntID hacks below
            tcl.evalf("%s set dst_addr_ 0", nsagentname);
            tcl.evalf("%s set agent_port_", nsagentname);
            tcl.resultAs(&portid);
            if(0)printf("RTIRouter: valid node, no agent at port: connecting to droptarget(%s:%d)\n", nsagentname, portid);
            no_cache = true;
          } else {
            portid = pAgent->port();
            if(0)printf("Agent portid %d\n", portid);
            Cache = AddCache(ipaddr, dport, nodeid, portid); // Add to cache
          }
          // Verify that the target has a destination
          tcl.evalf("%s set dst_addr_", nsagentname);
          tcl.resultAs(&IntID);
          if(0)printf("NSAgentName %s IntId %d\n", nsagentname, IntID);
          if (IntID < 0)
            { // No dest on ultimate target, point back to me for reply
              // Is this always right????
              if(0)printf("MyAddr %08x\n", addr());
              if(0)printf("Setting stuff ofr agent %s, daddr %d dport %d\n",
                          nsagentname, addr(),port());
              tcl.evalf("%s set dst_addr_ %d", nsagentname, addr());
              tcl.evalf("%s set dst_port_ %d", nsagentname, port());
              tcl.evalf("%s set dst_ipaddr_ %d", nsagentname, rti->ipsrc());
              tcl.evalf("%s set dst_ipport_ %d", nsagentname, rti->ipsrcport());
            }
        }
    }
  // The the new destination, within this subnet
  // Time to change the dest addr to the real one and fordard to target
  if (no_cache == false) {
    ip->daddr() = Cache.first;
    ip->dport() = Cache.second;
  } else {
    ip->daddr() = nodeid;
    ip->dport() = portid;
  }
  if(0)printf("%s set dstaddr to %d dport %d\n", name(), ip->daddr(), ip->dport());
#ifdef USE_NIX
  // Check for NV Routing, and if so get/create a nix vector
  nsaddr_t myid = addr();
  hdr_nv* nv = hdr_nv::access(p);
  NixNode* pNixNode = NixNode::GetNodeObject(myid);
  if(0)printf("After GNO, myid %d pNix %p\n", myid, pNixNode);
  if (pNixNode)
    { 
      // If we get non-null, indicates nixvector routing in use
      // Delete any left over nv in the packet
      // Get a nixvector to the target (may create new)
      if(0)printf("RTI Creating NV from %ld to %ld\n",
             myid, ip->daddr());
      if(0)printf("Offset::rti %d Offset::nix %d\n",
             hdr_rti::offset_, hdr_nv::offset_);

      NixVec* pNv = pNixNode->GetNixVector(ip->daddr());
      pNv->Reset();
      if(0)pNv->DBDump();
      nv->nv() = pNv; // And set  the nixvec in the packet
      nv->h_used = 0; // And reset used portion to 0
    }
#endif
  // Now forward to target
  if (target_ == NULL)
    {
      printf("%s NO TARGET ON RRECV!\n", name());
      return;
    }
  if(0)printf("Target is %s\n", target_->name());
  
  // Deliver to the classifier for forwarding
  target_->recv(p, (Handler*)NULL);
}

// Private member functions
NCVal_t RTIRouter::FindCache(
    ipaddr_t      ipaddr,
    ipportaddr_t  port)
{
  NCMap_t::iterator i = nodeCache.find(NCKey_t(ipaddr, port));
  if (i == nodeCache.end())
    { // Can't find
      NCVal_t n(-1,-1);
      return n; // indicate not found
    }
  return i->second;
}

NCVal_t RTIRouter::AddCache(
    ipaddr_t      ipaddr, 
    ipportaddr_t  port, 
    short         nodeid, 
    short         portid)
{
  nodeCache.insert(NCPair_t(NCKey_t(ipaddr, port), NCVal_t(nodeid, portid)));
  return NCVal_t(nodeid, portid);
}

NsObject* RTIRouter::Lookup( // Find target for a specific ipaddr
    ipaddr_t addr, ipaddr_t srcip)
{
  int i;

  // ALFRED first loop through check for src matches, then dst only
  for (i = 0; i < rTableLength_; i++)
    {
      if(0)printf("SRC RTIRouter::Lookup %08x->%08x (%08x,%08x,%08x,%08x)\n",
          srcip, addr, rTable_[i].srcip, rTable_[i].smask, rTable_[i].ipaddr,
          rTable_[i].mask);
     if (rTable_[i].srcip != 0 && rTable_[i].smask != 0) {
      if ((rTable_[i].srcip & rTable_[i].smask) == (srcip & rTable_[i].smask)
          && (rTable_[i].ipaddr & rTable_[i].mask) == (addr & rTable_[i].mask))
        {
          if(0)printf("SRC Lookup found %08x at rtable %d %08x %08x\n",
                 addr, i, rTable_[i].ipaddr, rTable_[i].mask);
          return(rTable_[i].pHead); // Found it
        }
     }
    }
  for (i = 0; i < rTableLength_; i++) {
    if ((rTable_[i].ipaddr & rTable_[i].mask) == (addr & rTable_[i].mask))
        {
          if(0)printf("REG Lookup found %08x at rtable %d %08x %08x\n",
                 addr, i, rTable_[i].ipaddr, rTable_[i].mask);
          return(rTable_[i].pHead); // Found it
        }
  }
  if (rTableLength_ == 0)
    {
      if(0)printf("Lookup no routing table\n");
      return(pDefaultRemoteRoute); // NO route found, use default if exists
    }
  printf("RTIRouter(%s)::Lookup NO ROUTE found %08x->%08x!\n",
         name(), srcip, addr);
  // Use first as default
  return(rTable_[0].pHead);
}

void RTIRouter::AddRoute( // Add a new route entry
    ipaddr_t  ipaddr,     // IPAddress to add
    NsObject* pHead,      // ns object target
    ipaddr_t  targmask,   // Target mask for aggregation
    ipaddr_t  srcip,      // ALFRED add srcip/smask support
    ipaddr_t  smask)
{
  if(0){printf("RTIRouter::AddRoute, rt %p rtsize %d rtlength %d\n",
         rTable_, rTableSize_, rTableLength_);fflush(stdout);}
  if (rTableSize_ == rTableLength_)
    {
      rTableSize_+=100;
      rTable_ = (RPtr)realloc(rTable_, (rTableSize_+1)*sizeof(*rTable_));
      if(0){printf("After realloc rTable_ %p, size\n", rTable_, rTableSize_);
        fflush(stdout);}
    }
  rTable_[rTableLength_].ipaddr = ipaddr;
  rTable_[rTableLength_].mask   = targmask;
  rTable_[rTableLength_].pHead  = pHead;
  // ALFRED add source to data struct
  rTable_[rTableLength_].srcip  = srcip;
  rTable_[rTableLength_].smask  = smask;
  if(0)printf("%s added route to %08x via %s\n",
              name(), ipaddr, pHead->name());
  rTableLength_++;
  if (pDefaultRemoteRoute == NULL) pDefaultRemoteRoute = pHead; // Set default
}

void RTIRouter::SetDefaultRemoteRoute( // Add a new route entry
    NsObject* pHead)      // ns object target
{
  if(0)printf("Set default remote route %s\n", name());
  pDefaultRemoteRoute = pHead; // Set default
}

void RTIRouter::AddPTRoute(ipaddr_t targ, ipaddr_t mask, ipaddr_t via,
    ipaddr_t srcip, ipaddr_t smask)
{
ipaddr_t maskedtarg = targ & mask;
PTRouteMap_it it;

  if(0)printf("Adding PTRoute for targ %08x mask %08x\n", maskedtarg, mask);
  // ALFRED Allow "dupes" due to SRC based routing
  /*
  // First see if existing route
  it = ptRoutes.find(maskedtarg);
  if (it != ptRoutes.end())
    {
      printf("Ignoring duplicate pt route %08x\n", maskedtarg);
      return;
    }
  */
  NsObject* pNode = GetLocalIP(via);
  if (pNode == NULL)
    {
      printf("Can't find localip %08x on passthrough route, ignoring\n",
             via);
      return;
    }
  Tcl& tcl = Tcl::instance();
  if(0)printf("Found localip %08x node %s\n", via, pNode->name());
  tcl.evalf("%s set rtirouter_", pNode->name());

  Agent* pRTIRouter = (Agent*)TclObject::lookup(tcl.result());
  if (pRTIRouter == NULL)
    {
      printf("Can't find rtirouter on node %08x passthrough route, ignoring\n",
             via);
      return;
    }
  PTRoute_t* pt = new PTRoute_t;
  pt->mask = mask;
  pt->rlink = via;
  pt->addr.addr_ = pRTIRouter->addr();
  pt->addr.port_ = pRTIRouter->port();
  pt->pObj = pRTIRouter;
  // ALFRED add source stuff
  pt->srcip = srcip;
  pt->smask = smask;
  ptRoutes.insert(PTRouteMap_pt(maskedtarg, pt)); // Insert the new pt route
}

PTRoute_t* RTIRouter::GetPTRoute(ipaddr_t targ, ipaddr_t srcip)
{
PTRouteMap_it it;

  if(0)printf("entered GetPTRoute for %08x->%08x\n",srcip,targ);

// First see if 24 bit mask
  it = ptRoutes.find(targ & 0xffffff00);
  if (it != ptRoutes.end())
    {
      // ALFRED check src
      PTRoute_t *tmp = it->second; // Save first entry
      for (; it != ptRoutes.end(); it++) {
        if (it->second->srcip != 0 && it->second->smask != 0) {
          if ((it->second->srcip & it->second->smask) == 
              (srcip & it->second->smask)) {
            return it->second;
          }
        }
      }
      return tmp;
    }
// Next see if 16 bit mask
  it = ptRoutes.find(targ & 0xffff0000);
  if (it != ptRoutes.end())
    {
      // ALFRED check src
      PTRoute_t *tmp = it->second; // Save first entry
      for (; it != ptRoutes.end(); it++) {
        if (it->second->srcip != 0 && it->second->smask != 0) {
          if ((it->second->srcip & it->second->smask) == 
              (srcip & it->second->smask)) {
            return it->second;
          }
        }
      }
      return tmp;
    }
// Next see if 8 bit mask
  it = ptRoutes.find(targ & 0xff000000);
  if (it != ptRoutes.end())
    {
      // ALFRED check src
      PTRoute_t *tmp = it->second; // Save first entry
      for (; it != ptRoutes.end(); it++) {
        if (it->second->srcip != 0 && it->second->smask != 0) {
          if ((it->second->srcip & it->second->smask) == 
              (srcip & it->second->smask)) {
            return it->second;
          }
        }
      }
      return tmp;
    }
  // Try all (slower);
  for (it = ptRoutes.begin(); it != ptRoutes.end(); it++)
    {
      PTRoute_t* p = it->second;
      if (p->srcip != 0 && p->smask != 0) {
        if ((p->srcip & p->smask) == (srcip & p->smask)) {
          if ((targ & p->mask) == it->first) {
            return p;
          }
        }
      }
    }
  for (it = ptRoutes.begin(); it != ptRoutes.end(); it++) {
    PTRoute_t *p = it->second;
    if ((targ & p->mask) == it->first) {
      return p;
    }
  }
  return NULL; // No route found
}


