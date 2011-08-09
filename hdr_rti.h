// Define the packet header for the pdns/rti information
// George F. Riley, Georgia Tech, Winter 2000

#ifndef __HDR_RTI_H__
#define __HDR_RTI_H__

#include "packet.h"

typedef unsigned int ipaddr_t;
typedef int ipportaddr_t;

struct hdr_rti {
  ipaddr_t        ipsrc_;
  ipportaddr_t    ipsrc_port_;
  ipaddr_t        ipdst_;
  ipportaddr_t    ipdst_port_;
  unsigned long   RTINodeId_; // Node id of originator
  static int offset_;
  inline static int& offset() { return offset_; }
  inline static hdr_rti* access(Packet* p) {
    return (hdr_rti*) p->access(offset_);
  }

  /* per-field member acces functions */

  ipaddr_t& ipsrc() {
    return ipsrc_;
  }
  ipportaddr_t& ipsrcport() {
    return ipsrc_port_;
  }
  ipaddr_t& ipdst() {
    return ipdst_;
  }
  ipportaddr_t& ipdstport() {
    return ipdst_port_;
  }

  unsigned long& RTINodeId() {
    return RTINodeId_;
  }
};


#endif
