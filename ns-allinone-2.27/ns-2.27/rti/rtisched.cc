/* RTI Scheduler */
/* George F. Riley.  Georgia Tech College of Computing */

/* Part of the ns modifications to allow distributed simulations */
/* RTI Scheduler is subclassed from list scheduler, but uses */
/* RTIKIT to synchronize the timestamps for messages and */
/* calls RTIKIT_Tick periodically to process incomming messages */

#include <stdlib.h>
#include <math.h>
// libSynk/libbrti includes
extern "C" {
#include "brti.h"
}

//#define USE_BACKPLANE
//#undef  USE_BACKPLANE
#ifdef  USE_BACKPLANE
// Backplane includes
#include "backplane/dsim.h"
#include "backplane/dsimip.h"
#include "backplane/dsimtcp.h"
#endif

#ifndef WIN32
#include <sys/time.h>
#endif

#include "tclcl.h"
#include "config.h"
#include "scheduler-map.h"
#include "packet.h"
#include "agent.h"
#include "rti/rtisched.h"
#include "rti/rtilink.h"
#include "rti/rtirouter.h"

#undef  HDCF_IF
#ifdef  HDCF_IF
#include "fluidqueue.h"
#endif

#ifdef USE_COMPRESSION
#include "rti/rticompress.h"
#endif

#include "rtioob.h"

// STL Includes

#include <map>

typedef map<ipaddr_t, NsObject*> IPMap_t;
typedef IPMap_t::iterator        IPMap_it;
typedef IPMap_t::value_type      IPPair_t;

// ALFRED - ecc/ia64 fix
#define strtoll strtol

// ALFRED - pkt stats
#include "common/pktcount.h"
unsigned long long rlinkPktC;

IPMap_t IPMap;  // Map of ipaddresses vs. ns Node Object pointer

// ALFRED ip->rtirouter table
typedef struct _route_map_t {
  ipaddr_t ipaddr;
  ipaddr_t mask;
  Agent *pAgent;
  ipaddr_t srcip;
  ipaddr_t smask;
} route_map_t;
route_map_t *rmap_;
unsigned int rmap_size_;
unsigned int rmap_len_;
Agent *GetIPRoute(ipaddr_t ipaddr, ipaddr_t srcip);

void AddLocalIP(ipaddr_t ipaddr, NsObject* pObj)
{
IPPair_t* pp = new IPPair_t(ipaddr, pObj);

  if(0)printf("AddLocalIP, addr %08x objname %s\n", ipaddr, pObj->name()); 
  IPMap.insert(*pp);
}

NsObject* GetLocalIP(ipaddr_t ipaddr)
{
IPMap_it it;

  if(0)printf("RTISched::GetLocalIp %08x, size of map %d\n", ipaddr, IPMap.size()); 
  it = IPMap.find(ipaddr);
  if (it == IPMap.end()) return(NULL);  // Not found
  return it->second; // Found
}

const char* GetLocalIPName(ipaddr_t ipaddr)
{
IPMap_it it;

  it = IPMap.find(ipaddr);
  if (it == IPMap.end()) return(NULL);  // Not found
  return it->second->name(); // Found
}

unsigned long long evcount = 0; /* Debug..count events */ /* ALFRED - ull */
extern ULONG FM_nodeid;
extern ULONG FM_numnodes;
static  double lookahead;

static  RTI_ObjClassDesignator    ObjClass;   
static  RTI_ObjInstanceDesignator ObjInstance; 
#define OOB_GROUP_NAME "RTIOOB"

RTIScheduler::FreeVec_t RTIScheduler::FreeVec;

char* RTIScheduler::WhereMessage (/* Advise where to store Rx message */
    long         MsgSize,        /* Size of RX Message */
    void*        pNotUsed,       /* Context info */
    long         MsgType)        /* Type specified by sender */
{
char* r;
 if (FreeVec.size())
   { // entries exist in free vector
     r = FreeVec.back();
     FreeVec.pop_back(); // Remove it
     return r;
   }
 r = (char*)malloc(MsgSize + 16);
 return r;
}

void RTIScheduler::FreeMessage(
    char*         pMsg)
{
  FreeVec.push_back(pMsg);
}

// Create the group for exchanging out-of-band data
static void CreateOOBGroup()
{

  RTI_CreateClass(OOB_GROUP_NAME);
}

static void PublishOOBGroup()
{
  ObjClass = RTI_GetObjClassHandle(OOB_GROUP_NAME);
  if (ObjClass == NULL)
    {
      printf("Can't get objclasshandle for %s\n", OOB_GROUP_NAME);
      exit(0);
    }
  // Publish and register my interest in this class
  RTI_PublishObjClass(ObjClass);
  ObjInstance = RTI_RegisterObjInstance(ObjClass);
}

// Join the group for exchanging out-of-band data
static void JoinOOBGroup()
{
  if (! RTI_IsClassSubscriptionInitialized (ObjClass))
    { //  Set up a context
      RTI_InitObjClassSubscription (ObjClass, RTIScheduler::WhereMessage, NULL);
    }
  RTI_SubscribeObjClassAttributes(ObjClass);
}

void SendOOB(int t, int da, int dp, int sa, int sp,
             unsigned long ep1, unsigned long ep2,
             unsigned long ep3, unsigned long ep4)
{
char      work[255];
struct MsgS* pMsg = (struct MsgS*)&work;
int*         pMsgType = (int*)&pMsg[1];
RTIMsg_t* pm = (RTIMsg_t*)&pMsgType[1];

  pMsg->TimeStamp = Scheduler::instance().clock() + lookahead;
  *pMsgType = 1; // Note OOB Data
  pm->t = (RTIMsgEnum_t)t;
  pm->da = da;
  pm->dp = dp;
  pm->sa = sa;
  pm->sp = sp;
  pm->ep1 = ep1;
  pm->ep2 = ep2;
  pm->ep3 = ep3;
  pm->ep4 = ep4;
  RTI_UpdateAttributeValues(ObjInstance, pMsg,
                            MSGS_SIZE(sizeof(*pm) + sizeof(int)),
                            0);
}
#ifdef HAVE_FILTER
void SendOOBFilter(int t)
{
  char      work[255];
  struct MsgS* pMsg = (struct MsgS*)&work;
  int*         pMsgType = (int*)&pMsg[1];
  RTIFilterEnum_t* msg= (RTIFilterEnum_t*)&pMsgType[1];
  pMsg->TimeStamp = Scheduler::instance().clock() + lookahead;
  *pMsgType = 601601; // Note OOB Data for Filter
  *msg = (RTIFilterEnum_t)t;
  RTI_UpdateAttributeValues(ObjInstance, pMsg,
                            MSGS_SIZE(sizeof(*msg)+sizeof(int)),0);
}

#endif /*HAVE_FILTER*/
// Needed for BRTI, not used
extern "C" {
  void RequestRetraction(CoreRetractionHandle)
  {
  }
}

RTIScheduler* RTIScheduler::rtiinstance_;

static class RTISchedulerClass : public TclClass {
public:
	RTISchedulerClass() : TclClass("Scheduler/RTI") {}
	TclObject* create(int /* argc */, const char*const* /* argv */) {
		return (new RTIScheduler());
	}
} class_RTI_sched;

RTIScheduler::RTIScheduler()
{
  // ALFRED ip->rtirouter
  rmap_len_ = 0;
  rmap_size_ = 100;
  if ((rmap_ = (route_map_t *)malloc(sizeof(route_map_t) * \
          rmap_size_)) == NULL) {
    printf("RTIScheduler constructor: malloc() failed!\n");
    exit(1);
  }
}

extern "C" {
  int gethostname(const char*, int);
}
char         hn[255]; /* Hostname */
int RTIScheduler::init(int argc, const char*const* argv)
{
char* argv1[2] = {"pdns", NULL};
int   argc1 = 1;

  gethostname(hn, sizeof(hn));
  if(0) printf("Hello from RTISched::init name %s, argc %d\n", name(), argc);
  if(0) for (int i = 0; i < argc; i++) printf("arg %d - %s\n", i, argv[i]);
  fprintf(stderr, "RTISched before RTIKIT initializer %s\n", hn);fflush(stderr);
  RTI_Init(argc1, argv1);
  // ALFRED get env var for DEBUG purposes
  char *pdebug = getenv("PDNS_DEBUG");
  pd_ = pdebug ? atoi(pdebug) : 0;
  RTIKIT_Barrier();  /* Wait for others */
  // SF init hack
  for(int slf=0;slf<10000;slf++)Core_tick();
  printf("RTI_Init COMPLETE! Node %d\n", FM_nodeid);fflush(stdout);
#ifdef  USE_BACKPLANE
  DSHandle_t dsh = InitializeDSim(FM_nodeid);
  TcpRegister(dsh);
  IpRegister(dsh);
  RegistrationComplete ( FM_nodeid, FM_numnodes);
#endif
	// ALFRED - init counters
  rlinkPktC = 0;
	pktC = 0;
	dropPkts = 0;
  return(0);
}

void RTIScheduler::setclock(double clock)
{
 clock_ = clock;
}

/*void DBLBTSDump();*/

/* Callbacks from RTI.C */
static int          Granted; /* TRUE if time advance granted */
typedef unsigned long MY_Time;  /* Use 32 bit int's for time */
TM_Time GrantedTime;            /* Time granted to */
static unsigned long long TotalReflects; /* ALFRED converted to ull */

NsObject* GetLocalIP(ipaddr_t ipaddr); // in scheduler.cc

static void TryToFree(ipaddr_t ipaddr, ipportaddr_t port)
{
Tcl& tcl = Tcl::instance();
static int firstpass = 1;
static int exists = 0;
int        r;

 if (firstpass) 
   { // See if the unload-agent proc exists
     tcl.evalf("lsearch -exact [info procs] unload-agent");
     tcl.resultAs(&r);
     if (r >= 0)
       { // Ok found it
         if(0)printf("unload-agent exists\n");
         exists = 1;
       }
     else
       { // No unload-agent, just ignore
         if(0)printf("unload-agent NOT FOUND\n");
       }
     firstpass = 0;
   }
 if (exists)
   {
     NsObject* pObj = GetLocalIP(ipaddr);
     if (pObj)
       { // Object found
         char work[255];
         tcl.evalf("%s findport %d", pObj->name(), port);
         strcpy(work, tcl.result());
         if (work[0] != 0)
           { // Found the agent
             if(0)printf("Freeing agent %s\n");
             tcl.evalf("unload-agent %s",work);
           }
         else
           {
             if(0)printf("Can't find port %d\n", port);
           }
       }
     else
       {
         if(0)printf("Can't find ipaddr %08x\n", ipaddr);
       }
   }
}

static ipportaddr_t TryToCreate(ipaddr_t ipaddr, ipaddr_t daddr,
                                ipportaddr_t dport,
                                unsigned long ep1,
                                unsigned long ep2,
                                unsigned long ep3)
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
     // Since oob's are broadcast, we see our own.  So we need
     // to check if the ipaddr is local, and only call create-and-bind
     // if so.
     NsObject* p = GetLocalIP(ipaddr);
     if (p)
       {
         tcl.evalf("create-and-bind 0x%08x 0x%08x %d %d %d %d",
                   ipaddr, daddr, dport, ep1, ep2, ep3);
         tcl.resultAs(&port);
         if(0)printf("The bound port is %d\n", port);
       }
   }
 return port;
}

void ReflectAttributeValues (TM_Time T, struct MsgS* pMsg, long MsgSize, long MsgType)
{
RTIRouter*   pRouter;
RTIRouter**  ppRouter;
char*        pbuf;
RTIMsg_t*    pOOB;
int*         pMsgType;
char*        pMyData;
char         work[512];

 pMsgType = (int*)&pMsg[1];

#ifdef HAVE_FILTER
if(*pMsgType == 601601)  
   { // Got oob data for Filter
     RTIFilterEnum_t* pm;
     pm = (RTIFilterEnum_t*)&pMsgType[1];

     Tcl& tcl = Tcl::instance();
     
     tcl.evalf("info exists filters");
     int proceed = atoi(tcl.result());
     if (proceed) {
       tcl.evalf("llength $filters");
       
       int filters_length  = atoi((char*)tcl.result());
       printf("%d filters in system\n",filters_length);fflush(stdout);
       char filter[10];

       if(*pm == RTIFilterStart) {
	 printf("Turning filtering ON\n");fflush(stdout);
	 for (int i = 0; i < filters_length ;i++) {
	   tcl.evalf("lindex  $filters  %d",i);	
	   strcpy(filter,tcl.result());
	   if(1)printf("Filter is %s",filter);
	   tcl.evalf("%s on",filter);
	 }
       }
       if (*pm == RTIFilterStop) {
	 printf("Turning filtering OFF\n");fflush(stdout);
	 for (int i = 0; i < filters_length ;i++) {
	   tcl.evalf("lindex  $filters  %d",i);	
	   strcpy(filter,tcl.result());
	   if(0)printf("Filter is %s",filter);
	   tcl.evalf("%s off",filter);
	 }
       }
     } else { 
       printf("No global variable \"filters\" exist in this system\n");
       fflush(stdout);
     }
     return;
   }
#endif /* HAVE_FILTER*/

#ifdef  HDCF_IF
 typedef struct {
   size_t WhichQ;
   Size_t Size;
   Rate_t ChangeRate;
   Rate_t  InputFlowRate;
   Rate_t  OutputFlowRate;
 } HDCF_t;
 if(*pMsgType == 4321)
 {
     HDCF_t* pHDCF = (HDCF_t*)&pMsgType[1];
     int s = MsgSize - sizeof(struct MsgS) - sizeof(int);
     printf("Got HDCF_IF msg, total size %d residual size %d\n",
            MsgSize, s);
     int c = s / sizeof(HDCF_t); // Number of update entries
     for (int i = 0; i < c; i++, pHDCF++)
       {
         FluidQueue* fq = FluidQueue::GetFQ(pHDCF->WhichQ);
         if (fq)
           {
             fq->Update(T, pHDCF->Size, pHDCF->ChangeRate,
                        pHDCF->InputFlowRate, pHDCF->OutputFlowRate);
           }
         else
           {
             printf("RTISched::Reflect, ignoring fq update for %d\n",
                    pHDCF->WhichQ);
           }
       }
     MB_FreeBuffer(RTIFreePool, pMsg);
     return;
 }
#endif

#ifdef VEIL_IF
 if(*pMsgType == 4321)
 {
     char *pvmsg = (char *)pMsg;
     RTIScheduler& s = RTIScheduler::rtiinstance();
     s.setclock(T);
     void veil_recv( char * );
     veil_recv( pvmsg );
     MB_FreeBuffer(RTIFreePool, pMsg);
     return;
 }
#endif

#ifdef USE_OOB
 if (0)//KALYAN:(*pMsgType)
   { // Got oob data
     pOOB = (RTIMsg_t*)&pMsgType[1];
     if(0)printf("Got oob t %d da %08x dp %d sa %08x sp %d\n",
            pOOB->t, pOOB->da, pOOB->dp, pOOB->sa, pOOB->sp);
     switch(pOOB->t) {
       case  RTIM_LoadNewAgent:  // Request a new agent be loaded
         if(0)printf("TTC, ep1 %d ep2 %d ep3 %d\n", 
                     pOOB->ep1, // Instance id
                     pOOB->ep2, // req size
                     pOOB->ep3);// Reply size
         TryToCreate(pOOB->da, pOOB->sa, pOOB->sp,
                     pOOB->ep1, // Instance id
                     pOOB->ep2, // req size
                     pOOB->ep3);// Reply size
         break;
       case  RTIM_UnloadAgent :  // Unload an existing agent
         TryToFree( pOOB->da, pOOB->dp);
         break;
       case  RTIM_SetDestPort :  // Set destination port for an agent
         NsObject* pObj = GetLocalIP(pOOB->sa);
         Tcl&      tcl  = Tcl::instance();
         if (pObj)
           { 
             if(0)printf("Found local ip %08x node %s\n",
                         pOOB->sa, pObj->name());
             tcl.evalf("%s findport %d", pObj->name(), pOOB->sp);
             strcpy(work, tcl.result());
             if (work[0] != 0)
               { // Found the agent
                 tcl.evalf("[Simulator instance] rconnect %s 0x%08x %d",
                           work, pOOB->da, pOOB->dp); // do the remote connect
                 if(0)printf("SDP, rconnect %s 0x%08x %d\n", work, 
                        pOOB->da, pOOB->dp);
               }
             else
               {
                 printf("Can't find agent on port %d, node %s\n",
                        pOOB->sp, pObj->name());
               }
           }
         else
           { 
             if(0)printf("Can't find node for localip %08x\n", pOOB->sa);
           }
         break;
     } 
     MB_FreeBuffer (RTIFreePool, pMsg); /* Return buffer to available pool */
     return;
   }
#endif

 GrantedTime = T;
if(0)printf("Host %s ReflectTime %f MSize %ld MTime %f\n",
        hn, T, MsgSize, pMsg->TimeStamp);

  if(*pMsgType==RTIKIT_nodeid)
    {/*Ignore self msg*/
      RTILink::FreeMessage((char*)pMsg);
      return;
    }
	
 TotalReflects++; /* ALFRED - moved after self reflect drop */

 /* Get a packet from available pool and load up the bits */
 Packet* p = Packet::alloc();
 p->time_ = T; // Set event time
 pMyData = (char*)&pMsgType[1];
#ifdef USE_BACKPLANE
 char baggage[2000]; // Fix this..need malloc and getmessagesize()
 int outlth = sizeof(baggage);
 ImportMessage((char*)(pMyData), MsgSize - sizeof(struct MsgS) - sizeof(int),
              (char*)p, baggage, &outlth);
#else

#ifdef USE_COMPRESSION
 Uncompress((unsigned long*)p->bits(),
            (unsigned long*)(pMyData),
            (MsgSize - sizeof(struct MsgS) - sizeof(long)) / 4);
#else
 memcpy(p->bits(), (char*)(pMyData), MsgSize - sizeof(struct MsgS) - sizeof(int));
#endif

#endif
 RTIScheduler& s = RTIScheduler::rtiinstance();
 assert(T >= clock_); // ALFRED check forward progress
 s.setclock(T); // Set new clock time
 pbuf = (char*)pMsg;
 ppRouter = (RTIRouter**)&pbuf[MsgSize];
 pRouter = *ppRouter;
 pRouter->rrecv(p, NULL);
 RTILink::FreeMessage((char*)pMsg); /* Return buffer to available pool */
}

extern "C" {
int TM_PrintHisto(void);
void MyHereProc (long MsgSize, struct MsgS *Msg) {}
}

double getime; // Global event time for debugging
int    ghalted;// Global halted for debugging

void TimeAdvanceGrant (TM_Time T)
{
 GrantedTime = T;
 if(ghalted && 0)printf("Host %s GrantTime %f\n", hn, T);
 Granted = 1;
}

void RTIScheduler::run()
{ 
#define LARGE_TIME 1000000.0
  int   i;
  char* argv[2] = {"nsdt", NULL};
  int   argc = 1;
  unsigned long long RequestCount = 0; // ALFRED converted to ull
  Event* e;
  double etime;
  TIMER_TYPE t1, t2; //KALYAN

  // ALFRED cleaned up code
  printf("Entering RTI Scheduler, host %s\n", hn);
  Tcl::instance().evalc("[Simulator instance] forced-lookahead");
  Tcl::instance().resultAs(&i);
  if (i) {
    /* Forced value specified (for debugging only) */
    Tcl::instance().evalc("[Simulator instance] set forced-lookahead-value");
    Tcl::instance().resultAs(&lookahead);
    printf("Forced LA is %f\n", lookahead);
  } else {
    Tcl::instance().evalc("[Simulator instance] get-lookahead");
    Tcl::instance().resultAs(&lookahead);
    printf("Calculated LA is %f\n", lookahead);
  }
  RTI_SetLookAhead(lookahead); /* Set the lookahead value */
  printf("%s using lookahead value of %f\n", hn, lookahead);
  printf("RTISched after RTIKIT initializer %s\n", hn);fflush(stdout);
  fprintf(stderr, "RTISched after RTIKIT initializer %s\n", hn);fflush(stderr);
  /* Create and join groups for all output links */
  Tcl::instance().evalc("foreach i [Node info instances] {\n"
                        "$i create-groups\n}");
  CreateOOBGroup();  /* Create the group for OOB Data */
  RTIKIT_Barrier();  /* Wait for others */
  printf("After create group barrier %s\n", hn);fflush(stdout);

  Tcl::instance().evalc("foreach i [Node info instances] {\n"
                        "$i publish-groups\n}");
  PublishOOBGroup(); /* Join the group for OOB Data */
  RTIKIT_Barrier();  /* Wait for others */
  printf("After publish group barrier %s\n", hn);fflush(stdout);

  Tcl::instance().evalc("foreach i [Node info instances] {\n"
                        "$i join-groups\n}");
  JoinOOBGroup();    /* Join the group for OOB Data */
  RTIKIT_Barrier();  /* Wait for others */
  printf("After join group barrier %s\n", hn);fflush(stdout);

  /* Log the start time of the actual simulation */
  Tcl::instance().evalc( "[Simulator instance] sim-start");

  /* Allocate a few packets before entering loop so we have some on free list */
  for (int i1 = 0; i1 < 20; i1++) {
    Packet* p = Packet::alloc();
    Packet::free(p);
  } 
  instance_ = this;
  rtiinstance_ = this;
  
#if 1 //Set this to 0 if using libSynk versions prior to 29Dec03
  RTIKIT_FinalizeTopology();
#endif

  printf("Entering sched main loop, host %s\n", hn);fflush(stdout);
  Tcl::instance().evalc("[Simulator instance] log-simstart");
  TIMER_NOW(t1);
  // Main simulation loop
	while (!halted_) {
    e = earliest();
    if (e) {
      etime = e->time_;
    } else {
      etime = LARGE_TIME;
    }
    getime = etime;
    if (etime > GrantedTime) { // Need to see if ok
      if(0)printf("Trying to adv to %f\n", etime);
      Granted = 0;
      RTI_NextEventRequest(etime);
      RequestCount++;
			while(!Granted) Core_tick();
    }
    e = (Event*)earliest(); // In case a new one came in
    if (e) {
      etime = e->time_;
    } else {
      etime = LARGE_TIME;
    }
    if (GrantedTime >= LARGE_TIME) break; // Done
    if (GrantedTime >= etime && e != NULL) { // Time to process this event
      e = deque(); // remove this one
      assert(e->time_ >= clock_); // ALFRED check forward progress
      clock_ = e->time_;
      e->uid_ = - e->uid_;
      e->handler_->handle(e);
      evcount++;
    }
    if(pd_){static double nxtp=0;if(clock_>=nxtp){double dt;TIMER_NOW(t2);dt=TIMER_DIFF(t2,t1);nxtp+=1.0;printf("Simnum= %d Now= %lf elapsedsecs= %lf dlink= %llu rlink=%llu totpktH= %llu dropped=%llu RqC= %llu EvC= %llu MPTS= %lf MEVS= %lf\n",FM_nodeid,clock_,dt,pktC,rlinkPktC,(pktC+rlinkPktC),dropPkts,RequestCount,evcount,(pktC+rlinkPktC)/dt/1000000.0,evcount/dt/1000000.0);fflush(stdout);}}//KALYAN
  }
  ghalted = 1;
  printf("%s exited event main loop RqC %llu EvC %llu\n", 
      hn, RequestCount, evcount);
  printf("-----------\n");
  printf("Packet stats:\n");
  printf("  dlink packet hops: %llu\n", pktC);
  printf("  rlink packet hops: %llu\n", rlinkPktC);
  printf("  total packet hops: %llu\n", pktC+rlinkPktC);
  printf("  drop-tail packets dropped: %llu\n", dropPkts);
  printf("-----------\n");
  /*if(0)DBLBTSDump();*/
  fflush(stdout);
  // Let's all agree we are all done
  while (1) {
    etime = LARGE_TIME + 10;
    Granted = 0;
    RTI_NextEventRequest(etime);
    while(!Granted) Core_tick();
    if (GrantedTime >= etime) break; /* All agree */
  }

  if(0)printf("Before exit barrier\n");
  RTIKIT_Barrier();  /* Wait for others */
  // KALYAN termination hack
  {TIMER_TYPE t1,t2; double dt; TIMER_NOW(t1);do{FM_extract(~0);
   TIMER_NOW(t2);dt=TIMER_DIFF(t2,t1);}while(dt<2/*secs*/);}
  if(0)printf("After exit barrier\n");
  printf("Total Reflect Msgs received %llu\n", TotalReflects);fflush(stdout);
  TM_PrintStats();
  printf("Host %s exiting run\n", hn);fflush(stdout);
}


char scrbuf[100]; // tclresults for getlocalipname .. gfr
int RTIScheduler::command(int argc, const char*const* argv)
{
Tcl& tcl = Tcl::instance();
 
  if (argc == 3)
    {
      if (strcmp(argv[1], "get-local-ip") == 0)
        { // Get the name of tcl object representing the specified ip addr
          // ALFRED fix this to support dotted-quad
          struct in_addr addr;
          ipaddr_t ipaddr;
          if (strchr(argv[2], '.') != NULL) {
            inet_aton(argv[2], &addr);
            ipaddr = ntohl(addr.s_addr);
          } else {
            ipaddr = strtol(argv[2], NULL, 0);
          }
          if(0)printf("getting local ip %s\n", argv[2]);
          const char* r = GetLocalIPName(ipaddr);
          *scrbuf = 0;
          if (r) {
            strcpy(scrbuf, r);
          } else {
            strcpy(scrbuf, "0");
          }
          tcl.result(scrbuf);
          return (TCL_OK);
        }
#ifdef HAVE_FILTER
      if (strcmp(argv[1], "send-oob-filter") == 0)
	{ // Get the name of tcl object representing the specified ip addr
	  if(1)printf("sending start filtering OOB msg");

	  if (strcmp(argv[2], "on") == 0){
	    SendOOBFilter(RTIFilterStart);
	    return (TCL_OK);
	  }
	  if (strcmp(argv[2], "off") == 0){
	    SendOOBFilter(RTIFilterStop);
	    return (TCL_OK);
	  }
	  return (TCL_ERROR);
	}
#endif /*HAVE_FILTER*/
    }
  
  if (argc == 4)
    {
      if (strcmp(argv[1], "add-local-ip") == 0)
        {
          AddLocalIP(strtoll(argv[2], NULL, 0),
                     (NsObject*)TclObject::lookup(argv[3]));
          return (TCL_OK);
        }
      if (strcmp(argv[1], "get-iproute") == 0) {
        struct in_addr addr;
        ipaddr_t ipaddr;
        *scrbuf = 0;
        Agent *pAgent = (Agent *)TclObject::lookup(argv[2]);
        if (strchr(argv[3], '.') != NULL) {
          inet_aton(argv[3], &addr);
          ipaddr = ntohl(addr.s_addr);
        } else {
          ipaddr = strtol(argv[3], NULL, 0);
        }
        Agent *pRoute = GetIPRoute(ipaddr, pAgent->get_ipaddr());
        if (pRoute == NULL) {
          strcpy(scrbuf, "0");
        } else {
          strcpy(scrbuf, pRoute->name());
        }
        tcl.result(scrbuf);
        return TCL_OK;
      }
    }
  if (argc == 5) {
      if (strcmp(argv[1], "iproute-connect") == 0) {
        struct in_addr addr;
        ipaddr_t ipaddr;
        ipportaddr_t dport = strtol(argv[4], NULL, 0);
        Agent *pAgent = (Agent *)TclObject::lookup(argv[2]);
        if (strchr(argv[3], '.') != NULL) {
          inet_aton(argv[3], &addr);
          ipaddr = ntohl(addr.s_addr);
        } else {
          ipaddr = strtol(argv[3], NULL, 0);
        }
        Agent *pRoute = GetIPRoute(ipaddr, pAgent->get_ipaddr());
        if (pRoute == NULL) {
          printf("Sim::rtischeduler: No IPRoute for %08x:%d! ", ipaddr, dport);
          printf("Are you sure target is remote?\n");
          exit(1);
        }
        Tcl &tcl = Tcl::instance();
        tcl.evalf("[Simulator instance] connect %s %s", pAgent->name(), \
            pRoute->name());
        if(0)printf("Sim::rtischeduler: rconnected %s %s\n", pAgent->name(),pRoute->name());
        return (TCL_OK);
      }
  }
  if (argc >= 7) 
    {
      // ALFRED ip->rtirouter
      if (strcmp(argv[1], "add-iproute") == 0) {
        struct in_addr addr;
        ipaddr_t ipaddr, mask, srcip, smask;
        Agent *pAgent = (Agent *)TclObject::lookup(argv[6]);
        if (strchr(argv[2], '.') != NULL) {
          inet_aton(argv[2], &addr);
          ipaddr = ntohl(addr.s_addr);
        } else {
          ipaddr = strtol(argv[2], NULL, 0);
        }
        if (strchr(argv[3], '.') != NULL) {
          inet_aton(argv[3], &addr);
          mask = ntohl(addr.s_addr);
        } else {
          mask = strtol(argv[3], NULL, 0);
        }
        if (strchr(argv[4], '.') != NULL) {
          inet_aton(argv[4], &addr);
          srcip = ntohl(addr.s_addr);
        } else {
          srcip = strtol(argv[4], NULL, 0);
        }
        if (strchr(argv[5], '.') != NULL) {
          inet_aton(argv[5], &addr);
          smask = ntohl(addr.s_addr);
        } else {
          smask = strtol(argv[5], NULL, 0);
        }
        AddIPRoute(ipaddr, mask, pAgent, srcip, smask);
        return (TCL_OK);
      }
      if (strcmp(argv[1], "rti-oob") == 0)
        {
          unsigned long ep1 = 0; // The "extra" parameters
          unsigned long ep2 = 0;
          unsigned long ep3 = 0;
          unsigned long ep4 = 0;
          if(0)printf("Sendoob, %s %s %s %s %s\n", 
                 argv[2], argv[3], argv[4], argv[5], argv[6]);
          if (argc > 7)  ep1 = strtoll(argv[7], NULL, 0);
          if (argc > 8)  ep2 = strtoll(argv[8], NULL, 0);
          if (argc > 9)  ep3 = strtoll(argv[9], NULL, 0);
          if (argc > 10) ep4 = strtoll(argv[10], NULL, 0);
          SendOOB(atoi(argv[2]), // Msg Type
                  strtoll(argv[3], NULL, 0), // Dest Addr
                  strtoll(argv[4], NULL, 0), // Dest port 
                  strtoll(argv[5], NULL, 0), // Src Addr
                  strtoll(argv[6], NULL, 0), // Src port
                  ep1, ep2, ep3, ep4);
          return (TCL_OK);
        }
    }
  return MapScheduler::command(argc, argv);
}

// ALFRED ip->rtirouter methods
void RTIScheduler::AddIPRoute(ipaddr_t ipaddr, ipaddr_t mask, Agent *pAgent,
    ipaddr_t srcip, ipaddr_t smask) {

  if (rmap_len_ >= rmap_size_) {
    route_map_t *tmp;
    if ((tmp = (route_map_t *)realloc(rmap_, sizeof(route_map_t) * \
            (rmap_size_ + 100))) == NULL) {
      printf("RTIScheduler::AddIPRoute, realloc() failed!\n");
      exit(1);
    }
    rmap_ = tmp;
    rmap_size_ += 100;
  }
  rmap_[rmap_len_].ipaddr = ipaddr;
  rmap_[rmap_len_].mask = mask;
  rmap_[rmap_len_].pAgent = pAgent;
  rmap_[rmap_len_].srcip = srcip;
  rmap_[rmap_len_].smask = smask;
  rmap_len_++;
}

Agent *GetIPRoute(ipaddr_t ipaddr, ipaddr_t srcip) {

  if(0)printf("GetIPRoute called, d %08x s %08x\n", ipaddr,srcip);

  for (int i = 0; i < rmap_len_; i++) {
    if (rmap_[i].srcip != 0 && rmap_[i].smask != 0) {
      if(0)printf(" -->non-zero: %08x %08x %08x %08x\n", rmap_[i].ipaddr,rmap_[i].mask,rmap_[i].srcip,rmap_[i].smask);
      if(0)printf("   masked: %08x %08x %08x %08x\n", rmap_[i].ipaddr & rmap_[i].mask, ipaddr & rmap_[i].mask, rmap_[i].srcip & rmap_[i].smask, srcip & rmap_[i].smask);
      if ((rmap_[i].srcip & rmap_[i].smask) == (srcip & rmap_[i].smask) &&
        (rmap_[i].ipaddr & rmap_[i].mask) == (ipaddr & rmap_[i].mask)) {
        if(0)printf("  SRC returning pAgent: %s\n", rmap_[i].pAgent->name());
        return rmap_[i].pAgent;
      }
    }
  }
  for (int i = 0; i < rmap_len_; i++) {
    if ((rmap_[i].ipaddr & rmap_[i].mask) == (ipaddr & rmap_[i].mask)) {
      if(0)printf("  REG returning pAgent: %s\n", rmap_[i].pAgent->name());
      return rmap_[i].pAgent;
    }
  }
  return NULL;
}

