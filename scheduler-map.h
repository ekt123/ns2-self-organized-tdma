/* -*-	Mode:C++; c-basic-offset:2; tab-width:2; indent-tabs-mode:t -*- */
/*
 * Copyright (c) 1994 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

// Event Scheduler using the Standard Template Library map and deque
// Contributed by George F. Riley, Georgia Tech.  Spring 2002
// Modified by Alfred Park for compatibility with ns-2.26

#ifndef __SCHEDULER_MAP_H__
#define __SCHEDULER_MAP_H__

#include <map>
#include <deque>

#include "scheduler.h"

typedef pair<double,scheduler_uid_t> KeyPair_t; // The key for the multimap
typedef map<KeyPair_t, Event*, less<KeyPair_t> > EventMap_t;
typedef deque<Event*> UIDDeq_t;                 // For looking up ev by uid

class MapScheduler : public Scheduler {
public:
  MapScheduler();
  virtual ~MapScheduler();
public:
  int command(int argc, const char*const* argv);
  virtual void cancel(Event*);	                // cancel event
  virtual void insert(Event*);	                // schedule event
  virtual Event* lookup(scheduler_uid_t uid);	// look for event
  virtual Event* deque();		        // next event (removes from q)
  virtual Event* head() { return earliest(); }
  virtual Event* earliest();                    // earliest, don't remove
protected:
  EventMap_t      EventList;                    // The actual event list
#ifdef USING_UIDDEQ
	UIDDeq_t        UIDDeq;                       // DEQ Lookup
#endif
  scheduler_uid_t fuid;
  scheduler_uid_t luid;   // First and last+1 in UIDDeq
  unsigned long   totev;  // Total events (debug)
  unsigned long   totrm;  // Total events removed (debug)
	bool            verbose;// True if verbose debug
	unsigned long   verbosemod;  // Mod factor for verbosity
  EventMap_t::iterator hint;   // Hint for insertions (right after prior)
private:
  void CleanUID();        // Clean up the UID Deq
  void DBDump(const char* pMsg = NULL);          // Debug Dump
};

#endif
