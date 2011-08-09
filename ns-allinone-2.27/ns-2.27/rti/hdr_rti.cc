// Define the packet header for the pdns/rti information
// George F. Riley, Georgia Tech, Winter 2000

#include "hdr_rti.h"

// Define the TCL glue for the packet header
int hdr_rti::offset_;

static class RTIHeaderClass : public PacketHeaderClass {
public:
        RTIHeaderClass() : PacketHeaderClass("PacketHeader/RTI",
					    sizeof(hdr_rti)) {
          //printf("Binding offset\n");
	  bind_offset(&hdr_rti::offset_);
	  //printf("Done Binding Offset\n");

	}
        void export_offsets() {
              field_offset("ipsrc_", OFFSET(hdr_rti, ipsrc_));
              field_offset("ipsrc_port_", OFFSET(hdr_rti, ipsrc_port_));
	      field_offset("ipdst_", OFFSET(hdr_rti, ipdst_));
              field_offset("ipdst_port_", OFFSET(hdr_rti, ipdst_port_));
        }

} class_rtihdr;
