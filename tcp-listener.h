/* Tcp Listener Agent :: for dynamic FullTcp connections
 * 
 * Alfred Park (park@cc.gatech.edu)
 * College of Computing, Georgia Institute of Technology
 * See tcp-listener.cc for revision history
 *
 */

#include "tcp-full.h"
#include "rti/hdr_rti.h"
#include "node.h"
#include "trace-colors.h"
#include <map>

class TcpListenerAgent;

typedef struct {
  Agent *pAgent;
  double connect_time;
  bool est;
} ip_info_t;

typedef pair<ipaddr_t, ipportaddr_t> cmap_kt;
typedef map<cmap_kt, ip_info_t*> cmap_t;
typedef cmap_t::iterator cmap_it;
typedef cmap_t::value_type cmap_pt;

typedef struct {
  ipaddr_t src_ip;
  ipportaddr_t sport;
  int retries;
  bool est;
} timeout_info_t;

typedef multimap<double, timeout_info_t*> tomap_t;
typedef tomap_t::iterator tomap_it;
typedef tomap_t::value_type tomap_pt;

class ListenerTimer : public TimerHandler {
  public:
    ListenerTimer(TcpListenerAgent *a) : TimerHandler() { a_ = a; }
  protected:
    virtual void expire(Event *e);
    TcpListenerAgent *a_;
};

class TcpListenerAgent : public FullTcpAgent {
  public:
    TcpListenerAgent();
    ~TcpListenerAgent() { cancel_timers(); rq_.clear(); }
    virtual void recv(Packet *pkt, Handler*);
    virtual int command(int argc, const char*const* argv);
    virtual void timeout();
   
  protected:
    ListenerTimer ltimer_;
    cmap_t cmap_;
    tomap_t tomap_;
    double current_to_;
    int table_size_;
    int connections_;
    int max_synack_retries_;
    double entry_expire_;
    Node *pNode;
    char node_name[255];
    char agent_name[255];
    int filter_enable_;
    double filter_trigger_;
    double filter_release_;
    bool filter_on_;
    char *app_agent_;
    char *app_params_;
    char *app_callback_;
    int app_recv_;
    int app_reuse_;
    NsObject *app_pApp_;
    int prebinding_;
    Agent **fulltcp_;
    unsigned int fulltcp_num_;
    unsigned int fulltcp_len_;
    unsigned int fulltcp_use_;
#ifndef TCP_TRACEFILE_SUPPORT
    int trace_enable_;
    unsigned int trace_interval_;
    unsigned int trace_pktcount_;
    char *trace_file_;
    FILE *trace_fp_;
    char *trace_prefix_;
    char *trace_nodename_;
#endif
    void expire_connections();
    void add_timeout(double to, ipaddr_t src_ip, ipportaddr_t sport, 
        int retries);
    void schedule_timeout();
    void est_timeout(ipaddr_t src_ip, ipportaddr_t sport);
    void add_connection(ipaddr_t src_ip, ipportaddr_t sport);
    bool get_connection(ipaddr_t src_ip, ipportaddr_t sport);
    void del_connection(ipaddr_t src_ip, ipportaddr_t sport);
    bool add_agentinfo(ipaddr_t src_ip, ipportaddr_t sport, Agent *agent_add);
    Agent *get_agentinfo(ipaddr_t src_ip, ipportaddr_t sport);
    int get_state() { return state_; }
    void reset_state();
    void promote();
    void set_src(ipaddr_t ip, ipportaddr_t port);
    void inherit_attributes(TcpListenerAgent *a);
    virtual void delay_bind_init_all();
    virtual int delay_bind_dispatch(const char *varName, 
        const char *localName, TclObject *tracer);
};

