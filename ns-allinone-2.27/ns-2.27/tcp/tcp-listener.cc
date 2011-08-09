/* Tcp Listener Agent :: for dynamic FullTcp connections
 * 
 * Alfred Park (park@cc.gatech.edu)
 * College of Computing, Georgia Institute of Technology
 *
 * *** Revision History ***
 * v0.84 (01/28/04): add local->remote->local fixes, bug fixes
 * v0.83 (05/29/03): small optimizations and extra checks added
 * v0.82 (05/24/03): fixed timeout bugs and removed unnecessary reschedules,
 *                   added init parameters support for application binding
 * v0.81 (05/21/03): fixed filter trigger/release bugs
 * v0.8  (05/20/03): added FullTcpAgent prebinding support
 * v0.71 (05/19/03): fixed iss_, cwnd_, and connection expire bugs
 * v0.7  (05/17/03): application binding support, cmap_ optimizations
 * v0.6  (05/16/03): pdns-2.26-v2 "ip-connect" compatibility
 * v0.5  (05/14/03): NetAnim tracefile support added
 * v0.4  (05/02/03): more syn+ack bugs fixed, egress filtering support added
 * v0.3  (04/11/03): syn+ack retry bugs fixed, improved efficiency
 * v0.2  (04/10/03): expiration timer added for non syn+ack retry listeners
 * v0.1  (04/04/03): added syn+ack timeout mechanism, spawned FullTcpAgents 
 *                   inherit attributes from listener, optimizations added
 * v0.0  (04/01/03): initial release
 */

#include "ip.h"
#include "flags.h"
#include "random.h"
#include "tcp-listener.h"
#include "tclcl.h"
#include "rti/rtisched.h"
#ifdef HAVE_FILTER
#include "rti/rtioob.h"
#endif

// Need prototypes from rtisched.cc 
NsObject* GetLocalIP(ipaddr_t ipaddr);
Agent *GetIPRoute(ipaddr_t ipaddr, ipaddr_t srcip);

static class TcpListenerClass : public TclClass {
  public:
    TcpListenerClass() : TclClass("Agent/TCP/Listener") {}
    TclObject* create(int, const char*const*) {
      return (new TcpListenerAgent());
    }
} listener_class;

TcpListenerAgent::TcpListenerAgent() : ltimer_(this) {
  connections_ = 0;
  state_ = TCPS_LISTEN;
  type_ = PT_ACK;
  pNode = NULL;
  filter_on_ = false;
  trace_file_ = NULL;
  trace_pktcount_ = 0;
  trace_prefix_ = NULL;
  app_agent_ = NULL;
  app_params_ = NULL;
  app_callback_ = NULL;
  app_recv_ = 0;
  app_reuse_ = 0;
  app_pApp_ = NULL;
  fulltcp_num_ = 0;
  fulltcp_len_ = 0;
  fulltcp_use_ = 0;
  current_to_ = 0.0;
}

void TcpListenerAgent::delay_bind_init_all() {

  delay_bind_init_one("max_synack_retries_");
  delay_bind_init_one("table_size_");
  delay_bind_init_one("entry_expire_");
  delay_bind_init_one("filter_enable_");
  delay_bind_init_one("filter_trigger_");
  delay_bind_init_one("filter_release_");
  delay_bind_init_one("trace_enable_");
  delay_bind_init_one("trace_interval_");
  delay_bind_init_one("prebinding_");
  FullTcpAgent::delay_bind_init_all();
  reset();
}

int TcpListenerAgent::delay_bind_dispatch(const char *varName, 
    const char *localName, TclObject *tracer) {
  
  if (delay_bind(varName, localName, "max_synack_retries_", 
        &max_synack_retries_, tracer)) return TCL_OK;
  if (delay_bind(varName, localName, "table_size_", &table_size_, tracer))
    return TCL_OK;
  if (delay_bind(varName, localName, "entry_expire_", &entry_expire_, 
        tracer)) return TCL_OK;
  if (delay_bind(varName, localName, "filter_enable_", &filter_enable_, 
        tracer)) return TCL_OK;
  if (delay_bind(varName, localName, "filter_trigger_", &filter_trigger_, 
        tracer)) return TCL_OK;
  if (delay_bind(varName, localName, "filter_release_", &filter_release_, 
        tracer)) return TCL_OK;
  if (delay_bind(varName, localName, "trace_enable_", &trace_enable_, 
        tracer)) return TCL_OK;
  if (delay_bind(varName, localName, "trace_interval_", &trace_interval_, 
        tracer)) return TCL_OK;
  if (delay_bind(varName, localName, "prebinding_", &prebinding_, 
        tracer)) return TCL_OK;
  return FullTcpAgent::delay_bind_dispatch(varName, localName, tracer);
}

int TcpListenerAgent::command(int argc, const char*const* argv) {
  
  if (argc == 2) {
    if (strcmp(argv[1], "reset") == 0) {
      reset_state();
      return (TCL_OK);
    }
  }
  if (argc == 3) {
    if (strcmp(argv[1], "add-fulltcp") == 0) {
      if (fulltcp_num_ == 0) {
        if ((fulltcp_ = (Agent **)malloc(sizeof(Agent *) * 100)) == NULL) {
          printf("TcpListenerAgent(%s): FATAL: add-fulltcp malloc() failed!\n",
              name());
          exit(1);
        }
        fulltcp_len_ += 100;
      } else if (fulltcp_num_ >= fulltcp_len_) {
        Agent **tmp;
        if ((tmp = (Agent **)realloc(fulltcp_, sizeof(Agent *) *
                (fulltcp_len_ + 100))) == NULL) {
          printf("TcpListenerAgent(%s): FATAL: add-fulltcp realloc() failed!\n",
              name());
          exit(1);
        }
        fulltcp_ = tmp;
        fulltcp_len_ += 100;
      }
      fulltcp_[fulltcp_num_] = (Agent *)TclObject::lookup(argv[2]);
      if (fulltcp_[fulltcp_num_] == NULL) {
        printf("TcpListenerAgent(%s): FATAL: add-fulltcp lookup(%s) failed!\n",
            name(), argv[2]);
        exit(1);
      }
      fulltcp_num_++;
      return (TCL_OK);
    }
    if (strcmp(argv[1], "set-tracefile") == 0) {
      if (trace_enable_ > 0) {
        trace_file_ = strdup(argv[2]);
        if ((trace_fp_ = fopen(trace_file_, "w")) == NULL) {
          printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
              now(), name(), trace_file_);
          printf("  for writing. Disabling tracefile output.\n");
          trace_enable_ = 0;
        } else {
          fclose(trace_fp_);
        }
        if (trace_prefix_ == NULL) {
          printf("%f: TcpListenerAgent(%s): Warning: trace_prefix_ is NULL\n", 
              now(), name());
          trace_prefix_ = strdup("!");
        }
      }
      return (TCL_OK);
    }
    if (strcmp(argv[1], "set-traceprefix") == 0) {
      if (trace_prefix_ != NULL)
        free(trace_prefix_);
      trace_prefix_ = strdup(argv[2]);
      return (TCL_OK);
    }
  }
  if (argc == 7) {
    if (strcmp(argv[1], "set-application") == 0) {
      app_agent_ = strdup(argv[2]);
      app_params_ = strdup(argv[3]);
      app_callback_ = strdup(argv[4]);
      app_recv_ = atoi(argv[5]);
      app_reuse_ = atoi(argv[6]);
      return (TCL_OK);
    }
  }
  return (TcpAgent::command(argc, argv));
}

void TcpListenerAgent::reset_state() {

  closed_ = 0;
  pipe_ = -1;
  rtxbytes_ = 0;
  fastrecov_ = FALSE;
  last_send_time_ = -1.0;
  infinite_send_ = FALSE;
  irs_ = -1;
  maxseq_ = -1;
  flags_ = 0;
  ect_ = FALSE;
  recent_ce_ = FALSE;
  last_ack_sent_ = -1;
  cancel_timers();
  rq_.clear();
  reset();
  last_state_ = TCPS_CLOSED;
  state_ = TCPS_LISTEN;
  type_ = PT_ACK;
}

void TcpListenerAgent::add_connection(ipaddr_t src_ip, ipportaddr_t sport) {

  ip_info_t *ip_info = new ip_info_t;
  ip_info->pAgent = NULL;
  ip_info->connect_time = now();
  ip_info->est = false;

  cmap_.insert(cmap_pt(cmap_kt(src_ip, sport), ip_info));
  connections_++;

  if (debug_ > 2) {
    printf("%f: TcpListenerAgent(%s): cmap_ list:\n", now(), agent_name);
    for (cmap_it it = cmap_.begin(); it != cmap_.end(); it++)
      printf("  srcip %08x sport %d ct %f\n", ((*it).first).first,
          ((*it).first).second, ((*it).second)->connect_time);
  }
  // Trace this data to out file
  if (trace_enable_ > 0) {
    trace_pktcount_ = 0;
    if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
      printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
          now(), agent_name, trace_file_);
      printf("  for writing. Disabling tracefile output.\n");
      trace_enable_ = 0;
    } else {
      fprintf(trace_fp_, "METRIC TCPTableSize %s.%s %f %d %d\n",
          trace_prefix_, trace_nodename_, now(), connections_, table_size_);
      fclose(trace_fp_);
    }
  }
}

void TcpListenerAgent::expire_connections() {
  
  double diff = 0, simtime = now();

  for (cmap_it it = cmap_.begin(); it != cmap_.end(); it++) {
    if ((*it).second->est == false) {
      diff = simtime - (*it).second->connect_time;
      if (diff >= entry_expire_) {
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): expired connection, deleting...\n",
              now(), agent_name);
        }
        del_connection((*it).first.first, (*it).first.second);
      }
    }
  }
}

bool TcpListenerAgent::get_connection(ipaddr_t src_ip, ipportaddr_t sport) {
  
  cmap_it it = cmap_.find(cmap_kt(src_ip, sport));
  
  if (it != cmap_.end()) {
    return true;
  }
  return false;
}

void TcpListenerAgent::del_connection(ipaddr_t src_ip, ipportaddr_t sport) {
  
  cmap_it it = cmap_.find(cmap_kt(src_ip, sport));
  
  delete (*it).second;
  cmap_.erase(cmap_kt(src_ip, sport));
  connections_--;
  if (debug_ > 1) {
    printf("%f: TcpListenerAgent(%s): del_connection():\n",
        now(), agent_name);
    printf("  srcip %08x sport %d deleted\n", src_ip, sport);
  }
  if (debug_ > 2) {
    printf("%f: TcpListenerAgent(%s): cmap_ list:\n", now(), agent_name);
    for (cmap_it it = cmap_.begin(); it != cmap_.end(); it++)
      printf("  srcip %08x sport %d ct %f\n", ((*it).first).first,
          ((*it).first).second, ((*it).second)->connect_time);
  }
  // Trace this data to out file
  if (trace_enable_ > 0) {
    trace_pktcount_ = 0;
    if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
      printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
          now(), agent_name, trace_file_);
      printf("  for writing. Disabling tracefile output.\n");
      trace_enable_ = 0;
    } else {
      fprintf(trace_fp_, "METRIC TCPTableSize %s.%s %f %d %d\n",
          trace_prefix_, trace_nodename_, now(), connections_, table_size_);
      fclose(trace_fp_);
    }
  }
}

bool TcpListenerAgent::add_agentinfo(ipaddr_t src_ip, ipportaddr_t sport, 
    Agent *agent_add) {
  
  cmap_it it = cmap_.find(cmap_kt(src_ip, sport));

  if (it != cmap_.end()) {
    ((*it).second)->pAgent = agent_add;
    ((*it).second)->est = true; // Also flip the connection to established
    if (debug_ > 1) {
      printf("%f: TcpListenerAgent(%s): add_agentinfo() added %s\n", 
          now(), agent_name, ((*it).second)->pAgent->name());
    }
    return true;
  }
  return false;
}

Agent *TcpListenerAgent::get_agentinfo(ipaddr_t src_ip, ipportaddr_t sport) {
  
  cmap_it it = cmap_.find(cmap_kt(src_ip, sport));

  if (it != cmap_.end()) {
    return ((*it).second)->pAgent;
  }
  return NULL;
}

void TcpListenerAgent::inherit_attributes(TcpListenerAgent *a) {

  reset();
  // First get TcpAgent attributes
  wnd_ = a->wnd_; 
  wnd_init_ = a->wnd_init_;
  wnd_init_option_ = a->wnd_init_option_;
  wnd_option_ = a->wnd_option_;
  wnd_const_ = a->wnd_const_;
  wnd_th_ = a->wnd_th_;
  delay_growth_ = a->delay_growth_;
  overhead_ = a->overhead_;
  tcp_tick_ = a->tcp_tick_;
  ecn_ = a->ecn_;
  old_ecn_ = a->old_ecn_;
  eln_ = a->eln_;
  eln_rxmit_thresh_ = a->eln_rxmit_thresh_;
  size_ = a->size_;
  tcpip_base_hdr_size_ = a->tcpip_base_hdr_size_;
  ts_option_size_ = a->ts_option_size_;
  bug_fix_ = a->bug_fix_;
  less_careful_ = a->less_careful_;
  ts_option_ = a->ts_option_;
  slow_start_restart_ = a->slow_start_restart_;
  restart_bugfix_ = a->restart_bugfix_;
  maxburst_ = a->maxburst_;
  maxcwnd_ = a->maxcwnd_;
  numdupacks_ = a->numdupacks_;
  numdupacksFrac_ = a->numdupacksFrac_;
  maxrto_ = a->maxrto_;
  minrto_ = a->minrto_;
  srtt_init_ = a->srtt_init_;
  rttvar_init_ = a->rttvar_init_;
  rtxcur_init_ = a->rtxcur_init_;
  T_SRTT_BITS = a->T_SRTT_BITS;
  T_RTTVAR_BITS = a->T_RTTVAR_BITS;
  rttvar_exp_ = a->rttvar_exp_;
  awnd_ = a->awnd_;
  decrease_num_ = a->decrease_num_;
  increase_num_ = a->increase_num_;
  k_parameter_ = a->k_parameter_;
  l_parameter_ = a->l_parameter_;
  trace_all_oneline_ = a->trace_all_oneline_;
  nam_tracevar_ = a->nam_tracevar_;
  QOption_ = a->QOption_;
  EnblRTTCtr_ = a->EnblRTTCtr_;
  control_increase_ = a->control_increase_;
  noFastRetrans_ = a->noFastRetrans_;
  precision_reduce_ = a->precision_reduce_;
  oldCode_ = a->oldCode_;
  useHeaders_ = a->useHeaders_;
  low_window_ = a->low_window_;
  high_window_ = a->high_window_;
  high_p_ = a->high_p_;
  high_decrease_ = a->high_decrease_;
  max_ssthresh_ = a->max_ssthresh_;
  cwnd_frac_ = a->cwnd_frac_;
  timerfix_ = a->timerfix_;
  rfc2988_ = a->rfc2988_;
  singledup_ = a->singledup_;
  rate_request_ = a->rate_request_;
  qs_enabled_ = a->qs_enabled_;
  // Now get FullTcpAgent attributes
  segs_per_ack_ = a->segs_per_ack_;
  maxseg_ = a->maxseg_;
  tcprexmtthresh_ = a->tcprexmtthresh_;
  iss_ = a->iss_;
  spa_thresh_ = a->spa_thresh_;
  nodelay_ = a->nodelay_;
  data_on_syn_ = a->data_on_syn_;
  dupseg_fix_ = a->dupseg_fix_;
  dupack_reset_ = a->dupack_reset_;
  signal_on_empty_ = a->signal_on_empty_;
  delack_interval_ = a->delack_interval_;
  ts_option_size_ = a->ts_option_size_;
  reno_fastrecov_ = a->reno_fastrecov_;
  pipectrl_ = a->pipectrl_;
  open_cwnd_on_pack_ = a->open_cwnd_on_pack_;
  halfclose_ = a->halfclose_;
  nopredict_ = a->nopredict_;
}

void TcpListenerAgent::promote() {
  
  close_on_empty_ = 0; // force close_on_empty false
  reset();
  last_send_time_ = -1.0; 
  last_ack_sent_ += 2;
  rcv_nxt_ += 2;
  pipe_ += 1;
  rtxbytes_ += 1;
  flags_ = 0;
  irs_ += 1;
  iss_ = 0;
  t_seqno_ = iss_ + 1;
  highest_ack_ = iss_;
  maxseq_ += 2;
  curseq_ = maxseq_ - 1;
  recent_ = 0;
  recent_age_ = 0;
  nackpack_ = 1;
  type_ = (packet_t)5;
  last_state_ = 4;
  state_ = 4;
  cwnd_ = 1;
}

void TcpListenerAgent::set_src(ipaddr_t ip, ipportaddr_t port) {
  my_ipaddr_ = ip;
  my_port_ = port;
}

void TcpListenerAgent::recv(Packet *pkt, Handler*) {
  
  hdr_tcp *tcph = hdr_tcp::access(pkt);
  hdr_cmn *th = hdr_cmn::access(pkt);
  hdr_flags *fh = hdr_flags::access(pkt);
  hdr_rti *rti = hdr_rti::access(pkt);

  int datalen = th->size() - tcph->hlen();
  int tiflags = tcph->flags();
  Tcl &tcl = Tcl::instance();

  ipaddr_t src_ip = rti->ipsrc();
  ipaddr_t dst_ip = rti->ipdst();
  ipportaddr_t sport = rti->ipsrcport();
  ipportaddr_t dport = rti->ipdstport();

  // Store Node info for future reference
  if (!pNode) {
    pNode = (Node *)GetLocalIP(dst_ip);
    if (!pNode) {
      tcl.evalf("%s set node_", name());
      strcpy(node_name, tcl.result());
      pNode = (Node *)TclObject::lookup(node_name);
      if (!pNode) {
        printf("%f: TcpListenerAgent(%s): FATAL: Node lookup failed!\n",
            now(), name());
        exit(1);
      }
    } else {
      strcpy(node_name, pNode->name());
    }
    strcpy(agent_name, name());
    if (trace_enable_ > 0) {
      tcl.evalf("%s set id_", node_name);
      trace_nodename_ = strdup(tcl.result());
    }
  }
  
  if (debug_ > 1) {
    printf("%f: TcpListenerAgent(%s): incoming %s packet:\n", now(), 
        agent_name, flagstr(tiflags));
    printf("  srcip %08x sport %d dstip %08x dport %d\n", src_ip, sport,
        dst_ip, dport);
    printf("  ts %d conn %d tiflags %d datalen %d seqno %d ackno %d\n",
        table_size_, connections_, tiflags, datalen, tcph->seqno(),
        tcph->ackno());
  }
  // Increment trace packet counter
  if (trace_enable_ > 0) {
    trace_pktcount_++;
  }
  
  // Incoming SYN Packet (passive open)
  if (tiflags & TH_SYN) {
    // Trace this data to out file
    if (trace_enable_ > 1 && trace_pktcount_ % trace_interval_ == 0) {
      trace_pktcount_ = 0;
      if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
        printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
            now(), agent_name, trace_file_);
        printf("  for writing. Disabling tracefile output.\n");
        trace_enable_ = 0;
      } else {
        fprintf(trace_fp_, "PACKET * %s.%s %f %s 1.0\n", trace_prefix_,
            trace_nodename_, now(), COLOR_SYN);
        fclose(trace_fp_);
      }
    }
#ifdef HAVE_FILTER
    if (filter_enable_ == 1) {
      if ((double)connections_ / (double)table_size_ >= filter_trigger_) {
        if (max_synack_retries_ == 0) {
          expire_connections();
        }
        if ((double)connections_ / (double)table_size_ >= filter_trigger_) {
          if (filter_on_ == false) {
            filter_on_ = true;
            SendOOBFilter(RTIFilterStart);
            if (debug_ > 0) {
              printf("%f: TcpListenerAgent(%s): Filter Triggered! (%.1f%)\n",
                 now(), agent_name, filter_trigger_ * 100);
            }
          }
        } else if (((double)connections_ / (double)table_size_ <= 
              filter_release_) && (filter_on_ == true)) {
          filter_on_ = false;
          SendOOBFilter(RTIFilterStop);
          if (debug_ > 0) {
            printf("%f: TcpListenerAgent(%s): Filter Released! (%.1f%)\n",
               now(), agent_name, filter_release_ * 100);
          }
        } // release else
      } // trigger check
    } // filter_enable_
#endif
    // Check TCP Connection Table first
    if (connections_ >= table_size_) {
      if (max_synack_retries_ == 0) {
        expire_connections();
      }
      if (connections_ >= table_size_) {
        if (debug_ > 0) {
          printf("%f: TcpListenerAgent(%s): TCP connection table full (%d)\n",
             now(), agent_name, table_size_);
        }
      } else {
        goto cont;
      }
    } else {
cont:
      if (debug_ > 1) {
        printf("%f: TcpListenerAgent(%s): SYN packet recv'd\n", now(), 
            agent_name);
      }
      // Allocate connection information
      if (get_connection(src_ip, sport) == false) {
        add_connection(src_ip, sport);
      }
      // Send SYN+ACK packet back to src_ip
      dooptions(pkt);
      irs_ = tcph->seqno();
      t_seqno_ = iss_;
      rcv_nxt_ = rcvseqinit(irs_, datalen);
      flags_ |= TF_ACKNOW;
      tcph->seqno()++;
      cwnd_ = initial_window();
      newstate(TCPS_SYN_RECEIVED);
      // Reconnect back to remote sources
      NsObject *pObj = GetLocalIP(src_ip);
      Agent *tmpAgent = GetIPRoute(src_ip, dst_ip);
      if (pObj == NULL || tmpAgent != NULL) {
        tcl.evalf("%s set-dst-ipaddr %d; %s set-dst-port %d; [[Simulator instance] set scheduler_] iproute-connect %s %d %d", agent_name, src_ip, agent_name, sport, agent_name, src_ip, sport);
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): remote iproute-connect (%08x:%d)\n",
              now(), agent_name, src_ip, sport);
        }
      } else {
        char tmp[255];
        tcl.evalf("%s findport %d", pObj->name(), sport);
        strcpy(tmp, tcl.result());
        if (strcmp(tmp, "") == 0) {
          tcl.evalf("%s get-droptarget", pObj->name());
          strcpy(tmp, tcl.result());
        }
        tcl.evalf("[Simulator instance] connect %s %s", agent_name, tmp);
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): local connect (%08x:%d)\n",
              now(), agent_name, src_ip, sport);
        }
      }
      dst_ipaddr_ = src_ip;
      dst_ipport_ = sport;
      send_much(1, REASON_NORMAL, maxburst_);
      // Add timeout to map
      if (max_synack_retries_ > 0) {
        add_timeout(rtt_timeout(), src_ip, sport, 0);
        schedule_timeout();
      }
      // Trace this data to out file
      if (trace_enable_ > 1 && trace_pktcount_ % trace_interval_ == 0) {
        trace_pktcount_ = 0;
        if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
          printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
              now(), agent_name, trace_file_);
          printf("  for writing. Disabling tracefile output.\n");
          trace_enable_ = 0;
        } else {
          fprintf(trace_fp_, "PACKET %s.%s * %f %s\n", trace_prefix_,
              trace_nodename_, now(), COLOR_SYNACK);
          fclose(trace_fp_);
        }
      }
    }
    Packet::free(pkt);
  } else if (tiflags == 16 || ((tiflags & TH_ACK) && (tiflags & TH_PUSH))) {
    // ACK for our SYN+ACK only if an existing connection doesn't exist yet
    if (get_connection(src_ip, sport) == true) {
      Agent *pAgent = get_agentinfo(src_ip, sport);
      if (!pAgent) {
        char ft_an[255];
        bool no_attach = false;
        // No existing active connection, create new
        if (prebinding_ != 0 && (fulltcp_use_ < fulltcp_num_)) {
          pAgent = fulltcp_[fulltcp_use_];
          fulltcp_use_++;
          no_attach = true;
        } else {
          tcl.evalf("new Agent/TCP/FullTcp");
          pAgent = (Agent *)TclObject::lookup(tcl.result());
          // Set the the source ip for the new agent as my ip address
          ((TcpListenerAgent *)pAgent)->set_src(dst_ip, dport);
        }
        if (!pAgent) {
          printf("%f: TcpListenerAgent(%s): FATAL: Can't create FullTcpAgent\n",
              now(), agent_name);
          exit(1);
        }
        strcpy(ft_an, pAgent->name());
        // Attach agent to node and connect to source ip
        if (no_attach == false) {
          tcl.evalf("[Simulator instance] attach-agent %s %s", node_name,
              ft_an);
        }
        NsObject *pObj = GetLocalIP(src_ip);
        Agent *tmpAgent = GetIPRoute(src_ip, dst_ip);
        if (pObj != NULL && tmpAgent == NULL) {
          char tmp[255];
          tcl.evalf("%s findport %d", pObj->name(), sport);
          strcpy(tmp, tcl.result());
          if (strcmp(tmp, "") == 0) {
            // This should never happen!... but just in case
            tcl.evalf("%s get-droptarget", pObj->name());
            strcpy(tmp, tcl.result());
          }
          tcl.evalf("[Simulator instance] connect %s %s", ft_an, tmp);
        } else {
          tcl.evalf("%s set-dst-ipaddr %d; %s set-dst-port %d; [[Simulator instance] set scheduler_] iproute-connect %s %d %d", ft_an, src_ip, ft_an, sport, ft_an, src_ip, sport);
        }
        // Attach a custom application if available
        if (app_agent_ != NULL) {
          if (app_reuse_ != 0) {
            if (app_pApp_ == NULL) {
              if (strcmp(app_params_, "0") != 0) {
                tcl.evalf("new %s %s", app_agent_, app_params_);
              } else {
                tcl.evalf("new %s", app_agent_);
              }
              app_pApp_ = (NsObject *)TclObject::lookup(tcl.result());
              // Perform application callback
              if (strcmp(app_callback_, "0") != 0) {
                tcl.evalf("%s %s %s", app_pApp_->name(), app_callback_, ft_an);
              }
              if (app_recv_ != 0) {
                tcl.evalf("%s enable-recv", app_pApp_->name());
              }
            }
            tcl.evalf("%s attach-agent %s", app_pApp_->name(), ft_an);
          } else {
            if (strcmp(app_params_, "0") != 0) {
              tcl.evalf("new %s %s", app_agent_, app_params_);
            } else {
              tcl.evalf("new %s", app_agent_);
            }
            NsObject *pApp = (NsObject *)TclObject::lookup(tcl.result());
            // Perform application callback
            if (strcmp(app_callback_, "0") != 0) {
              tcl.evalf("%s %s %s", pApp->name(), app_callback_, ft_an);
            }
            if (app_recv_ != 0) {
              tcl.evalf("%s enable-recv", pApp->name());
            }
            tcl.evalf("%s attach-agent %s", pApp->name(), ft_an);
          }
        }
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): FullTcpAgent %s attached to %s\n",
              now(), agent_name, ft_an, node_name);
          printf("  rconnected to %08x on port %d\n", src_ip, sport);
        }
        // Promote FullTcpAgent to TCPS_ESTABLISHED
        ((TcpListenerAgent *)pAgent)->inherit_attributes(this);
        ((TcpListenerAgent *)pAgent)->promote();
        if (add_agentinfo(src_ip, sport, pAgent) == false) {
          printf("%f: TcpListenerAgent(%s): FATAL: pAgent add to cmap failed\n",
              now(), agent_name);
          exit(1);
        }
        Packet::free(pkt);
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): 3-way handshake complete ",
              now(), agent_name);
          printf("with %08x\n", src_ip);
        }
        // Modify timeout entry that connection was established
        if (max_synack_retries_ > 0) {
          est_timeout(src_ip, sport);
        }
        // Trace this data to out file
        if (trace_enable_ > 1 && trace_pktcount_ % trace_interval_ == 0) {
          trace_pktcount_ = 0;
          if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
            printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
                now(), agent_name, trace_file_);
            printf("  for writing. Disabling tracefile output.\n");
            trace_enable_ = 0;
          } else {
            fprintf(trace_fp_, "PACKET * %s.%s %f %s\n", trace_prefix_,
                trace_nodename_, now(), COLOR_ACK);
            fclose(trace_fp_);
          }
        }
      } else {
        // forward packet to FullTcpAgent
        goto handoff;
      }
    }
  } else {
handoff:
    // forward packet to FullTcpAgent
    Agent *pAgent = get_agentinfo(src_ip, sport);
    if (!pAgent) {
      printf("%f: TcpListenerAgent(%s): FATAL: get_agentinfo() failed on %s packet\n", now(), agent_name, flagstr(tiflags));
      printf("  srcip %08x sport %d dstip %08x dport %d\n", src_ip, sport,
          dst_ip, dport);
      printf("  ts %d conn %d tiflags %d datalen %d seqno %d ackno %d\n",
          table_size_, connections_, tiflags, datalen, tcph->seqno(),
          tcph->ackno());
      exit(1);
    }
    if (debug_ > 1) {
      printf("%f: TcpListenerAgent(%s): packet handoff to ",
          now(), agent_name);
      printf("FullTcpAgent::recv()\n");
    }
    // Trace this data to out file
    if (trace_enable_ > 2 && trace_pktcount_ % trace_interval_ == 0) {
      trace_pktcount_ = 0;
      if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
        printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
            now(), agent_name, trace_file_);
        printf("  for writing. Disabling tracefile output.\n");
        trace_enable_ = 0;
      } else {
        fprintf(trace_fp_, "PACKET * %s.%s %f %s\n", trace_prefix_,
            trace_nodename_, now(), COLOR_DATA);
        fclose(trace_fp_);
      }
    }
    pAgent->recv(pkt, (Handler*)NULL);
    // Check to see if the connection has been closed
    if (((TcpListenerAgent *)pAgent)->get_state() == TCPS_CLOSED) {
      del_connection(src_ip, sport);
      if (debug_ > 1) {
        printf("%f: TcpListenerAgent(%s): TCP connection closed with %08x\n",
            now(), agent_name, src_ip);
        printf("  ts %d conn %d\n", table_size_, connections_);
      }
    } else if (trace_enable_ > 2 && trace_pktcount_ % trace_interval_ == 0) {
      trace_pktcount_ = 0;
      if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
        printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
            now(), agent_name, trace_file_);
        printf("  for writing. Disabling tracefile output.\n");
        trace_enable_ = 0;
      } else {
        fprintf(trace_fp_, "PACKET %s.%s * %f %s\n", trace_prefix_,
            trace_nodename_, now(), COLOR_DATA);
        fclose(trace_fp_);
      }
    }
  }
  reset_state();
}

void TcpListenerAgent::add_timeout(double to, ipaddr_t src_ip, 
    ipportaddr_t sport, int retries) {

  timeout_info_t *to_info = new timeout_info_t;
  to_info->src_ip = src_ip;
  to_info->sport = sport;
  to_info->retries = retries;
  to_info->est = false;

  tomap_.insert(tomap_pt(now() + to, to_info));

  if (debug_ > 2) {
    printf("%f: TcpListenerAgent(%s): tomap_ list:\n", now(), agent_name);
    for (tomap_it it = tomap_.begin(); it != tomap_.end(); it++)
      printf("  to %f srcip %08x sport %d\n",
          (*it).first, ((*it).second)->src_ip, ((*it).second)->sport);
  }
}

void TcpListenerAgent::schedule_timeout() {
  
  double simtime = now();
  if (!tomap_.empty()) {
    tomap_it it;
    // First check to see if we really need to reschedule
    it = tomap_.begin();
    if (current_to_ > (*it).first || current_to_ == 0.0) {
      // Cancel any outstanding timeouts
      ltimer_.force_cancel();
      for (it = tomap_.begin(); it != tomap_.upper_bound(simtime); it++) {
        // If the connection is established or timeout is in the past, remove
        if (((*it).second->est == true) || ((*it).first < simtime)) {
          // Delete timeout
          delete (*it).second;
          tomap_.erase(it);
          if (tomap_.empty())
            return;
        }
      }
      it = tomap_.begin(); // re-get first entry
      if (debug_ > 1) {
        printf("%f: TcpListenerAgent(%s): scheduling timeout at %f,",
            simtime, agent_name, (*it).first);
        printf(" retries %d\n", ((*it).second)->retries);
      }
      ltimer_.resched((*it).first - simtime); // resched wants offset from now
      current_to_ = (*it).first;
    } // if current_to_ > (*it).first
  } // if !tomap_.empty()
}

void TcpListenerAgent::est_timeout(ipaddr_t src_ip, ipportaddr_t sport) {

  // Now modify the tomap_ est bool
  for (tomap_it it = tomap_.begin(); it != tomap_.end(); it++) {
    if ( ((*it).second)->src_ip == src_ip &&
        ((*it).second)->sport == sport ) {
      ((*it).second)->est = true;
      if (debug_ > 1) {
        printf("%f: TcpListenerAgent(%s): tomap est flipped\n", now(),
            agent_name);
        printf("  to %f srcip %08x sport %d est %d\n", (*it).first,
            ((*it).second)->src_ip, ((*it).second)->sport, ((*it).second)->est);
      }
      break;
    }
  }
}

void TcpListenerAgent::timeout() {

  current_to_ = 0.0; // timeout in progress, reset current timeout
  // Find map entry at now()
  tomap_it it;
  for (it = tomap_.find(now()); it != tomap_.end(); it = tomap_.find(now())) {
    if ( ((*it).second->est == true) || ((*it).first < now()) ) {
      // Connection already established, remove from tomap
      if (debug_ > 1) {
        printf("%f: TcpListenerAgent(%s): active connection exists (%08x:%d)\n",
            now(), agent_name, (*it).second->src_ip, (*it).second->sport);
      }
      delete (*it).second;
      tomap_.erase(it);
    } else {
      // Retry SYN+ACK packet if we haven't reached max attempts
      Tcl &tcl = Tcl::instance();
      ipaddr_t src_ip = (*it).second->src_ip;
      ipportaddr_t sport = (*it).second->sport;
      int retries = (*it).second->retries;
      retries++; // Increment retry counter
      // Delete current timeout entry
      delete (*it).second;
      tomap_.erase(it);
      // See if we are over the SYN+ACK retry limit
      if (retries > max_synack_retries_) {
        // Maximum SYN+ACK retry limit hit, delete connection info
        del_connection(src_ip, sport);
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): Max SYN+ACK retries (%d)\n",
              now(), agent_name, max_synack_retries_);
          printf("  tomap_ %d srcip %08x sport %d removed\n",
              tomap_.size(), src_ip, sport);
        }
        continue;
      }
      // Retransmit another SYN+ACK packet back to src_ip
      if (debug_ > 1) {
        printf("%f: TcpListenerAgent(%s): Retrying SYN+ACK to %08x %d\n",
            now(), agent_name, src_ip, sport);
      }
      // Construct and send packet
      reset_state();
      t_seqno_ = iss_;
      rcv_nxt_ = rcvseqinit(irs_, 0);
      flags_ |= TF_ACKNOW;
      ++nrexmit_;
      recover_ = maxseq_;
      cwnd_ = initial_window();
      slowdown(CLOSE_SSTHRESH_HALF|CLOSE_CWND_RESTART);
      reset_rtx_timer(1);
      t_seqno_ = (highest_ack_ < 0) ? iss_ : int(highest_ack_);
      fastrecov_ = FALSE;
      dupacks_ = 0;
      newstate(TCPS_SYN_RECEIVED);
      // Reconnect back to the remote sources
      NsObject *pObj = GetLocalIP(src_ip);
      Agent *tmpAgent = GetIPRoute(src_ip, my_ipaddr_);
      if (pObj == NULL || tmpAgent != NULL) {
        tcl.evalf("%s set-dst-ipaddr %d; %s set-dst-port %d; [[Simulator instance] set scheduler_] iproute-connect %s %d %d", agent_name, src_ip, agent_name, sport, agent_name, src_ip, sport);
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): remote iproute-connect (%08x:%d)\n",
              now(), agent_name, src_ip, sport);
        }
      } else {
        char tmp[255];
        tcl.evalf("%s findport %d", pObj->name(), sport);
        strcpy(tmp, tcl.result());
        if (strcmp(tmp, "") == 0) {
          tcl.evalf("%s get-droptarget", pObj->name());
          strcpy(tmp, tcl.result());
        }
        tcl.evalf("[Simulator instance] connect %s %s", agent_name, tmp);
        if (debug_ > 1) {
          printf("%f: TcpListenerAgent(%s): local connect (%08x:%d)\n",
              now(), agent_name, src_ip, sport);
        }
      }
      // Set IP info since we are coming from a timeout
      dst_ipaddr_ = src_ip;
      dst_ipport_ = sport;
      send_much(1, PF_TIMEOUT, maxburst_);
      // Add new timeout entry and schedule
      for (int i = 0; i < retries; i++)
        rtt_backoff();
      add_timeout(rtt_timeout(), src_ip, sport, retries);
      // Trace this data to out file
      if (trace_enable_ > 0) {
        trace_pktcount_++;
      }
      if (trace_enable_ > 1 && trace_pktcount_ % trace_interval_ == 0) {
        trace_pktcount_ = 0;
        if ((trace_fp_ = fopen(trace_file_, "a")) == NULL) {
          printf("%f: TcpListenerAgent(%s): cannot open tracefile (%s)\n",
              now(), agent_name, trace_file_);
          printf("  for writing. Disabling tracefile output.\n");
          trace_enable_ = 0;
        } else {
          fprintf(trace_fp_, "PACKET %s.%s * %f %s\n", trace_prefix_,
              trace_nodename_, now(), COLOR_SYNACK);
          fclose(trace_fp_);
        }
      }
    }
  }
  schedule_timeout();
  reset_state();
}

void ListenerTimer::expire(Event*) {
  a_->timeout();
}

