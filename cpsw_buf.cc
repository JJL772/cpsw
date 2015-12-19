#include <boost/weak_ptr.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/atomic.hpp>
#include <boost/make_shared.hpp>

#include <cpsw_api_user.h>
#include <cpsw_error.h>
#include <cpsw_freelist.h>
#include <cpsw_buf.h>

#include <stdio.h>

using boost::make_shared;

class CBufImpl;
class CBufChainImpl;
typedef shared_ptr<CBufImpl>      BufImpl;
typedef shared_ptr<CBufChainImpl> BufChainImpl;

using boost::weak_ptr;
using boost::static_pointer_cast;
using boost::lockfree::detail::freelist_stack;
using boost::atomic;
using boost::memory_order_relaxed;
using boost::memory_order_release;
using boost::memory_order_acquire;

#define NULLBUF   BufImpl( reinterpret_cast<CBufImpl*>(0) )
#define NULLCHAIN BufChainImpl( reinterpret_cast<CBufChainImpl*>(0) )

class CBufImpl : public IBuf, public CFreeListNode<CBufImpl> {
private:
	weak_ptr<CBufChainImpl> chain_;
	BufImpl            next_; 
	weak_ptr<CBufImpl> prev_;
	unsigned beg_,  end_;
	uint8_t  data_[3*512 - 2*sizeof(prev_) - sizeof(CFreeListNode<CBufImpl>) - 2*sizeof(beg_)];
protected:
	virtual void     addToChain(BufImpl p, bool);
	virtual void     delFromChain();

public:
	CBufImpl(CFreeListNodeKey<CBufImpl> k);

	virtual void     addToChain(BufChainImpl c);

	size_t   getCapacity() { return sizeof(data_);                    }
	size_t   getSize()     { return end_ - beg_;                      }
	uint8_t *getPayload()  { return data_ + beg_;                     }

	void     setSize(size_t);
	void     setPayload(uint8_t*);
	void     reinit();

	Buf      getNext()      { return next_; }
	Buf      getPrev()      { return prev_.expired() ? NULLBUF : Buf(prev_);      }

	BufImpl  getNextImpl()  { return next_; }
	BufImpl  getPrevImpl()  { return prev_.expired() ? NULLBUF : BufImpl(prev_);  }

	BufChainImpl getChainImpl() { return chain_.expired() ? NULLCHAIN : BufChainImpl(chain_); }
	BufChain getChain();

	void     after(Buf);
	void     before(Buf);
	void     unlink();
	void     split();

	// We don't need to unlink a buffer when it is destroyed:
	// Only the first one in a chain can ever be destroyed (because
	// strong refs to all others in a chain exist). If this happens
	// then the 'prev' pointer of the following node expires which 
	// yields the correct result: a subsequent getPrev() on the 
	// second/following node will return NULL.
	//virtual ~CBufImpl() { }

	static BufImpl getBuf(size_t capa);

	static CFreeList<CBufImpl> freeList;
};

CFreeList<CBufImpl> CBufImpl::freeList;

class CBufChainImpl : public IBufChain, public CFreeListNode<CBufChainImpl> {
private:
	BufChainImpl strong_self_;
	BufImpl      head_;
	BufImpl      tail_;
	unsigned     len_;
	size_t       size_;

	virtual void setHead(BufImpl h)     { head_ = h; }
	virtual void setTail(BufImpl t)     { tail_ = t; }

	BufChain     yield_ownership();

	friend class CBufImpl;
	friend void IBufChain::take_ownership(BufChain*);

public:
	CBufChainImpl( CFreeListNodeKey<CBufChainImpl> k );

	virtual Buf      getHead()          { return head_; }
	virtual Buf      getTail()          { return tail_; }
	virtual BufImpl  getHeadImpl()      { return head_; }
	virtual BufImpl  getTailImpl()      { return tail_; }

	virtual unsigned getLen()           { return len_;  } // # of buffers in chain
	virtual size_t   getSize()          { return size_; } // in bytes

	virtual void     addSize(ssize_t s) { size_ += s; }
	virtual void     addLen(int l)      { len_  += l; }

	virtual void     setSize(size_t s)  { size_  = s; }
	virtual void     setLen(unsigned l) { len_   = l; }

	virtual Buf      createAtHead();
	virtual Buf      createAtTail();

	virtual void     addAtHead(Buf b);
	virtual void     addAtTail(Buf b);

	static BufChainImpl createImpl();

	static CFreeList<CBufChainImpl> freeList;
};

CFreeList<CBufChainImpl> CBufChainImpl::freeList;

CBufImpl::CBufImpl(CFreeListNodeKey<CBufImpl> k)
: CFreeListNode<CBufImpl>( k ),
  beg_(0),
  end_(sizeof(data_))
{
}

void CBufImpl::setSize(size_t s)
{
unsigned     old_size = getSize();
unsigned     e;
BufChainImpl c;

	if ( (e = beg_ + s) > sizeof(data_) )
		throw InvalidArgError("requested size too big");
	end_ = e;

	if ( (c=getChainImpl()) ) {
		c->addSize( getSize() - old_size );
	}
}

void CBufImpl::setPayload(uint8_t *p)
{
unsigned     old_size = getSize();
BufChainImpl c;

	if ( !p ) {
		beg_ = 0;
	} else if ( p < data_ || p > data_ + sizeof(data_) ) {
		throw InvalidArgError("requested payload pointer out of range");
	} else  {
		beg_ = p - data_;
		if ( end_ < beg_ )
			end_ = beg_;
	}
	if ( (c=getChainImpl()) ) {
		c->addSize( getSize() - old_size );
	}
}

void CBufImpl::reinit()
{
unsigned     old_size = getSize();
BufChainImpl c;
	beg_ = 0;
	end_ = sizeof(data_);
	if ( (c=getChainImpl()) ) {
		c->addSize( getSize() - old_size );
	}
}

void CBufImpl::addToChain(BufImpl p, bool before)
{
BufChainImpl c  = getChainImpl();
BufChainImpl pc = p->getChainImpl();

	if ( c )
		throw InternalError("buffer already on a chain");

	if ( pc ) {
		chain_ = c = pc;
		if ( before ) {
			if ( (p == c->getHeadImpl()) ) {
				BufImpl me = getSelf();
				c->setHead( me );
			}
		} else {
			if ( (p == c->getTailImpl()) ) {
				BufImpl me = getSelf();
				c->setTail( me );
			}
		}
		c->addLen( 1 );
		c->addSize( getSize() );
	}
}

void CBufImpl::addToChain(BufChainImpl c)
{
BufImpl  p,h,t;
BufImpl  me = getSelf();
size_t   s = 0;
unsigned l = 0;
	if ( c->getHead() || c->getTail() )
		throw InternalError("can use this method only to initialize an empty chain"); 

	h = t = me;
	for ( p=me; p; t=p, p=p->getNextImpl() ) {
		if ( p->getChainImpl() )
			throw InternalError("buffer already on a chain");
		s += p->getSize();
		l++;
		p->chain_ = c;
	}
	for ( p=me->getPrevImpl(); p; h=p, p=p->getPrevImpl() ) {
		if ( p->getChainImpl() )
			throw InternalError("buffer already on a chain");
		s += p->getSize();
		l++;
		p->chain_ = c;
	}
	c->setSize( s );
	c->setLen ( l );
	c->setHead( h );
	c->setTail( t );
}

BufChain CBufImpl::getChain()
{
	return getChainImpl();
}

void CBufImpl::delFromChain()
{
BufChainImpl c  = getChainImpl();
	if ( c ) {
		BufImpl me = getSelf();
		if ( c->getHeadImpl() == me ) {
			c->setHead( c->getHeadImpl()->getNextImpl() );
		}
		if ( c->getTailImpl() == me ) {
			c->setTail( c->getTailImpl()->getPrevImpl() );
		}
		c->addLen( -1 );
		c->addSize( - getSize() );
		chain_.reset();
	}
}

void CBufImpl::after(Buf p)
{
BufImpl pi = static_pointer_cast<BufImpl::element_type>(p);
BufImpl me = getSelf();

	if ( pi && pi != me ) {

		if ( getNextImpl() || getPrevImpl() )
			throw InternalError("Cannot enqueue non-empty node");

		addToChain( pi, false ); // no exceptions must happen after this

		prev_        = pi;
		next_        = pi->next_;
		pi->next_    = me;
		if ( getNextImpl() )
			getNextImpl()->prev_ = me;

	}
}

void CBufImpl::before(Buf p)
{
BufImpl pi = static_pointer_cast<BufImpl::element_type>(p);
BufImpl me = getSelf();

	if ( pi && pi != me ) {

		if ( getNextImpl() || getPrevImpl() )
			throw InternalError("Cannot enqueue non-empty node");

		addToChain( pi, true); // no exceptions must happen after this

		next_        = pi;
		prev_        = pi->prev_;
		pi->prev_    = me;
		if ( getPrevImpl() )
			getPrevImpl()->next_ = me;
	}
}

void CBufImpl::unlink()
{
	delFromChain(); // no exceptions must happen after this

	if ( getNextImpl() ) {
		getNextImpl()->prev_ = prev_;
		next_ = NULLBUF;
	}
	if ( getPrevImpl() ) {
		getPrevImpl()->next_ = next_;
		prev_ = NULLBUF;
	}
}

void CBufImpl::split()
{
	if ( getChainImpl() ) {
		throw InternalError("Cannot split buffers which are on a chain");
	}
	if ( getPrevImpl() ) {
		getPrevImpl()->next_ = NULLBUF;
		prev_                = NULLBUF;
	}
}

BufImpl CBufImpl::getBuf(size_t capa)
{
	if ( capa > sizeof(data_) ) {
		throw InternalError("ATM all buffers are std. MTU size");
	}
	return freeList.alloc();
}

Buf IBuf::getBuf(size_t capa)
{
	return CBufImpl::getBuf( capa );
}

unsigned IBuf::numBufsAlloced()
{
	return CBufImpl::freeList.getNumAlloced();
}

unsigned IBuf::numBufsFree()
{
	return CBufImpl::freeList.getNumFree();
}

unsigned IBuf::numBufsInUse()
{
	return CBufImpl::freeList.getNumInUse();
}

CBufChainImpl::CBufChainImpl( CFreeListNodeKey<CBufChainImpl> k )
: CFreeListNode<CBufChainImpl>( k ),
  len_(0),
  size_(0)
{
}

	
BufChainImpl CBufChainImpl::createImpl()
{
	return freeList.alloc();
}

BufChain IBufChain::create()
{
	return CBufChainImpl::createImpl();
}

void CBufChainImpl::addAtHead(Buf b)
{
	if ( ! head_ ) {
		static_pointer_cast<BufImpl::element_type>(b)->addToChain( getSelf() );
	} else {
		b->before( head_ );
	}
}

void CBufChainImpl::addAtTail(Buf b)
{
	if ( ! tail_ ) {
		static_pointer_cast<BufImpl::element_type>(b)->addToChain( getSelf() );
	} else {
		b->after( tail_ );
	}
}

BufChain CBufChainImpl::yield_ownership()
{
BufChain rval = strong_self_;
	strong_self_.reset();
	return rval;
}

void IBufChain::take_ownership(BufChain *current_owner)
{
BufChainImpl me = static_pointer_cast<BufChainImpl::element_type>( *current_owner );
	me->strong_self_ = me;
	(*current_owner).reset();
}

Buf CBufChainImpl::createAtHead()
{
	Buf rval = IBuf::getBuf();
	addAtHead(rval);
	return rval;
}

Buf CBufChainImpl::createAtTail()
{
	Buf rval = IBuf::getBuf();
	addAtTail(rval);
	return rval;
}