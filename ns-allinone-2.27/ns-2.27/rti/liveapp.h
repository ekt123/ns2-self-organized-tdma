/*
 *  Support for routing live application traffic via ns
 *    - Kalyan Perumalla <http:/www.cc.gatech.edu/~kalyan> 15 June 2001
 */

#ifndef ns_liveapp_h
#define ns_liveapp_h

#include "app.h"

class LiveApplication : public Application {
public:
	LiveApplication( long addr, long port );
	virtual void send(int nbytes);
	virtual void recv(int nbytes);
	virtual void resume( void );
	virtual void initiate_connect( void );
	virtual void disconnect( void );
	virtual void bind_local( long port );
	virtual void unbind_local( void );
	virtual void bind_peer( long addr, long port );

protected:
	virtual int command(int argc, const char*const* argv);
	virtual void start();
	virtual void stop();

protected:
	struct { long addr, port; } local, peer;
	struct { long bytes; } sent, recd, tosend;
	bool awaiting_resume;
public:
	bool nomore_data;
};

#endif
