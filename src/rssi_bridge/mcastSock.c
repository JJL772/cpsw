/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "prot.h"
#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <errno.h>

#include <protRelayUtil.h>

int
mkMcSd(int srv, struct sockaddr_in *sap)
{
int                sd;
struct sockaddr_in sin;
int                val;
struct ip_mreqn    mul;
int                rval = -1;
socklen_t          slen;

	if ( (sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		printErrMsg( "ERROR: unable to create socket: %s\n", strerror(errno) );
		return -1;
	}

	val = 1;

	if ( setsockopt( sd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof( val ) ) ) {
		printErrMsg( "ERROR: setsockopt(SO_REUSEADDR) failed: %s\n", strerror(errno) );
		goto bail;
	}

	mul.imr_multiaddr.s_addr = sap->sin_addr.s_addr;
	mul.imr_address.s_addr   = inet_addr( "127.0.0.1" );
	mul.imr_ifindex          = 0; //if_nametoindex("eth0");

/*
	val = 1;
	if ( setsockopt( sd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof( val ) ) ) {
		printErrMsg( "ERROR: setsockopt(SO_REUSEPORT) failed: %s\n", strerror(errno) );
		return 1;
	}

	if ( setsockopt( sd, SOL_SOCKET, SO_BROADCAST, &val, sizeof( val ) ) ) {
		printErrMsg( "ERROR: setsockopt(SO_BROADCAST) failed: %s\n", strerror(errno) );
		return 1;
	}
*/

	sin.sin_family      = AF_INET;
	// The 'client' doesn't want to receive MC messages it sends to servers.
	// Avoid that by binding a client to a different (not the MC) address.
	sin.sin_addr.s_addr = srv ? INADDR_ANY : inet_addr("127.0.0.1");
	sin.sin_port        = sap->sin_port;

	if ( bind( sd, (struct sockaddr*)&sin, sizeof(sin) ) ) {
		printErrMsg( "ERROR: unable to bind: %s\n", strerror(errno) );
		goto bail;
	}

	if ( ntohs(0) == sin.sin_port ) {
		slen = sizeof(sin);
		if ( getsockname( sd, (struct sockaddr*)&sin, &slen ) ) {
			printErrMsg( "ERROR: getsockname failed: %s\n", strerror(errno) );
			goto bail;
		}
		sap->sin_port = sin.sin_port;
	}

	if ( srv ) {
		if ( setsockopt( sd, SOL_IP, IP_ADD_MEMBERSHIP, &mul, sizeof(mul) ) ) {
			printErrMsg( "ERROR: setsockopt(IP_ADD_MEMBERSHIP) failed: %s\n", strerror(errno) );
			goto bail;
		}
	} else {
        if ( setsockopt( sd, SOL_IP, IP_MULTICAST_IF, &mul.imr_address.s_addr, sizeof(mul.imr_address.s_addr) ) ) {
			printErrMsg( "ERROR: setsockopt(IP_MULTICAST_IF) failed: %s\n", strerror(errno) );
			goto bail;
        }
	}

	val = 1;
	if ( setsockopt( sd, SOL_IP, IP_MULTICAST_LOOP, &val, sizeof(val) ) ) {
		printErrMsg( "ERROR: setsockopt(IP_MULTICAST_LOOP) failed: %s\n", strerror(errno) );
		goto bail;
	}

	val = 0;
	if ( setsockopt( sd, SOL_IP, IP_MULTICAST_TTL, &val, sizeof(val) ) ) {
		printErrMsg( "ERROR: setsockopt(IP_MULTICAST_TTL) failed: %s\n", strerror(errno) );
		goto bail;
	}

	rval = sd;
	sd   = -1;

bail:
	if ( sd >= 0 )
		close( sd );
	return rval;
}

CLIENT *
mkMcClnt(struct sockaddr_in *sap, unsigned long prog, unsigned long vers)
{
	int                sd;
	CLIENT            *clnt;
	struct timeval     tout;
	char               buf[100];

	if (  (sd = mkMcSd(0, sap)) < 0 ) {
		printErrMsg( "Unable to create MC socket: %s\n", strerror(errno) );
		return 0;
	}

	tout.tv_sec  = 1;
	tout.tv_usec = 0;

    clnt = clntudp_create (sap, prog, vers, tout, &sd);
    if (clnt == NULL) {
		inet_ntop(sap->sin_family, &sap->sin_addr, buf, sizeof(buf));
		printErrMsg( "%s\n", clnt_spcreateerror( buf ) );
	}
	return clnt;
}
