/*-------------------------------------------------------------------------*/
/* SocketTcp: subclass of AdvwTcpAgent that implements the services        */
/* provided by real system TCP socket, e.g.,                               */
/*      limited sender buffer,                                             */
/*      spawning new connections on request,                               */
/*      real payload transfer.                                             */ 
/* Author: Qi He <http://www.cc.gatech.edu/~qhe> 01Aug2003                 */
/* $Revision:$ $Name:$ $Date:$                                             */
/*-------------------------------------------------------------------------*/
#include "tcp-sock.h"
#include "ip.h"
#include "flags.h"

#include "nssocket.h"

#ifdef PDNS
extern NsObject* GetLocalIP(ipaddr_t ipaddr);
#endif

#define MIN(a, b)	((a) < (b) ? (a) : (b))
static int pfcnt=0, pdfcnt=0, pcnt=0, opcnt=0, apcnt=0, dropcnt=0;

static class SocketTcpClass: public TclClass {
public:
	SocketTcpClass(): TclClass("Agent/TCP/FullTcp/AdvwTcp/SocketTcp") {}
	TclObject* create(int, const char*const*argv) {
		return (new SocketTcp());
	}
} class_socket_tcp;

PktDataEntry::PktDataEntry(int seqno, PacketData *p) {
  key_ = seqno;

  if(p)
    pktdata_ = (PacketData *)p->copy(); 
  else
    pktdata_ = NULL;

}

SocketTcp::SocketTcp(): AdvwTcpAgent() {
	bind("inf_rcv_", &infinite_rcv_);
	bind("snd_wnd_", &snd_wnd_);
	listen_only_ = FALSE; 
}

int SocketTcp::command(int argc, const char*const* argv) {
	if(argc==3) {
		if(strcmp(argv[1], "listen")==0) {
			listen(atoi(argv[2]));
			return TCL_OK;
		}
		if(strcmp(argv[1], "send-dummy")==0) {
			send_dummy(atoi(argv[2]));
			return TCL_OK;
		}
	}

	return AdvwTcpAgent::command(argc, argv);
}	

int SocketTcp::send_dummy(int nbytes) {
	PacketData *p = new PacketData(nbytes);
	sendmsg(p, nbytes);	
}

int SocketTcp::sendmsg(PacketData *p, int len) {
  int res;

  res = r_sendmsg(p, 0);
  if(p)
    delete p;
  return res;
}

int SocketTcp::r_send(int bytes) {
  return r_sendmsg(bytes, NULL);
}

int SocketTcp::r_send(PacketData *p) {
  return r_sendmsg(p, 0);
}


int SocketTcp::r_sendmsg(int nbytes, const char *flags = 0) {
	if (flags && strcmp(flags, "MSG_EOF") == 0) 
		close_on_empty_ = TRUE;
	if (nbytes == -1) {
		infinite_send_ = TRUE;
		return r_advance_bytes(0);
	} else
		return r_advance_bytes(nbytes);
}

int SocketTcp::r_sendmsg(PacketData *p, const char *flags = 0 ) {
	if (flags && strcmp(flags, "MSG_EOF") == 0) 
		close_on_empty_ = TRUE;
	
	return r_advance_pkt(p);
}

void SocketTcp::newack(Packet* pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt);

	register int ackno = tcph->ackno();
	int progress = (ackno > highest_ack_);

	if (ackno == maxseq_) {
		cancel_rtx_timer();	// all data ACKd
	} else if (progress) {
		set_rtx_timer();
	}

	//advance the ack number if this is for new data
	if (progress) {
		highest_ack_ = ackno;

		Islist_iter<PktDataEntry> pkt_iter(snd_buf_);
		PktDataEntry *cur, *last=NULL;

		while((cur=pkt_iter())!=NULL) {
		  if(cur->key_ < ackno) {
		    delete cur->pktdata_;
		    snd_buf_.remove(cur, last);
		  } else
		    break;
		}
		if(app_) {
		  ((NSSocket *)app_)->upcall_send();
		}

	}

	// if we have suffered a retransmit timeout, t_seqno_
	// will have been reset to highest_ ack.  If the
	// receiver has cached some data above t_seqno_, the
	// new-ack value could (should) jump forward.  We must
	// update t_seqno_ here, otherwise we would be doing
	// go-back-n.

	if (t_seqno_ < highest_ack_)
		t_seqno_ = highest_ack_; // seq# to send next

        /*
         * Update RTT only if it's OK to do so from info in the flags header.
         * This is needed for protocols in which intermediate agents
         * in the network intersperse acks (e.g., ack-reconstructors) for
         * various reasons (without violating e2e semantics).
         */
        hdr_flags *fh = hdr_flags::access(pkt);
        if (!fh->no_ts_) {
                if (ts_option_) {
			recent_age_ = now();
			recent_ = tcph->ts();
                        rtt_update(now() - tcph->ts_echo());
		} else if (rtt_active_ && ackno > rtt_seq_) {
			// got an RTT sample, record it
                        t_backoff_ = 1;
                        rtt_active_ = FALSE;
			rtt_update(now() - rtt_ts_);
                }
        } else {
		printf("no_ts\n");
	}
	return;
}

void SocketTcp::output(int seqno, int reason)
{
	int quiet = (highest_ack_ == maxseq_);
	int pflags = outflags();
	int syn = (seqno == iss_);
	int emptying_buffer = FALSE;
	int win = window() * maxseg_;	// window (in bytes)
	int datalen;
	PacketData *data = NULL;

	PktDataEntry *pe = snd_buf_.find(seqno);
	if(pe) {
	  data = pe->pktdata_;
	}

	if ((syn && !data_on_syn_) || pe==NULL || data==NULL) 
	  datalen = 0;
	else
	  datalen = data->size();
	//
	// this is an option that causes us to slow-start if we've
	// been idle for a "long" time, where long means a rto or longer
	// the slow-start is a sort that does not set ssthresh
	//

	if (slow_start_restart_ && quiet && datalen > 0) {
		if (idle_restart()) {
			slowdown(CLOSE_CWND_INIT);
		}
	}

	//
	// see if sending this packet will empty the send buffer
	// a dataless SYN packet counts also
	//
	if (!infinite_send_ && ((seqno + datalen) > curseq_ || 
	    (syn && datalen == 0))) {
		emptying_buffer = TRUE;
		//
		// if not a retransmission, notify application that 
		// everything has been sent out at least once.
		//
		if (!syn) {
			idle();
			if (close_on_empty_ && quiet) {
				flags_ |= TF_NEEDCLOSE;
			}
		}
		pflags |= TH_PUSH;
		//
		// if close_on_empty set, we are finished
		// with this connection; close it
		//
	} else  {
		/* not emptying buffer, so can't be FIN */
		pflags &= ~TH_FIN;
	}

	pflags |= TH_PUSH;

	/* sender SWS avoidance (Nagle) */
send:

	syn = (pflags & TH_SYN) ? 1 : 0;
	int fin = (pflags & TH_FIN) ? 1 : 0;

	sendpacket(seqno, rcv_nxt_, pflags, datalen, data, reason);
        /*      
         * Data sent (as far as we can tell).
         * Any pending ACK has now been sent.
         */      
	flags_ &= ~(TF_ACKNOW|TF_DELACK);

	/*
	 * if we have reacted to congestion recently, the
	 * slowdown() procedure will have set cong_action_ and
	 * sendpacket will have copied that to the outgoing pkt
	 * CACT field. If that packet contains data, then
	 * it will be reliably delivered, so we are free to turn off the
	 * cong_action_ state now  If only a pure ACK, we keep the state
	 * around until we actually send a segment
	 */

	int reliable = datalen + syn + fin; // seq #'s reliably sent
	if (cong_action_ && reliable > 0)
		cong_action_ = FALSE;

	/*
	 * SYNs and FINs each use up one sequence number, but
	 * there's no reason to advance t_seqno_ by one for a FIN
	 */

	if (!fin && seqno == t_seqno_) {
		t_seqno_ += reliable;
	}

	// highest: greatest sequence number sent + 1
	//	and adjusted for SYNs and FINs which use up one number

	int highest = seqno + reliable;
	if (highest > maxseq_) {
		maxseq_ = highest;
		//
		// if we are using conventional RTT estimation,
		// establish timing on this segment
		//
		if (!ts_option_ && rtt_active_ == FALSE) {
			rtt_active_ = TRUE;	// set timer
			rtt_seq_ = seqno; 	// timed seq #
			rtt_ts_ = now();	// when set
		}
	}

	/*
	 * Set retransmit timer if not currently set,
	 * and not doing an ack or a keep-alive probe.
	 * Initial value for retransmit timer is smoothed
	 * round-trip time + 2 * round-trip time variance.
	 * Future values are rtt + 4 * rttvar.
	 */
	if (rtx_timer_.status() != TIMER_PENDING && reliable) {
		set_rtx_timer();  // no timer pending, schedule one
	}

	if (flags_ & TF_NEEDCLOSE) {
		flags_ &= ~TF_NEEDCLOSE;
		if (state_ <= TCPS_ESTABLISHED && state_ != TCPS_CLOSED)
			usrclosed();
	}

	return;
}

void SocketTcp::recvBytes(int nbytes)
{
        int rcv_buf_free, byte_read=0, seqno, acksent = FALSE;
	PktDataEntry *cur, *last=NULL;
	PacketData *pkt;

	num_bytes_avail_ += nbytes;
	Islist_iter<PktDataEntry> pkt_iter(rcv_buf_);
	while(((cur=pkt_iter())!=NULL) && num_bytes_avail_ > 0) {
	  pkt = cur->pktdata_;
	  
	  if(app_) {
	    if(pkt==NULL) {
	      printf("ERROR: Rcv Buffer Entry without Pkt\n");
	      break;
	    }   
	    byte_read = ((NSSocket*)app_)->upcall_recv(pkt);
	  } else
	    byte_read = pkt->size();

	  if(byte_read==0) 
	    break;

	  num_bytes_avail_ -= byte_read;
	  num_bytes_req_ -= byte_read;
	  if (byte_read > 0) {
	    delete pkt;
	    rcv_buf_.remove(cur, last);
	  }
	
	  if(num_bytes_avail_<0) {
	    fprintf(stderr,"SocketTcp::recvBytes(): agent %d %d num_bytes_avail_ %d < 0\n", here_.addr_, here_.port_, num_bytes_avail_);
	    break;
	  }
	
	  rcv_buf_free = rcv_buff_ - num_bytes_avail_;
	  if(rcv_buf_free >= rcv_wnd_ + MIN (rcv_buff_/2, maxseg_) && !acksent){
	    rcv_wnd_ = rcv_buf_free;
	    acksent = TRUE;
	    sendpacket(0, rcv_nxt_, TH_ACK, 0, NULL, REASON_NORMAL);
	  }

	};
}

void SocketTcp::insert(Islist<PktDataEntry> *list, PktDataEntry *pe) {
  Islist_iter<PktDataEntry> pkt_iter(*list);
  PktDataEntry *last=NULL, *cur;
  
  while((cur=pkt_iter())!=NULL && cur->key_ < pe->key_) {
    last = cur;
  };
  
  if(last!=NULL) {
    if(cur==NULL) {
      list->append(pe);
      return;
    }
    if(cur->key_==pe->key_) {
      free(pe);
      return;
     }
    pe->next_ = last->next_;
     last->next_ = pe;
  } else {
    if(cur && cur->key_==pe->key_) {
      free(pe);
      return;
    }
    list->insert(pe);
  }
}

void SocketTcp::recv(Packet *pkt, Handler* handler)
{

       if(handler) { 
	 int nb = ((PacketData *)pkt->userdata())->size();
	 if(r_send(((PacketData *)pkt->userdata()))>0)
	   handler->handle(NULL);
	 return;
       }

        hdr_ip *iph = hdr_ip::access(pkt);
  	hdr_tcp *tcph = hdr_tcp::access(pkt);
	hdr_cmn *th = hdr_cmn::access(pkt);
	hdr_flags *fh = hdr_flags::access(pkt);

	int needoutput = FALSE;
	int ourfinisacked = FALSE;
	int dupseg = FALSE;
	int todrop = 0;

	last_state_ = state_;

	int datalen = th->size() - tcph->hlen(); // # payload bytes
	int ackno = tcph->ackno();		 // ack # from packet
	int tiflags = tcph->flags() ; 		 // tcp flags from packet
	
	if (state_ == TCPS_CLOSED)
		goto drop;

	if (state_ != TCPS_LISTEN)
		dooptions(pkt);

	if(!listen_only_) {
	  if (delack_interval_ > 0.0 &&
	      (delack_timer_.status() != TIMER_PENDING)) {
	    int last = int(now() / delack_interval_);
	    delack_timer_.resched(delack_interval_ * (last + 1.0) - now());
	  }
	}

	wnd_ = (double)tcph->advwin()/(double)size_;

	if (fh->ce() && !fh->ect()) {
	    fprintf(stderr,
	    "%f: FullTcpAgent::recv(%s): warning: CE bit on, but ECT false!\n",
		now(), name());
	}

	if(listen_only_ && state_ != TCPS_LISTEN) {
	  printf("listening socket not in TCPS_LISTEN\n");
	  return;
	} 
	switch (state_) {

        /*
         * If the segment contains an ACK then it is bad and do reset.
         * If it does not contain a SYN then it is not interesting; drop it.
         * Otherwise initialize tp->rcv_nxt, and tp->irs, iss is already
	 * selected, and send a segment:
         *     <SEQ=ISS><ACK=RCV_NXT><CTL=SYN,ACK>
         * Initialize tp->snd_nxt to tp->iss.
         * Enter SYN_RECEIVED state, and process any other fields of this
         * segment in this state.
         */

	case TCPS_LISTEN:	/* awaiting peer's SYN */
		if (tiflags & TH_ACK) {
			goto dropwithreset;
		}
		if ((tiflags & TH_SYN) == 0) {
			goto drop;
		}

	  if(listen_only_) {
	    if(app_)
	      ((NSSocket *)app_)->upcall_accept(pkt);
	    return;
	  }

		/*
		 * must by a SYN (no ACK) at this point...
		 * in real tcp we would bump the iss counter here also
		 */
		dooptions(pkt);
		irs_ = tcph->seqno();
		t_seqno_ = iss_; /* tcp_sendseqinit() macro in real tcp */
		rcv_nxt_ = rcvseqinit(irs_, datalen);
		flags_ |= TF_ACKNOW;
		if (ecn_ && fh->ecnecho()) {
			ect_ = TRUE;
		}

		newstate(TCPS_SYN_RECEIVED);
		goto trimthenstep6;

        /*
         * If the state is SYN_SENT:
         *      if seg contains an ACK, but not for our SYN, drop the input.
         *      if seg does not contain SYN, then drop it.
         * Otherwise this is an acceptable SYN segment
         *      initialize tp->rcv_nxt and tp->irs
         *      if seg contains ack then advance tp->snd_una
         *      if SYN has been acked change to ESTABLISHED else SYN_RCVD state
         *      arrange for segment to be acked (eventually)
         *      continue processing rest of data/controls, beginning with URG
         */

	case TCPS_SYN_SENT:	/* we sent SYN, expecting SYN+ACK (or SYN) */

		/* drop if it's a SYN+ACK and the ack field is bad */
		if ((tiflags & TH_ACK) &&
			((ackno <= iss_) || (ackno > maxseq_))) {
			// not an ACK for our SYN, discard
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s): bad ACK (%d) for our SYN(%d)\n",
			        now(), name(), int(ackno), int(maxseq_));
			goto dropwithreset;
		}

		if ((tiflags & TH_SYN) == 0) {
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s): no SYN for our SYN(%d)\n",
			        now(), name(), int(maxseq_));
			goto drop;
		}

		/* looks like an ok SYN or SYN+ACK */
#ifdef notdef
		cancel_rtx_timer();	// cancel timer on our 1st SYN [does this belong!?]
#endif
		irs_ = tcph->seqno();	// get initial recv'd seq #
		rcv_nxt_ = rcvseqinit(irs_, datalen);

		/*
		 * we are seeing either a SYN or SYN+ACK.  For pure SYN,
		 * ecnecho tells us our peer is ecn-capable.  For SYN+ACK,
		 * it's acking our SYN, so it already knows we're ecn capable,
		 * so it can just turn on ect
		 */
		if (ecn_ && (fh->ecnecho() || fh->ect()))
			ect_ = TRUE;

		if (tiflags & TH_ACK) {
			// SYN+ACK (our SYN was acked)
			// CHECKME
			highest_ack_ = ackno;
			cwnd_ = initial_window();
#ifdef notdef
			/*
			 * if we didn't have to retransmit the SYN,
			 * use its rtt as our initial srtt & rtt var.
			 */
			if (t_rtt_) {
			  double tao = now() - tcph->ts();
			  rtt_update(tao);
			}
#endif

			/*
			 * if there's data, delay ACK; if there's also a FIN
			 * ACKNOW will be turned on later.
			 */
			if (datalen > 0) {
				flags_ |= TF_DELACK;	// data there: wait
			} else {
				flags_ |= TF_ACKNOW;	// ACK peer's SYN
			}

                        /*
                         * Received <SYN,ACK> in SYN_SENT[*] state.
                         * Transitions:
                         *      SYN_SENT  --> ESTABLISHED
                         *      SYN_SENT* --> FIN_WAIT_1
                         */
			int newst = FALSE;
			if (flags_ & TF_NEEDFIN) {
				newstate(TCPS_FIN_WAIT_1);
				flags_ &= ~TF_NEEDFIN;
				tiflags &= ~TH_SYN;
			} else {
				newstate(TCPS_ESTABLISHED);
#ifdef PDNS
				hdr_rti *rtih = hdr_rti::access(pkt);
				dst_ipaddr_ = rtih->ipsrc();
				dst_ipport_ = rtih->ipsrcport();
				if(GetLocalIP(dst_ipaddr_))
				  dst_= iph->src();
#else
				dst_ = iph->src();
#endif
				newst = TRUE;
			}

			// special to ns:
			//  generate pure ACK here.
			//  this simulates the ordinary connection establishment
			//  where the ACK of the peer's SYN+ACK contains
			//  no data.  This is typically caused by the way
			//  the connect() socket call works in which the
			//  entire 3-way handshake occurs prior to the app
			//  being able to issue a write() [which actually
			//  causes the segment to be sent].
			//sendpacket(t_seqno_, rcv_nxt_, TH_ACK, 0, 0);
			sendpacket(t_seqno_, rcv_nxt_, TH_ACK, 0, NULL, 0);
			if(app_ && newst) 
			  ((NSSocket *)app_)->upcall_connected(pkt);
		} else {
			// SYN (no ACK) (simultaneous active opens)
			flags_ |= TF_ACKNOW;
			cancel_rtx_timer();
			newstate(TCPS_SYN_RECEIVED);
			/*
			 * decrement t_seqno_: we are sending a
			 * 2nd SYN (this time in the form of a
			 * SYN+ACK, so t_seqno_ will have been
			 * advanced to 2... reduce this
			 */
			t_seqno_--;	// CHECKME
		}

trimthenstep6:
		/*
		 * advance the seq# to correspond to first data byte
		 */
		tcph->seqno()++;

		if (tiflags & TH_ACK)
			goto process_ACK;

		goto step6;

	case TCPS_LAST_ACK:
	case TCPS_CLOSING:
		break;
	} /* end switch(state_) */

        /*
         * States other than LISTEN or SYN_SENT.
         * First check timestamp, if present.
         * Then check that at least some bytes of segment are within
         * receive window.  If segment begins before rcv_nxt,
         * drop leading data (and SYN); if nothing left, just ack.
         *
         * RFC 1323 PAWS: If we have a timestamp reply on this segment
         * and it's less than ts_recent, drop it.
         */

	if (ts_option_ && !fh->no_ts_ && recent_ && tcph->ts() < recent_) {
		if ((now() - recent_age_) > TCP_PAWS_IDLE) {
			/*
			 * this is basically impossible in the simulator,
			 * but here it is...
			 */
                        /*
                         * Invalidate ts_recent.  If this segment updates
                         * ts_recent, the age will be reset later and ts_recent
                         * will get a valid value.  If it does not, setting
                         * ts_recent to zero will at least satisfy the
                         * requirement that zero be placed in the timestamp
                         * echo reply when ts_recent isn't valid.  The
                         * age isn't reset until we get a valid ts_recent
                         * because we don't want out-of-order segments to be
                         * dropped when ts_recent is old.
                         */
			recent_ = 0.0;
		} else {
			goto dropafterack;
		}
	}

	// check for redundant data at head/tail of segment
	//	note that the 4.4bsd [Net/3] code has
	//	a bug here which can cause us to ignore the
	//	perfectly good ACKs on duplicate segments.  The
	//	fix is described in (Stevens, Vol2, p. 959-960).
	//	This code is based on that correction.
	//
	// In addition, it has a modification so that duplicate segments
	// with dup acks don't trigger a fast retransmit when dupseg_fix_
	// is enabled.
	//
	// Yet one more modification: make sure that if the received
	//	segment had datalen=0 and wasn't a SYN or FIN that
	//	we don't turn on the ACKNOW status bit.  If we were to
	//	allow ACKNO to be turned on, normal pure ACKs that happen
	//	to have seq #s below rcv_nxt can trigger an ACK war by
	//	forcing us to ACK the pure ACKs
	//
	todrop = rcv_nxt_ - tcph->seqno();  // how much overlap?

	if (todrop > 0 && ((tiflags & (TH_SYN|TH_FIN)) || datalen > 0)) {
		if (tiflags & TH_SYN) {
			tiflags &= ~TH_SYN;
			tcph->seqno()++;
			th->size()--;	// XXX Must decrease packet size too!!
			todrop--;
		}
		//
		// see Stevens, vol 2, p. 960 for this check;
		// this check is to see if we are dropping
		// more than this segment (i.e. the whole pkt + a FIN),
		// or just the whole packet (no FIN)
		//
		if (todrop > datalen ||
		    (todrop == datalen && (tiflags & TH_FIN) == 0)) {
                        /*
                         * Any valid FIN must be to the left of the window.
                         * At this point the FIN must be a duplicate or out
                         * of sequence; drop it.
                         */

			tiflags &= ~TH_FIN;

			/*
			 * Send an ACK to resynchronize and drop any data.
			 * But keep on processing for RST or ACK.
			 */

			flags_ |= TF_ACKNOW;
			todrop = datalen;
			dupseg = TRUE;
		}
		tcph->seqno() += todrop;
		th->size() -= todrop;	// XXX Must decrease size too!!
		datalen -= todrop;
	}

	/*
	 * If last ACK falls within this segment's sequence numbers,
	 * record the timestamp.
	 * See RFC1323 (now RFC1323 bis)
	 */
	if (ts_option_ && !fh->no_ts_ && tcph->seqno() <= last_ack_sent_) {
		/*
		 * this is the case where the ts value is newer than
		 * the last one we've seen, and the seq # is the one we expect
		 * [seqno == last_ack_sent_] or older
		 */
		recent_age_ = now();
		recent_ = tcph->ts();
	}

	if (tiflags & TH_SYN) {
		fprintf(stderr,
		    "%f: FullTcpAgent::recv(%s) received unexpected SYN (state:%d)\n",
		        now(), name(), state_);
		goto dropwithreset;
	}

	// K: added TH_SYN, but it is already known here!
	if ((tiflags & TH_ACK) == 0) {
		fprintf(stderr, "%f: FullTcpAgent::recv(%s) got packet lacking ACK (seq %d)\n",
			now(), name(), tcph->seqno());
		goto drop;
	}

	/*
	 * Ack processing.
	 */

	switch (state_) {
	case TCPS_SYN_RECEIVED:	/* want ACK for our SYN+ACK */
		if (ackno < highest_ack_ || ackno > maxseq_) {
			// not in useful range
			goto dropwithreset;
		}
                /*
                 * Make transitions:
                 *      SYN-RECEIVED  -> ESTABLISHED
                 *      SYN-RECEIVED* -> FIN-WAIT-1
                 */
                if (flags_ & TF_NEEDFIN) {
			newstate(TCPS_FIN_WAIT_1);
                        flags_ &= ~TF_NEEDFIN;
                } else {
                        newstate(TCPS_ESTABLISHED);
			if(app_)
			  ((NSSocket *)app_)->upcall_passconn();
                }
		cwnd_ = initial_window();
		/* fall into ... */

        /*
         * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
         * ACKs.  If the ack is in the range
         *      tp->snd_una < ti->ti_ack <= tp->snd_max
         * then advance tp->snd_una to ti->ti_ack and drop
         * data from the retransmission queue.
	 *
	 * note that state TIME_WAIT isn't used
	 * in the simulator
         */

        case TCPS_ESTABLISHED:
        case TCPS_FIN_WAIT_1:
        case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
        case TCPS_CLOSING:
        case TCPS_LAST_ACK:

		//
		// look for ECNs in ACKs, react as necessary
		//

		if (fh->ecnecho() && (!ecn_ || !ect_)) {
			fprintf(stderr,
			    "%f: FullTcp(%s): warning, recvd ecnecho but I am not ECN capable!\n",
				now(), name());
		}

                //
                // generate a stream of ecnecho bits until we see a true
                // cong_action bit
                // 
                if (ecn_) {
                        if (fh->ce() && fh->ect())
                                recent_ce_ = TRUE;
                        else if (fh->cong_action())
                                recent_ce_ = FALSE;
                }

		// look for dup ACKs (dup ack numbers, no data)
		//
		// do fast retransmit/recovery if at/past thresh
		if (ackno <= highest_ack_) {
			// an ACK which doesn't advance highest_ack_
			if (datalen == 0 && (!dupseg_fix_ || !dupseg)) {
				--pipe_; // ACK indicates pkt cached @ receiver

                                /*
                                 * If we have outstanding data
                                 * this is a completely
                                 * duplicate ack,
                                 * the ack is the biggest we've
                                 * seen and we've seen exactly our rexmt
                                 * threshhold of them, assume a packet
                                 * has been dropped and retransmit it.
                                 *
                                 * We know we're losing at the current
                                 * window size so do congestion avoidance.
                                 *
                                 * Dup acks mean that packets have left the
                                 * network (they're now cached at the receiver)
                                 * so bump cwnd by the amount in the receiver
                                 * to keep a constant cwnd packets in the
                                 * network.
                                 */

				if ((rtx_timer_.status() != TIMER_PENDING) ||
				    ackno != highest_ack_) {
					// not timed, or re-ordered ACK
					dupacks_ = 0;
				} else if (++dupacks_ == tcprexmtthresh_) {

					fastrecov_ = TRUE;

					/* re-sync the pipe_ estimate */
					pipe_ = maxseq_ - highest_ack_;
					pipe_ /= maxseg_;
					pipe_ -= (dupacks_ + 1);

					pipe_ = int(cwnd_) - dupacks_ - 1;

					dupack_action(); // maybe fast rexmt
					goto drop;

				} else if (dupacks_ > tcprexmtthresh_) {
					if (reno_fastrecov_) {
						// we just measure cwnd in
						// packets, so don't scale by
						// maxseg_ as real
						// tcp does
						cwnd_++;
					}
					send_much(0, REASON_NORMAL, maxburst_);
					goto drop;
				}
			} else {
				// non zero-length [dataful] segment
				// with a dup ack (normal for dataful segs)
				// (or window changed in real TCP).
				if (dupack_reset_) {
					dupacks_ = 0;
					fastrecov_ = FALSE;
				}
			}
			break;	/* take us to "step6" */
		} /* end of dup acks */

		/*
		 * we've finished the fast retransmit/recovery period
		 * (i.e. received an ACK which advances highest_ack_)
		 * The ACK may be "good" or "partial"
		 */

process_ACK:

		if (ackno > maxseq_) {
			// ack more than we sent(!?)
			fprintf(stderr,
			    "%f: FullTcpAgent::recv(%s) too-big ACK (ack: %d, maxseq:%d)\n",
				now(), name(), int(ackno), int(maxseq_));
			goto dropafterack;
		}

                /*
                 * If we have a timestamp reply, update smoothed
                 * round trip time.  If no timestamp is present but
                 * transmit timer is running and timed sequence
                 * number was acked, update smoothed round trip time.
                 * Since we now have an rtt measurement, cancel the
                 * timer backoff (cf., Phil Karn's retransmit alg.).
                 * Recompute the initial retransmit timer.
		 *
                 * If all outstanding data is acked, stop retransmit
                 * If there is more data to be acked, restart retransmit
                 * timer, using current (possibly backed-off) value.
                 */
		newack(pkt);	// handle timers, update highest_ack_
		--pipe_;

		/*
		 * if this is a partial ACK, invoke whatever we should
		 */

		int partial = pack(pkt);

		if (partial)
			pack_action(pkt);
		else
			ack_action(pkt);

		/*
		 * if this is an ACK with an ECN indication, handle this
		 */

		if (fh->ecnecho())
			ecn(highest_ack_);  // updated by newack(), above

		// CHECKME: handling of rtx timer
		if (ackno == maxseq_) {
			needoutput = TRUE;
		}

		/*
		 * If no data (only SYN) was ACK'd,
		 *    skip rest of ACK processing.
		 */
		if (ackno == (highest_ack_ + 1))
			goto step6;

		// if we are delaying initial cwnd growth (probably due to
		// large initial windows), then only open cwnd if data has
		// been received
                /*
                 * When new data is acked, open the congestion window.
                 * If the window gives us less than ssthresh packets
                 * in flight, open exponentially (maxseg per packet).
                 * Otherwise open about linearly: maxseg per window
                 * (maxseg^2 / cwnd per packet).
                 */
		if ((!delay_growth_ || (rcv_nxt_ > 0)) &&
		    last_state_ == TCPS_ESTABLISHED) {
			if (!partial || open_cwnd_on_pack_)
				opencwnd();
		}

		// K: added state check in equal but diff way
		if ((state_ >= TCPS_FIN_WAIT_1) && (ackno == maxseq_)) {
			ourfinisacked = TRUE;
		} else {
			ourfinisacked = FALSE;
		}
		// additional processing when we're in special states

		switch (state_) {
                /*
                 * In FIN_WAIT_1 STATE in addition to the processing
                 * for the ESTABLISHED state if our FIN is now acknowledged
                 * then enter FIN_WAIT_2.
                 */
		case TCPS_FIN_WAIT_1:	/* doing active close */
			if (ourfinisacked) {
				// got the ACK, now await incoming FIN
				newstate(TCPS_FIN_WAIT_2);
				cancel_timers();
				needoutput = FALSE;
			}
			break;

                /*
                 * In CLOSING STATE in addition to the processing for
                 * the ESTABLISHED state if the ACK acknowledges our FIN
                 * then enter the TIME-WAIT state, otherwise ignore
                 * the segment.
                 */
		case TCPS_CLOSING:	/* simultaneous active close */;
			if (ourfinisacked) {
				newstate(TCPS_CLOSED);
				cancel_timers();
			}
			break;
                /*
                 * In LAST_ACK, we may still be waiting for data to drain
                 * and/or to be acked, as well as for the ack of our FIN.
                 * If our FIN is now acknowledged,
                 * enter the closed state and return.
                 */
		case TCPS_LAST_ACK:	/* passive close */
			// K: added state change here
			if (ourfinisacked) {
				newstate(TCPS_CLOSED);
#ifdef notdef
cancel_timers();	// DOES THIS BELONG HERE?, probably (see tcp_cose
#endif
				finish();
				goto drop;
			} else {
				// should be a FIN we've seen
				hdr_ip* iph = hdr_ip::access(pkt);
                                fprintf(stderr,
                                "%f: %d.%d>%d.%d FullTcpAgent::recv(%s) received non-ACK (state:%d)\n",
                                        now(),
                                        iph->saddr(), iph->sport(),
                                        iph->daddr(), iph->dport(),
                                        name(), state_);
                        }
			break;

		/* no case for TIME_WAIT in simulator */
		}  // inner switch
	} // outer switch

step6:
	/* real TCP handles window updates and URG data here */
/* dodata: this label is in the "real" code.. here only for reference */
	/*
	 * DATA processing
	 */

	if ((datalen > 0 || (tiflags & TH_FIN)) &&
	    TCPS_HAVERCVDFIN(state_) == 0) {
		if (tcph->seqno() == rcv_nxt_ && rq_.empty()) {
			// see the "TCP_REASS" macro for this code:
			// got the in-order packet we were looking
			// for, nobody is in the reassembly queue,
			// so this is the common case...
			// note: in "real" TCP we must also be in
			// ESTABLISHED state to come here, because
			// data arriving before ESTABLISHED is
			// queued in the reassembly queue.  Since we
			// don't really have a process anyhow, just
			// accept the data here as-is (i.e. don't
			// require being in ESTABLISHED state)
			flags_ |= TF_DELACK;


			PktDataEntry *pe = new PktDataEntry(rcv_nxt_, (PacketData *)pkt->userdata());
			rcv_buf_.append(pe);
			rcv_nxt_ += datalen;
			rcv_wnd_ -= datalen;

			if(pkt->userdata() && datalen) {
			if(datalen==((PacketData *)pkt->userdata())->size())
			  recvBytes(datalen); // notify app. of "delivery"
			}
			tiflags = tcph->flags() & TH_FIN;
			needoutput = TRUE;
		} else {
			// see the "tcp_reass" function:
			// not the one we want next (or it
			// is but there's stuff on the reass queue);
			// do whatever we need to do for out-of-order
			// segments or hole-fills.  Also,
			// send an ACK to the other side right now.
			// K: some changes here, figure out
			int rcv_nxt_old_ = rcv_nxt_; // notify app. if changes
			
			tiflags = rq_.add(pkt);

			PktDataEntry *pe = new PktDataEntry(tcph->seqno(), (PacketData *)pkt->userdata());

			insert(&rcv_buf_, pe);

			if (rcv_nxt_ > rcv_nxt_old_)
				recvBytes(rcv_nxt_ - rcv_nxt_old_);
			if(datalen && app_)
			  ((NSSocket *)app_)->upcall_recv((PacketData*)pkt->userdata());

			if (tiflags & TH_PUSH) {
				// K: APPLICATION recv
				needoutput = need_send();
			} else {
				flags_ |= TF_ACKNOW;
			}
		}
	} else {
		/*
		 * we're closing down or this is a pure ACK that
		 * wasn't handled by the header prediction part above
		 * (e.g. because cwnd < wnd)
		 */
		// K: this is deleted
		tiflags &= ~TH_FIN;
	}

	/*
	 * if FIN is received, ACK the FIN
	 * (let user know if we could do so)
	 */

	if (tiflags & TH_FIN) {
		if (TCPS_HAVERCVDFIN(state_) == 0) {
			flags_ |= TF_ACKNOW;
			rcv_nxt_++;
		}
		if(app_)
		  app_->upcall_closing();
		switch (state_) {
                /*
                 * In SYN_RECEIVED and ESTABLISHED STATES
                 * enter the CLOSE_WAIT state.
		 * (passive close)
                 */
                case TCPS_SYN_RECEIVED:
                case TCPS_ESTABLISHED:
			newstate(TCPS_CLOSE_WAIT);
                        break;

                /*
                 * If still in FIN_WAIT_1 STATE FIN has not been acked so
                 * enter the CLOSING state.
		 * (simultaneous close)
                 */
                case TCPS_FIN_WAIT_1:
			newstate(TCPS_CLOSING);
                        break;
                /*
                 * In FIN_WAIT_2 state enter the TIME_WAIT state,
                 * starting the time-wait timer, turning off the other
                 * standard timers.
		 * (in the simulator, just go to CLOSED)
		 * (completion of active close)
                 */
                case TCPS_FIN_WAIT_2:
                        newstate(TCPS_CLOSED);
			cancel_timers();
                        break;
		}
	} /* end of if FIN bit on */

	if (needoutput || (flags_ & TF_ACKNOW))
		send_much(1, REASON_NORMAL, maxburst_);
	else if (curseq_ >= highest_ack_ || infinite_send_)
		send_much(0, REASON_NORMAL, maxburst_);
	// K: which state to return to when nothing left?

	if (!halfclose_ && state_ == TCPS_CLOSE_WAIT && highest_ack_ == maxseq_)
		usrclosed();

	Packet::free(pkt);

	// haoboy: Is here the place for done{} of active close? 
	// It cannot be put in the switch above because we might need to do
	// send_much() (an ACK)
	if (state_ == TCPS_CLOSED) 
		Tcl::instance().evalf("%s done", this->name());

	return;

dropafterack:
	flags_ |= TF_ACKNOW;
	send_much(1, REASON_NORMAL, maxburst_);
	goto drop;

dropwithreset:
	/* we should be sending an RST here, but can't in simulator */
	if (tiflags & TH_ACK) {
		sendpacket(ackno, 0, 0x0, 0, NULL, REASON_NORMAL);
	} else {
		int ack = tcph->seqno() + datalen;
		if (tiflags & TH_SYN)
			ack--;
		sendpacket(0, ack, TH_ACK, 0, NULL, REASON_NORMAL);
	}
drop:
	Packet::free(pkt);

	return;
}

void
SocketTcp::sendpacket(int seqno, int ackno, int pflags, int datalen, PacketData *data, int reason)
{
        Packet* p = allocpkt(datalen);
        hdr_tcp *tcph = hdr_tcp::access(p);

	/* build basic header w/options */

        tcph->seqno() = seqno;
        tcph->ackno() = ackno;
        tcph->flags() = pflags;
        tcph->reason() |= reason; // make tcph->reason look like ns1 pkt->flags?
	tcph->sa_length() = 0;    // may be increased by build_options()
        tcph->hlen() = tcpip_base_hdr_size_;
	tcph->hlen() += build_options(tcph);
	tcph->advwin() = rcv_wnd_;

	/* ECT, ECN, and congestion action */

        hdr_flags *fh = hdr_flags::access(p);
	fh->ect() = ect_;	// on after mutual agreement on ECT
	if (ecn_ && !ect_)	// initializing; ect = 0, ecnecho = 1
		fh->ecnecho() = 1;
	else {
		fh->ecnecho() = recent_ce_;
	}
	fh->cong_action() =  cong_action_;

	/* actual size is data length plus header length */

        hdr_cmn *ch = hdr_cmn::access(p);
        ch->size() = datalen + tcph->hlen();

        if (datalen <= 0)
                ++nackpack_;
        else {
                ++ndatapack_;
                ndatabytes_ += datalen;
		last_send_time_ = now();	// time of last data
		if(data) {
		  p->setdata(data->copy());
		}else {
		  fprintf(stderr, "datalen>0 without data\n");
		}
		
        }
        if (reason == REASON_TIMEOUT || reason == REASON_DUPACK) {
                ++nrexmitpack_;
                nrexmitbytes_ += datalen;
        }

	last_ack_sent_ = ackno;
	send(p, 0);
}

int SocketTcp::r_advance_bytes(int nb) {
	int new_inc = nb, old_curseq = curseq_;
        //
        // state-specific operations:
        //      if CLOSED, reset and try a new active open/connect
        //      if ESTABLISHED, just try to send more
        //      if above ESTABLISHED, we are closing, so don't allow
        //      if anything else (establishing), do nothing here
        //

        if (state_ > TCPS_ESTABLISHED) {
            fprintf(stderr,
            "%f: SocketTcpAgent::advance(%s): cannot advance while in state %d\n",
                 now(), name(), state_);
            return 0;
        } else if (state_ == TCPS_CLOSED) {
                reset();
                curseq_ = iss_ + nb;
                connect();              // initiate new connection
        } else if (state_ == TCPS_ESTABLISHED) {
                closed_ = 0;
                if (curseq_ < iss_)
                        curseq_ = iss_;

		curseq_ = curseq_ + nb > (highest_ack_ + snd_wnd_)? highest_ack_+ snd_wnd_: curseq_+nb;
		new_inc = curseq_ - old_curseq; 
                send_much (0, REASON_NORMAL, maxburst_);
        }

        return new_inc;	
}

int SocketTcp::r_advance_pkt(PacketData *p) {
	int new_inc = 0, old_curseq = curseq_, nb;

	if(p==NULL)
	  return 0;

	nb = p->size();
        if (state_ > TCPS_ESTABLISHED) {
            fprintf(stderr,
            "%f: SocketTcpAgent::advance(%s %d): cannot advance while in state %d\n",
                 now(), name(),  here_.addr_, state_);

	    // socket is closed
            return -1;
        } else if (state_ == TCPS_CLOSED) {
                reset();
		PktDataEntry *pe = new PktDataEntry((int)curseq_+1, p);
		curseq_ += nb;
		snd_buf_.append(pe);
                connect();              // initiate new connection
	       	return 0;
	} else if (state_ == TCPS_ESTABLISHED) { 
                closed_ = 0;
                if (curseq_ < iss_)
                        curseq_ = iss_;

		curseq_ = curseq_ + nb > (highest_ack_ + snd_wnd_)? (int)curseq_: curseq_ + nb;
		new_inc = curseq_ - old_curseq; 
		if(new_inc) {
		  PktDataEntry *pe = new PktDataEntry(old_curseq + 1, p);
		  apcnt++;
		  snd_buf_.append(pe);
		}
                send_much(0, REASON_NORMAL, maxburst_);
        }

	if (new_inc && curseq_ + nb <= highest_ack_ + snd_wnd_)
	  return 1;
	if (new_inc && curseq_ + nb > highest_ack_ + snd_wnd_)
	  return 0;

	if(p!=NULL) {
	  dropcnt++;
	}
	return -1;
}

void SocketTcp::listen(int max) {
  FullTcpAgent::listen();
  max_conn_ = max;
  listen_only_ = TRUE;
}

void SocketTcp::ack_syn(Packet *synpkt) {
  hdr_tcp *tcph = hdr_tcp::access(synpkt);

  irs_ = tcph->seqno();
  t_seqno_ = iss_; /* tcp_sendseqinit() macro in real tcp */
  rcv_nxt_ = rcvseqinit(irs_, 0);
  flags_ |= TF_ACKNOW;

  newstate(TCPS_SYN_RECEIVED);
  send_much(1, REASON_NORMAL, maxburst_);
  return;
}