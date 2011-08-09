//Fontas Dimitropoulos GaTech  Spring 2003 
//Simple filter that adds some delay

#include "delayfilter.h"
#include "packet.h"


static class DelayMultiFieldFilterClass : public TclClass {
public:
	DelayMultiFieldFilterClass() : TclClass("Filter/MultiField/DelayMultiField") {}
	TclObject* create(int, const char*const*) {
		return (new DelayMultiFieldFilter);
	}
} class_delay_filter_multifield;

void DelayMultiFieldFilter::recv(Packet* p, Handler* h)
{
	Scheduler& s = Scheduler::instance();
  
  switch(filter(p)) {
	case DROP : 
	  if (h) h->handle(p);
	  drop(p);
	  break;
	case DUPLIC :
	  if (filter_target_)
	    filter_target_->recv(p->copy(), h);
	  /* fallthrough */
// 	case PASS :
// 	  if(debug_)printf("D(%s)\n",name());	  
// 	  send(p, h);
// 	  break;
        case PASS :
	  if(debug_) printf("P(%s)\n",name());	  
	  if(disable_) send(p, h);
	  else {
	    Scheduler& s = Scheduler::instance();
	    s.schedule(this, p, delay_);
	  }
	  break;
	case FILTER :
	  if(debug_) printf("D(%s)\n",name());
	  filter_target_->recv((Packet*)p, (Handler*) NULL);
	  break;
  } 
}


void DelayMultiFieldFilter::handle(Event* e)
{
//filter_target_->recv((Packet*)e, (Handler*) NULL);
  send((Packet*)e, (Handler*)NULL);
}

MultiFieldFilter::filter_e DelayMultiFieldFilter::filter(Packet *p) 
{
  fieldobj* tmpfield;
  
  if(disable_) return (PASS);
  
  tmpfield = field_list_;
  if(and_rules_) { 
    while (tmpfield != 0) {
      if (*(int *)p->access(tmpfield->offset) == tmpfield->match)
	tmpfield = tmpfield->next;
      else 
	return (FILTER);
    }
    return(PASS);
  } else { 
    while (tmpfield != 0) {
      if (*(int *)p->access(tmpfield->offset) == tmpfield->match)
	return (PASS);
      else 
	tmpfield =tmpfield->next;
    }
    return(FILTER);
  }
}

int DelayMultiFieldFilter::command(int argc, const char*const* argv)
{
 	Tcl& tcl = Tcl::instance();
	if (argc == 2) {
	  if (strcmp(argv[1], "debug") == 0) {
	    debug_ = true;
	    return TCL_OK;
	  }
	}

 	if (argc == 3) {
 		if (strcmp(argv[1], "filter-delay") == 0) {
 		        delay_ = atof(argv[2]);
 			return TCL_OK;
 		}
	}
	if (argc == 2){
	  if (strcmp(argv[1], "on") == 0) {
	    if (!disable_) 
	      if(debug_)printf("Warning, attempt to enable filter(%s) while enabled\n",name());
	    else {
	      if(debug_)printf("Turning filter(%s) on\n",name());
	    }
	    disable_ = false;		      
	    return TCL_OK;
	  }
	  if (strcmp(argv[1], "off") == 0) {
	    if (disable_) 
	      if(debug_) printf("Warning, attempt to disable filter(%s) while disabled\n",name());
	    else { 
	      if(debug_) printf("Turning filter(%s) off\n",name());
	    }
	    disable_ = true;
	    return TCL_OK;
	  }
	  if (strcmp(argv[1], "or-rules") == 0) {
	    if (!and_rules_) 
	      if(debug_) printf("Warning, using OR filter\n");

	    and_rules_ =false;
	    return TCL_OK;
	  }
	  
 	}
 	return MultiFieldFilter::command(argc, argv);
}
