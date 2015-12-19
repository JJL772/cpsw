#include <cpsw_api_user.h>
#include <cpsw_error.h>

#include <cpsw_proto_mod_udp.h>

#include <errno.h>

#include <stdio.h>

#include <sched.h>

//#define UDP_DEBUG

CSockSd::CSockSd()
{
	if ( ( sd_ = ::socket( AF_INET, SOCK_DGRAM, 0 ) ) < 0 ) {
		throw InternalError("Unable to create socket");
	}
}

CSockSd::~CSockSd()
{
	close( sd_ );
}

SockSd CSockSd::create()
{
	CSockSd *s = new CSockSd();
	return SockSd(s);
}

void * CUdpHandlerThread::threadBody(void *arg)
{
	CUdpHandlerThread *obj = static_cast<CUdpHandlerThread *>(arg);
	try {
		obj->threadBody();
	} catch ( CPSWError e ) {
		fprintf(stderr,"CPSW Error (CUdpHandlerThread): %s\n", e.getInfo().c_str());
		throw;
	}
	return 0;
}

void CUdpHandlerThread::getMyAddr(struct sockaddr_in *addr_p)
{
	socklen_t l = sizeof(*addr_p);
	if ( getsockname( sd_->getSd(), (struct sockaddr*)addr_p, &l ) ) {
		throw IOError("getsockname() ", errno);
	}
}

CUdpHandlerThread::CUdpHandlerThread(struct sockaddr_in *dest, struct sockaddr_in *me_p)
: sd_( CSockSd::create() ),
  running_(false)
{
	int    optval = 1;

	struct sockaddr_in me;

	if ( NULL == me_p ) {
		me.sin_family      = AF_INET;
		me.sin_addr.s_addr = INADDR_ANY;
		me.sin_port        = htons( 0 );

		me_p = &me;
	}

	if ( ::setsockopt(  sd_->getSd(),  SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval) ) ) {
		throw IOError("setsockopt(SO_REUSEADDR) ", errno);
	}

	if ( ::bind( sd_->getSd(), (struct sockaddr*)me_p, sizeof(*me_p)) ) {
		throw IOError("bind failed ", errno);
	}

	// connect - filters any traffic from other destinations/fpgas in the kernel
	if ( ::connect( sd_->getSd(), (struct sockaddr*)dest, sizeof(*dest) ) )
		throw IOError("connect failed ", errno);

}

// only start after object is fully constructed
void CUdpHandlerThread::start()
{
	if ( pthread_create( &tid_, NULL, threadBody, this ) )
		throw IOError("unable to create thread ", errno);
	running_ = true;
}

CUdpHandlerThread::~CUdpHandlerThread()
{
	void *rval;
	if ( running_ ) {
		if ( pthread_cancel( tid_ ) ) {
			throw IOError("pthread_cancel: ", errno);
		}
		if ( pthread_join( tid_, &rval  ) ) {
			throw IOError("pthread_join: ", errno);
		}
	}
}

void CUdpRxHandlerThread::threadBody()
{
	ssize_t got;

	while ( 1 ) {
		Buf buf = IBuf::getBuf();
		got = ::read( sd_->getSd(), buf->getPayload(), buf->getSize() );
		if ( got < 0 ) {
			perror("rx thread");
			sleep(10);
			continue;
		}
		buf->setSize( got );
		if ( got > 0 ) {
#ifdef UDP_DEBUG
unsigned i;
uint8_t  *p = buf->getPayload();
unsigned fram = (p[1]<<4) | (p[0]>>4);
unsigned frag = (p[4]<<16) | (p[3] << 8) | p[2];
	for ( i=0; i< (got < 4 ? got : 4); i++ )
		printf("%02x ", buf->getPayload()[i]);
	printf("\n");
#endif

			BufChain bufch = IBufChain::create();
			bufch->addAtTail( buf );

#ifdef UDP_DEBUG
bool st=
#endif
			pOutputQueue_->push( &bufch );

#ifdef UDP_DEBUG
	printf("(UDP %d) fram # %4d, frag # %4d", got, fram, frag); 
if ( st )
     printf(" (SUCC)\n");
else printf(" (DROP)\n");
#endif
		}
	}
}

CUdpRxHandlerThread::CUdpRxHandlerThread(struct sockaddr_in *dest, struct sockaddr_in *me, CBufQueue *oqueue)
: CUdpHandlerThread(dest, me),
	  pOutputQueue_( oqueue )
{
}

void CUdpPeerPollerThread::threadBody()
{
	uint8_t buf[4];
	memset( buf, 0, sizeof(buf) );
	while ( 1 ) {
		if ( ::write( sd_->getSd(), buf, 0 ) < 0 ) {
			perror("poller thread (write)");
			continue;
		}
		if ( sleep( pollSecs_ ) )
			continue; // interrupted by signal
	}
}

CUdpPeerPollerThread::CUdpPeerPollerThread(struct sockaddr_in *dest, struct sockaddr_in *me, unsigned pollSecs)
: CUdpHandlerThread(dest, me),
  pollSecs_(pollSecs)
{
}

CProtoModUdp::CProtoModUdp(CProtoModKey k, struct sockaddr_in *dest, CBufQueueBase::size_type depth, unsigned nThreads)
:CProtoMod(k, depth),
 poller_( CUdpPeerPollerThread(dest, NULL, 4) )
{
	unsigned i;
	struct sockaddr_in me;

	poller_.getMyAddr( &me );

	for ( i=0; i<nThreads; i++ ) {
		rxHandlers_.push_back( CUdpRxHandlerThread( dest, &me, &outputQueue_ ) );
	}

	poller_.start();
	for ( i=0; i<rxHandlers_.size(); i++ ) {
		rxHandlers_[i].start();
	}
}

CProtoModUdp::~CProtoModUdp()
{
}

void CProtoModUdp::dumpInfo(FILE *f)
{
	if ( ! f )
		f = stdout;

	fprintf(f,"CProtoModUdp:\n");
}