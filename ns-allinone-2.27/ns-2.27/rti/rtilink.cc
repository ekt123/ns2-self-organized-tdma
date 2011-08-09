// New link type by George Riley.  Used to allow running ns simulations in 
// parallel.  Lets a simulation on one system be broken into several
// simulations on several systems
// The RTILink is a link where only one end is present on this simulation.
// The other end of the link is on another simulation somewhere.  The
// packet received is forwarded to the appropriate simulation via RTIKit
// with the timestamp incremented by the appropriate delay.
// George F. Riley. Georgia Tech.
// Fall 1998

// libSynk/libbrti includes
extern "C" {
#include "brti.h"
}

//#define USE_BACKPLANE
//#undef  USE_BACKPLANE
#ifdef  USE_BACKPLANE
#include "backplane/dsim.h"
#endif

//#include <stdlib.h>
//#include <string.h>

#include "tclcl.h"
//#include "net.h"
#include "packet.h"
#include "ip.h"
#include "rti/hdr_rti.h"
#include "agent.h"
#include "scheduler.h"

#include "rti/rtilink.h"
#ifdef USE_COMPRESSION
#include "rti/rticompress.h"
#endif

/* ALFRED - pkt count stats */
#include "common/pktcount.h"

extern char          hn[255];       /* Host name (RTISCHED.CC) */

RTILink::FreeVec_t RTILink::FreeVec;

static class RTILinkClass : public TclClass {
 public:
	RTILinkClass() : TclClass("RTILink") {}
	TclObject* create(int, const char*const*) {
		return (new RTILink());
	}
} class_RTI_link;

// Static "WhereMessage" procedure
char* RTILink::WhereMessage (   /* Advise where to store Rx message */
    long         MsgSize,        /* Size of RX Message */
    void*        pvRouter,       /* Context info */
    long         MsgType)        /* Type specified by sender */
{
char* buf;
RTIRouter *pRouter = (RTIRouter *)pvRouter;
RTIRouter** ppContext;

 if(0)printf("WhereMsg size %ld Type %ld context %p\n",
         MsgSize, MsgType, pRouter);
 if (FreeVec.size())
   { // entries exist in free vector
     // ALFRED fix possible buffer overflow by reallocating memory as needed
     buf = FreeVec.back();
     buf -= sizeof(long)/sizeof(char); // Go back one long to find zone size
     if(0)printf("Reusing msg %p\n", buf);
     FreeVec.pop_back(); // Remove it
     if (*((long *)buf) < MsgSize) {
       char *tbuf;
       long x = (MsgSize / sizeof(long) + sizeof(long)) * sizeof(long);
       if(0){printf("buf entry too small, reallocating memory from %d to %d\n", 
           *((long *)buf), MsgSize);fflush(stdout);}
       if ((tbuf = (char *)realloc(buf, x)) == NULL) {
         printf("Error: RTILink::WhereMessage, realloc() failed!\n");
         exit(1);
       }
       buf = tbuf;
       long *tmp = (long *)buf;
       *tmp = (long)MsgSize;
     }  
   }
 else
   {
     long x = (MsgSize / sizeof(long) + sizeof(long)) * sizeof(long);
     if ((buf = (char *)malloc(x)) == NULL) {
       printf("Error: RTILink::WhereMessage, malloc() failed!\n");
       exit(1);
     }
     long *tmp = (long *)buf;
     *tmp = (long)MsgSize;
     if(0)printf("Allocating new buf, size %d, ptr %p\n", MsgSize, buf);
   }
 if (buf == NULL)
   {
     printf("Error: buf is NULL...? Fatal error.\n");
     exit(1);
   }

 char *tmp = buf + sizeof(long)/sizeof(char); // Need to offset by long
 ppContext = (RTIRouter**)&tmp[MsgSize];
 *ppContext = pRouter; /* Set context after message */
 if(0){printf("RTILink setting pRouter %p msgtype %d\n", pRouter, 
     MsgType);fflush(stdout);}
 return(tmp);
}

void RTILink::FreeMessage(
    char*         pMsg)
{
  if(0)printf("Freeing Msg %p\n", pMsg);
  FreeVec.push_back(pMsg);
}

RTILink::RTILink() : LinkDelay()
{
 if(0)printf("Hello from RTILINK CC constructor\n");
 // Track the tcl "rtarget_" variable
 rtarget_ = NULL;

 bind_bw("bandwidth_", &bandwidth_);
 bind_time("delay_", &delay_);
 bind("off_ip_",               &off_ip_);
}

int RTILink::command(int argc, const char*const* argv)
{
  //NsObject*  pHead;
//ipaddr_t   ipaddr;
int i;

//Tcl& tcl = Tcl::instance();
  if(0){printf("RTILink Command %d ", argc);
    for(i=0;i<argc;i++)printf("%d - %s ", i, argv[i]);printf("\n");
    fflush(stdout);}
  if (argc == 3) 
    {
      if (strcmp(argv[1], "set-target") == 0)
        {
          rtarget_ = (NsObject*)TclObject::lookup(argv[2]);
          if (rtarget_ == NULL)
            {
              printf("remote target %s not found\n", argv[2]);
              exit(1);
            }
          return(TCL_OK);
        }
    }
  if (argc == 5) 
    {
      if (strcmp(argv[1], "create-group") == 0)
        {
          //printf("%s create-group %s %s %s\n", name(), argv[2], argv[3], argv[4]);
          CreateGroup(argv[2], argv[3], argv[4]);
          return(TCL_OK);
        }
      if (strcmp(argv[1], "publish-group") == 0)
        {
          //printf("%s publish-group %s %s %s\n", name(), argv[2], argv[3], argv[4]);
          PublishGroup(argv[2], argv[3], argv[4]);
          return(TCL_OK);
        }
      if (strcmp(argv[1], "join-group") == 0)
        {
          //printf("%s join-group %s %s %s\n", name(), argv[2], argv[3], argv[4]);
          JoinGroup(argv[2], argv[3], argv[4]);
          return(TCL_OK);
        }
    }
  return (LinkDelay::command(argc, argv));
}

extern unsigned long FM_nodeid;
static int firstpass = 0; // debug
void RTILink::recv(Packet* p, Handler* h)
{
  //int r;
double txtime;
struct MsgS*   pMyMsg;
//char   mybuf[Packet::hdrlen_ + sizeof(struct MsgS) + 16];
static char* mybuf = NULL;  // Buffer for the "reflected" message
long*  pMsgType;
char*  pMyData;
#ifdef USE_BACKPLANE
static isize_t bufsize = 0;
isize_t ol = 0;
#define FUDGE 1016
#endif

#ifdef USE_COMPRESSION
int    compressedlth;
#endif

  hdr_cmn* hdr = hdr_cmn::access(p);
  hdr_ip*  ip  = hdr_ip::access(p);
  hdr_rti* rti = hdr_rti::access(p);
  txtime = hdr->size() * 8.0 / bandwidth_;

  if (ip->dport() < 0) printf("Sending dport %d\n", ip->dport());
  if (!mybuf)
    { // First time, allocate the message buffer
#ifdef USE_BACKPLANE
      bufsize = InquireMessageSize(/*KALYAN:GetDSimHandle()*/);
      mybuf = new char[bufsize + sizeof(struct MsgS) + FUDGE]; // Fudge a bit
#else
      mybuf = new char[Packet::hdrlen_ + sizeof(struct MsgS) + 16];
#endif
    }
  pMyMsg = (struct MsgS*)mybuf;
  pMsgType = (long*)&pMyMsg[1];
  pMyData = (char*)&pMsgType[1];
  pMyMsg->TimeStamp = Scheduler::instance().clock() + txtime + delay_;
  *pMsgType = RTIKIT_nodeid;//KALYAN: 0;
  if(0)printf("RTILink forwarding to dstip %08x dstport %d\n",
         rti->ipdst(), rti->ipdstport());
  /* Use the nodeid as msgtype, so we can ignore our own messages */
  /* Above does not work!  Put nodeid in ip header! (HACK)! */
  rti->RTINodeId() = RTIKIT_nodeid;
#ifdef USE_BACKPLANE
  ol = bufsize + FUDGE;
  ExportMessage((char*)p,
                NULL, 0,
                pMyData, (int*)&ol);
                
  RTI_UpdateAttributeValues(ObjInstance, pMyMsg,
                            MSGS_SIZE(ol/*KALYAN*/+sizeof(int)),
                            0);
	rlinkPktC++;
#else
  /* Copy entire packet to the payload */
#ifdef USE_COMPRESSION
  // Use compression!
  compressedlth = Compress((unsigned long*)pMyData,
                           (unsigned long*)p->bits(), 
                           (Packet::hdrlen_ + 3) / 4);
  RTI_UpdateAttributeValues(ObjInstance, pMyMsg,
                            MSGS_SIZE(compressedlth * 4 + sizeof(int)),
                            // RTIKIT_nodeid);
                            0);
	rlinkPktC++;
#else
#ifdef DEBUG
hdr_rti* hrti = hdr_rti::access(p);
printf("RTILink::Update srcip %08x srcdst %d dstip %08x dstport %d\n",
hrti->ipsrc(), hrti->ipsrcport(),
hrti->ipdst(), hrti->ipdstport());
#endif
  memcpy(pMyData, p->bits(), Packet::hdrlen_);
  RTI_UpdateAttributeValues(ObjInstance, pMyMsg,
                            MSGS_SIZE(Packet::hdrlen_ + sizeof(int)),
                            // RTIKIT_nodeid);
                            0);
	rlinkPktC++;
#endif
#endif

  // Also schedule event when link tx complete
  Scheduler& s = Scheduler::instance();
  s.schedule(h, &intr_, txtime); // Notify when done
  // And free the packet we got
  Packet::free(p);
}

void RTILink::CreateGroup( // Create the RTI group for this link
    const char* pRouter,
    const char* pBroadcastAddr,
    const char* pIPAddr)
{
char          BroadcastAddr[128];
unsigned long ipaddr;
unsigned long bcastaddr;
char          work[255];

  ipaddr = atol(pIPAddr);
  sprintf(work, "%ld.%ld.%ld.%ld",
          ((ipaddr >> 24) & 0xff),
          ((ipaddr >> 16) & 0xff),
          ((ipaddr >>  8) & 0xff),
          ((ipaddr >>  0) & 0xff));
  if(1)printf("%s CreateGroup ipaddr %s\n", hn, work);

  strcpy(BroadcastAddr, pBroadcastAddr);
  bcastaddr = atol(pBroadcastAddr);
  sprintf(work, "%ld.%ld.%ld.%ld",
          ((bcastaddr >> 24) & 0xff),
          ((bcastaddr >> 16) & 0xff),
          ((bcastaddr >>  8) & 0xff),
          ((bcastaddr >>  0) & 0xff));
  if ((ipaddr & 0xff) == 0x01)
    {
      // Create the class (use broadcast address for group name)
      // May already exst (could be created by peer on other system)
      RTI_CreateClass(work);
      if(1)printf("%s created objclass for %s\n", hn, work);
    }
}

void RTILink::PublishGroup( // Publish the RTI group for this link
    const char* pRouter,
    const char* pBroadcastAddr,
    const char* pIPAddr)
{
char          BroadcastAddr[128];
unsigned long ipaddr;
unsigned long bcastaddr;
char          work[255];

  ipaddr = atol(pIPAddr);
  strcpy(BroadcastAddr, pBroadcastAddr);
  bcastaddr = atol(pBroadcastAddr);
  sprintf(work, "%ld.%ld.%ld.%ld",
          ((bcastaddr >> 24) & 0xff),
          ((bcastaddr >> 16) & 0xff),
          ((bcastaddr >>  8) & 0xff),
          ((bcastaddr >>  0) & 0xff));
  ObjClass = RTI_GetObjClassHandle(work);
  if (ObjClass == NULL)
    {
      printf("%s can't get objclasshandle for %s\n", hn, work);
      exit(0);
    }
  // Publish and register my interest in this class
  RTI_PublishObjClass(ObjClass);
  ObjInstance = RTI_RegisterObjInstance(ObjClass);
  if(1)printf("%s Registered objinst5, ObjInst %ld\n", name(), ObjInstance);
}

void RTILink::JoinGroup( // Join the RTI group for this link
    const char* pRouter,
    const char* pBroadcastAddr,
    const char* pIPAddr)
{
NsObject*    pNSRouter;
//char         BroadcastAddr[128];
 
  // Get the C++ object for the associated RTIRouter 
  pNSRouter = (NsObject*)TclObject::lookup(pRouter);
  // Join the group
  if (! RTI_IsClassSubscriptionInitialized (ObjClass))
    { //  Set up a context
      RTI_InitObjClassSubscription (ObjClass, WhereMessage, pNSRouter);
    }
  RTI_SubscribeObjClassAttributes(ObjClass);
  if(1)printf("Router %s subcribed...\n", pNSRouter->name());
}


