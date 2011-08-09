// Out-of-Band message for PDNS
// Allows one ns to communicate useful info to a second instance
// George F. Riley, Georgia Tech, Winter 2000

#include <rti/hdr_rti.h>

typedef enum {
  RTIM_LoadNewAgent,  // Request a new agent be loaded
  RTIM_UnloadAgent,   // Unload an existing agent
  RTIM_SetDestPort    // Set destination port for an agent
  // Will need more as time goes on
} RTIMsgEnum_t;

typedef struct {
  RTIMsgEnum_t  t;     // Type of messgae
  ipaddr_t      da;    // Specified dest address
  ipportaddr_t  dp;    // Dest port
  ipaddr_t      sa;    // Specified src address
  ipportaddr_t  sp;    // Src port
  unsigned long ep1;   // Extra parameter 1
  unsigned long ep2;   // Extra parameter 2
  unsigned long ep3;   // Extra parameter 3
  unsigned long ep4;   // Extra parameter 4
  // Will need more
} RTIMsg_t;

void SendOOB(int t, int da, int dp, int sa, int sp); // See rtisched.cc

#ifdef HAVE_FILTER

void SendOOBFilter(int t);

typedef enum {
  RTIFilterStart,  // Request to start filtering
  RTIFilterStop    //Request to stop filtering
} RTIFilterEnum_t;

#endif /*HAVE_FILTER*/
