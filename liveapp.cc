/*---------------------------------------------------------------------------*/
/*
 *  Support for routing live application traffic via ns
 *    - Kalyan Perumalla <http:/www.cc.gatech.edu/~kalyan> 15 June 2001
 */
/*---------------------------------------------------------------------------*/

#include <iostream.h>
#include <map.h>
#include <vector.h>

#include "liveapp.h"

extern "C" {
#include "veilmsg.h"
#include "buffers.h"
#include "veilmsg.c"
}

/*---------------------------------------------------------------------------*/
#include <assert.h>
#define ASSERT( _cond, _act ) do{ \
        if( !(_cond) ) { \
	    printf _act; \
	    printf( "\n" ); \
	    assert( _cond ); \
	} \
    }while(0)

/*---------------------------------------------------------------------------*/
static double LOOKAHEAD = 1.0*MICROSEC;
struct
{
    double lconnect;
    double ldisconnect;
    double lsocket_data;
    double ldata_notify;
} latency =
{
    LOOKAHEAD,
    LOOKAHEAD,
    LOOKAHEAD,
    LOOKAHEAD
};

/*---------------------------------------------------------------------------*/
struct LiveAppData
{
    LiveApplication *app;
};
typedef map<SockPort,LiveAppData> AppPortMap;

/*---------------------------------------------------------------------------*/
class AppList : public vector<LiveApplication*>
{
    public: LiveApplication *top_app( void )
        { return size() > 0 ? (*this)[0] : 0; }
    public: void add_app( LiveApplication *app )
        { push_back( app ); }
    public: void del_app( LiveApplication *app )
    {
	int i = 0, j = -1, n = size();
        for( i = 0; i < n; i++ )
	{
	    if( (*this)[i] == app ) { j = i; }
	    if( j >= 0 && i < n-1 ) (*this)[i] = (*this)[i+1];
	}
	ASSERT( j >= 0, ("App must exist") );
	pop_back();
    }
};

/*---------------------------------------------------------------------------*/
struct LiveHostData
{
    RTI_ObjClassDesignator a2n_class;
    RTI_ObjClassDesignator n2a_class;
    RTI_ObjInstanceDesignator n2a_instance;
    AppList unbound_apps;
    AppPortMap port_map;
};
typedef map<SockAddr,LiveHostData> HostAddrMap;
static HostAddrMap addr_map;

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
static void forward_veil_msg( VeilMsg *vmsg, TM_Time delay )
{
    LiveHostData &hdata = addr_map[vmsg->dest_ip];

    RTI_ObjInstanceDesignator objinst = hdata.n2a_instance;
    int len = sizeof( VeilMsg );
    struct MsgS *msg = &vmsg->rti_data;
    msg->TimeStamp += delay;
    RTI_UpdateAttributeValues( objinst, msg, len, VEIL_MSG_TAG );

    double now= Scheduler::instance().clock();

if(1){printf( "Now=%lf, forwarded veil msg\n", now ); print_veilmsg_hdr( stdout, vmsg ); fflush(stdout);}
}

/*---------------------------------------------------------------------------*/
void veil_recv( char *cp )
{
    VeilMsg *vmsg = (VeilMsg *)cp;

if(1){printf("veil_recv() recd veil msg\n"); print_veilmsg_hdr(stdout, vmsg);}

    LiveHostData &src_data = addr_map[vmsg->src_ip];
    LiveAppData &src_app_data = src_data.port_map[vmsg->src_port];
    LiveApplication *src_app = src_app_data.app;

    LiveHostData &dest_data = addr_map[vmsg->dest_ip];
    LiveAppData &dest_app_data = dest_data.port_map[vmsg->dest_port];
    LiveApplication *dest_app = dest_app_data.app;

    switch( vmsg->type )
    {
        case VEIL_MSG_CONNECT_REQ:
        case VEIL_MSG_CONNECT_RESP:
	{
	    /*Bind a LiveApplication object to this connection,if none already*/
	    if( !src_app )
	    {
	        LiveApplication *app = src_data.unbound_apps.top_app();
		ASSERT( app, ("At least one unbound app must exist") );
		app->bind_local( vmsg->src_port );
                src_app = src_app_data.app;
		ASSERT( src_app == app, ("App should be bound by now") );
		src_app->bind_peer( vmsg->dest_ip, vmsg->dest_port );
	        src_app->nomore_data = false;

		/*If our agent is FullTCP, force it to connect*/
		if( vmsg->type == VEIL_MSG_CONNECT_REQ )
		    src_app->initiate_connect();
	    }

	    /*Forward this request directly to dest, since ns has no SYN*/
	    forward_veil_msg( vmsg, latency.lconnect );
	    break;
	}
        case VEIL_MSG_DATA_NOTIFY:
	{
	    ASSERT( 0, ("Can't reach here") );
	    break;
	}
        case VEIL_MSG_SOCKET_DATA:
	{
            /*Forward this data message to destination application*/
            forward_veil_msg( vmsg, latency.lsocket_data );

	    ASSERT( src_app, ("Application must be bound") );
            src_app->send( vmsg->msg.sdata.datalen );

	    break;
	}
        case VEIL_MSG_DISCONNECT:
	{
	    ASSERT( src_app, ("") );
	    src_app->disconnect();
	    break;
	}
        default:
	{
	    ASSERT( 0, ("Bad veil message type %d\n", vmsg->type) );
	    break;
	}
    }
}

/*---------------------------------------------------------------------------*/
typedef char AddressString[100];
static class LiveApplicationClass : public TclClass
{
 public:
	LiveApplicationClass() :
	    TclClass("Application/LiveApplication"), inited(false) {}
	TclObject* create( int argc, const char*const*argv )
	{
	        cout << "LiveApplication::new()" << endl;
if(1){cout << "argc=" << argc << endl; for(int i=0;i<argc;i++)cout<<" \""<<argv[i]<<"\""; cout << endl;}
		if( !inited ) { init_groups(); inited = true; }
		SockAddr ipaddr = 0;
		SockPort port = 0;
		ASSERT(argc > 4, ("LiveApplication creation needs IP address"));
		if( argc > 4 ) { ipaddr = dot_to_ulong( argv[4] ); }
		if( argc > 5 ) { port = atoi( argv[5] ); }
		return (new LiveApplication( ipaddr, port ));
	}
 private:
	void init_groups( void );
	void read_addresses( AddressString addrs[], int maxn, int *rn );
	bool inited;
	static char *MyWhereProc( long, void *, long );
	static VeilMsg *alloc_veilmsg( void );
} class_application;

/*---------------------------------------------------------------------------*/
extern MB_BufferPool RTIFreePool;
VeilMsg *LiveApplicationClass::alloc_veilmsg( void )
{
  VeilMsg *buf = (VeilMsg *)MB_GetBuffer(RTIFreePool);
 
  return buf;
}

/*---------------------------------------------------------------------------*/
char *LiveApplicationClass::MyWhereProc(long MsgSize, void *ctxt, long MsgType)
{
  char * buf;
 
  ASSERT( MsgSize == sizeof(VeilMsg), ("WhereProc: bad msg size") );
  ASSERT( MsgType == VEIL_MSG_TAG, ("WhereProc: bad msg type") );
 
  buf = (char*)alloc_veilmsg();
 
  return buf;
}

/*---------------------------------------------------------------------------*/
void LiveApplicationClass::read_addresses( AddressString addrs[],
    int maxn, int *rn )
{
    int i = 0, n = maxn;

    *rn = 0;
    for( i = 0; i < n; i++ )
    {
        sprintf( addrs[i], "192.168.0.%d", i+1 );
	(*rn)++;
    }
}

/*---------------------------------------------------------------------------*/
void LiveApplicationClass::init_groups( void )
{
  #define MAXADDRESSES 2
  AddressString addrs[MAXADDRESSES];
  int i = 0, j = 0, n = 0;

  read_addresses( addrs, MAXADDRESSES, &n );

  for( j = 0; j < n; j++ )
  {
      char cn[100];
      struct in_addr inp;
      int retval = inet_aton( addrs[j], &inp );
      SockAddr ip_addr = inp.s_addr;

      ASSERT( retval, ("inet_aton(\"%s\")=%d", addrs[j], retval) );

      LiveHostData &hdata = addr_map[ip_addr];

      sprintf( cn, "A2N:%s", addrs[j] );
      hdata.a2n_class = RTI_CreateClass( cn );
      ASSERT( hdata.a2n_class, ("Can't create %s class", cn) );

      sprintf( cn, "N2A:%s", addrs[j] );
      hdata.n2a_class = RTI_CreateClass( cn );
      ASSERT( hdata.n2a_class, ("Can't create %s class\n", cn) );
  }

if(0) RTIKIT_Barrier(); /*Indicate to everyone that all classes are created*/

  for( j = 0; j < n; j++ )
  {
      char cn[100];
      struct in_addr inp;
      int retval = inet_aton( addrs[j], &inp );
      SockAddr ip_addr = inp.s_addr;

      LiveHostData &hdata = addr_map[ip_addr];

      RTI_PublishObjClass( hdata.n2a_class );
      hdata.n2a_instance = RTI_RegisterObjInstance( hdata.n2a_class );

      if( !RTI_IsClassSubscriptionInitialized( hdata.a2n_class ) )
        RTI_InitObjClassSubscription( hdata.a2n_class, MyWhereProc, 0 );
      RTI_SubscribeObjClassAttributes( hdata.a2n_class );
  }

if(0) RTIKIT_Barrier(); /*wait until all are subscribed before sending messages*/
}

/*---------------------------------------------------------------------------*/
LiveApplication::LiveApplication( long a, long p )
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << ": " << "LiveApplication::constructor("
	     << ulong_to_dot(a) << ":" << p << " )" << endl;

	enableRecv_ = 0;
	enableResume_ = 0;

        local.addr = a; local.port = p;
	peer.addr = 0; peer.port = 0;
	tosend.bytes = sent.bytes = recd.bytes = 0;
	awaiting_resume = true;

	SockAddr ipaddr = local.addr;
	LiveHostData &hdata = addr_map[ipaddr];
	hdata.unbound_apps.add_app( this );
}

/*---------------------------------------------------------------------------*/
int LiveApplication::command(int argc, const char*const* argv)
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": " << "LiveApplication::command(argc="
	     << argc << ")" << endl;
	for(int i=0;i<argc;i++){cout<<" \""<<argv[i]<<"\"";}cout<<endl;
	return (Application::command(argc, argv));
}

/*---------------------------------------------------------------------------*/
void LiveApplication::bind_local( long port )
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": " << "LiveApplication::bind_local(port="
	     << port << ")" << endl;

	ASSERT( local.port <= 0 || local.port == port, ("Bad port number") );

	if( local.port <= 0 )
	{
	    local.port = port;

	    SockAddr ipaddr = local.addr;
	    SockPort portnum = local.port;
            LiveHostData &hdata = addr_map[ipaddr];

	    hdata.unbound_apps.del_app( this );

            LiveAppData &adata = hdata.port_map[portnum];
            adata.app = this;
	}
}

/*---------------------------------------------------------------------------*/
void LiveApplication::unbind_local( void )
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": " << "LiveApplication::unbind_local()" << endl;

	if( local.port > 0 )
	{
	    SockAddr ipaddr = local.addr;
	    SockPort portnum = local.port;
	    LiveHostData &hdata = addr_map[ipaddr];

	    LiveAppData &adata = hdata.port_map[portnum];
	    adata.app = 0;

	    hdata.unbound_apps.add_app( this );
	    local.port = 0;
	}
}

/*---------------------------------------------------------------------------*/
void LiveApplication::bind_peer( long addr, long port )
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": " << "LiveApplication::bind_peer(addr=" << addr
	     << ", port=" << port << ")" << endl;
	
	peer.addr = addr;
	peer.port = port;
}

/*---------------------------------------------------------------------------*/
void LiveApplication::initiate_connect( void )
{
    if(1)Application::send( 0 );
    awaiting_resume = true;
cout << name_ << ": initiate_connect()" << endl;
}

/*---------------------------------------------------------------------------*/
void LiveApplication::start()
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": " << "LiveApplication::start() "
	     << ulong_to_dot(local.addr) << ":" << local.port << endl;

	tosend.bytes = sent.bytes = recd.bytes = 0;
	awaiting_resume = true;
	nomore_data = false;

	if( local.port > 0 ) { bind_local( local.port ); }
}

/*---------------------------------------------------------------------------*/
void LiveApplication::stop()
{
	double now = Scheduler::instance().clock();
	cout << "@ " << now << " ";
	cout << name_ << ": " << "LiveApplication::stop()" << endl;

	nomore_data = true;
	if( awaiting_resume )
	{
cout << name_ << ": " << "Skipping stop waiting for pending resume." << endl;
	}
	else
	{
	    if( local.port > 0 )
	    {
	        VeilMsg vmsg;
	        SockAddr src_ip = local.addr;
	        SockPort src_port = local.port;
	        SockAddr dest_ip = peer.addr;
	        SockPort dest_port = peer.port;
	        set_veil_msg( &vmsg, VEIL_MSG_DISCONNECT,
	                      src_ip, src_port, dest_ip, dest_port );
	        set_veil_msg_disconnect( &vmsg.msg.dcon, sent.bytes );
	        vmsg.rti_data.TimeStamp = now;
	        forward_veil_msg( &vmsg, latency.ldisconnect );
cout << name_ << ": " << "Forwarded DISCONNECT: totbytes=" << sent.bytes <<endl;
	        unbind_local();
	    }

	    sent.bytes = recd.bytes = 0;
	    nomore_data = false;
	}
cout << name_ << ": " << "done stop" << endl;
}

/*---------------------------------------------------------------------------*/
void LiveApplication::send(int nbytes)
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": " << "LiveApplication::send(" << nbytes <<")"<<endl;

	if( awaiting_resume )
	{
	    tosend.bytes += nbytes;
cout << name_ << ": " << "Enqueueing " << nbytes << endl;
	}
	else
	{
	    ASSERT( tosend.bytes == 0, ("") );
	    Application::send(nbytes);
cout << name_ << ": " << "Sending " << nbytes << endl;
	}

	sent.bytes += nbytes;
}

/*---------------------------------------------------------------------------*/
void LiveApplication::recv(int nbytes)
{
	double now = Scheduler::instance().clock();
	cout << "@ " << now << " ";
	cout << name_ << ": " << "LiveApplication::recv(" << nbytes <<")"<<endl;

	if( nbytes <= 0 ) return;

	recd.bytes += nbytes;

	/*Notify actual application that additional nbytes can be delivered*/
	{
	    VeilMsg vmsg;
	    SockAddr src_ip = peer.addr;
	    SockPort src_port = peer.port;
	    SockAddr dest_ip = local.addr;
	    SockPort dest_port = local.port;
	    set_veil_msg( &vmsg, VEIL_MSG_DATA_NOTIFY,
	                  src_ip, src_port, dest_ip, dest_port );
	    set_veil_msg_data_notify( &vmsg.msg.dntfy, nbytes );
	    vmsg.rti_data.TimeStamp = now;
	    forward_veil_msg( &vmsg, latency.ldata_notify );
	}
}

/*---------------------------------------------------------------------------*/
void LiveApplication::resume( void )
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": LiveApplication::resume()" << endl;
	ASSERT( awaiting_resume, ("") );
	awaiting_resume = false;
	if( tosend.bytes > 0 )
	{
	    int n = tosend.bytes;
	    tosend.bytes = 0;
            awaiting_resume = true;
	    Application::send( n );
cout << name_ << ": resume() sent " << n << endl;
	}

	if( nomore_data )
	{
cout << name_ << ": resume-stopping" << endl;
	    stop();
cout << name_ << ": resume-stopped" << endl;
	}
cout << name_ << ": Done resume" << endl;
}

/*---------------------------------------------------------------------------*/
void LiveApplication::disconnect( void )
{
	cout << "@ " << Scheduler::instance().clock() << " ";
	cout << name_ << ": " << "LiveApplication::disconnect()" << endl;
	nomore_data = true;
	stop();
}

/*---------------------------------------------------------------------------*/
