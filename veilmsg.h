/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
#ifndef __VEILMSG_H
#define __VEILMSG_H

/*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <brti.h>

/*---------------------------------------------------------------------------*/
#define SIN_FAMILY(/*struct sockaddr_in **/_sin) (!(_sin)?0:(_sin)->sin_family)
#define SIN_IP(/*struct sockaddr_in **/_sin) (!(_sin)?0:(_sin)->sin_addr.s_addr)
#define SIN_PORT(/*struct sockaddr_in **/_sin) (!(_sin)?0:(_sin)->sin_port)

/*---------------------------------------------------------------------------*/
typedef unsigned long SockPort;
typedef unsigned long SockAddr;

/*---------------------------------------------------------------------------*/
#define MICROSEC 1e-6
#define MILLISEC 1e-3
#define SEC 1.0

/*---------------------------------------------------------------------------*/
typedef enum
{
    VEIL_MSG_CONNECT_REQ,
    VEIL_MSG_CONNECT_RESP,
    VEIL_MSG_DATA_NOTIFY,
    VEIL_MSG_SOCKET_DATA,
    VEIL_MSG_DISCONNECT
} VeilMsgType;

/*---------------------------------------------------------------------------*/
#define VEIL_MSG_TAG 0

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
typedef struct
{
} VeilMsgConnectReq;

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
typedef struct
{
    int success_code;
} VeilMsgConnectResp;

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
typedef struct
{
    long tot_bytes; /*Total #bytes sent by sender between connect & disconnect;
                     -1 if unknown*/
} VeilMsgDisconnect;
#define set_veil_msg_disconnect( m, tb ) do{                                 \
    (m)->tot_bytes = tb;                                                     \
    }while(0)

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
typedef struct
{
    int nbytes;
} VeilMsgDataNotify;
#define set_veil_msg_data_notify( m, nb ) do{                                 \
    (m)->nbytes = nb;                                                         \
    }while(0)

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
typedef struct
{
    #define VEILDATACHUNKSIZE 1024
    char data[VEILDATACHUNKSIZE];
    int datalen; /*0<datalen<=VEILDATACHUNKSIZE*/
} VeilMsgSocketData;
#define set_veil_msg_socket_data( m, buf, dlen ) do{                          \
    ASSERT( 0 < (dlen) && (dlen) <= VEILDATACHUNKSIZE, ("") );                \
    memcpy( (m)->data, buf, dlen );                                           \
    (m)->datalen = dlen;                                                      \
    }while(0)

/*---------------------------------------------------------------------------*/
/*                                                                           */
/*---------------------------------------------------------------------------*/
typedef struct veil_msg_struct
{
    struct MsgS rti_data;
    int dummy; /*For pdns compatibility*/
    SockAddr src_ip, dest_ip;
    SockPort src_port, dest_port;
    VeilMsgType type;
    union
    {
        VeilMsgConnectReq creq;
	VeilMsgConnectResp cresp;
	VeilMsgDisconnect dcon;
	VeilMsgDataNotify dntfy;
	VeilMsgSocketData sdata;
    } msg;
    struct veil_msg_struct *next; /*To link msgs in a list*/
} VeilMsg;
#define set_veil_msg( m, ty, sip, spt, dip, dpt ) do{                         \
    (m)->dummy = 4321;                                                        \
    (m)->type = ty;                                                           \
    (m)->src_ip = sip; (m)->dest_ip = dip;                                    \
    (m)->src_port = spt; (m)->dest_port = dpt;                                \
    }while(0)

/*---------------------------------------------------------------------------*/
void print_veilmsg_hdr( FILE *out, VeilMsg *vmsg );

/*---------------------------------------------------------------------------*/
ulong dot_to_ulong( const char *s );
const char *ulong_to_dot( ulong n );

/*---------------------------------------------------------------------------*/
/* SockAddr --> [0..max_addr-1] hashing service.                             */
/*                                                                           */
/*---------------------------------------------------------------------------*/
struct HashIPAddrStruct;
typedef struct HashIPAddrStruct *HashIPAddr;
HashIPAddr hash_ipaddr_init( int max_addr );
int hash_ipaddr_find_index( HashIPAddr h, SockAddr addr );
void hash_ipaddr_printstats( HashIPAddr h, FILE *out );
void hash_ipaddr_wrapup( HashIPAddr h );

/*---------------------------------------------------------------------------*/
#endif /*__VEILMSG_H*/
