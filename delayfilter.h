// Fontas Dimitropoulos GaTech Spring 2003


#ifndef ns_dfilter_h
#define ns_dfilter_h

#include "filter.h"


class DelayMultiFieldFilter : public MultiFieldFilter {
 public:
  DelayMultiFieldFilter():delay_(0),
    disable_(false),and_rules_(true),debug_(false){}; 
protected:
	int command(int argc, const char* const* argv);
	void recv(Packet*, Handler* h= 0);
	void handle (Event* e);
	filter_e filter(Packet *p);
	double delay_;
	bool disable_;
	bool and_rules_; // if false rules are ORed,otherwise ANDed
	int debug_;
};

#endif /* ns_dfilter_h */
