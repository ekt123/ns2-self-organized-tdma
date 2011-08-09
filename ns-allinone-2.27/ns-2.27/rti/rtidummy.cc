// Dummy classes so the "non-rti" ns will still run
// (The tcl init code references RTI classes so we need this)
// George F. Riley. Georgia Tech.
// Fall 1998


//#include <stdlib.h>
//#include <string.h>

#include "tclcl.h"
#include "packet.h"
#include "ip.h"
#include "agent.h"
#include "scheduler.h"
#include "delay.h"

class RTILink : public LinkDelay {
  public :
    RTILink();
    int command(int, const char*const*);
};

class RTIRouter : public Agent {
  public :
    RTIRouter();
    int command(int, const char*const*);
};

static class RTILinkClass : public TclClass {
 public:
	RTILinkClass() : TclClass("RTILink") {}
	TclObject* create(int, const char*const*) {
		return (new RTILink());
	}
} class_RTI_link;

static class RTIRouterClass : public TclClass {
 public:
	RTIRouterClass() : TclClass("Agent/RTIRouter") {}
	TclObject* create(int, const char*const*) {
		return (new RTIRouter());
	}
} class_RTI_agent;

RTILink::RTILink() : LinkDelay()
{
}

int RTILink::command(int argc, const char*const* argv)
{
  return (LinkDelay::command(argc, argv));
}

RTIRouter::RTIRouter() : Agent(/*XXX*/PT_MESSAGE)
{

}

int RTIRouter::command(int argc, const char*const* argv)
{
  return (Agent::command(argc, argv));
}

