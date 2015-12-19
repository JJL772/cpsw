#ifndef CPSW_ENTRY_H
#define CPSW_ENTRY_H

#include <cpsw_api_builder.h>
#include <cpsw_shared_obj.h>

#include <boost/weak_ptr.hpp>
#include <boost/make_shared.hpp>

using boost::weak_ptr;
using boost::make_shared;

class   Visitor;

class   CEntryImpl;
typedef shared_ptr<CEntryImpl> EntryImpl;

// debugging
extern int cpsw_obj_count;

class CEntryImpl: public virtual IField, public CShObj {
	private:
		// WARNING -- when modifying fields you might need to
		//            modify 'operator=' as well as the copy
		//            constructor!
		std::string	        name_;
		std::string         description_;
		uint64_t    	    size_;
		mutable Cacheable   cacheable_;
		mutable bool        locked_;

	protected:
		// prevent public copy construction -- cannot copy the 'self'
		// member this constructor is intended be used by the 'clone'
		// template which takes care of setting 'self'.
		CEntryImpl(CEntryImpl &ei, Key &k);

	public:
		CEntryImpl(Key &k, const char *name, uint64_t size);

		virtual CEntryImpl *clone(Key &k) { return new CEntryImpl( *this, k ); }

		virtual const char *getName() const
		{
			return name_.c_str();
		}

		virtual const char *getDescription() const
		{
			return description_.c_str();
		}

		virtual void setDescription(const char *);
		virtual void setDescription(const std::string&);

		virtual uint64_t getSize() const
		{
			return size_;
		}

		virtual Cacheable getCacheable() const
		{
			return cacheable_;
		}

		// may throw exception if modified after
		// being attached
		virtual void setCacheable(Cacheable cacheable);

		virtual ~CEntryImpl();

		virtual void setLocked()
		{
			locked_ = true;
		}

		virtual bool getLocked()
		{
			return locked_;
		}

		virtual void accept(IVisitor    *v, RecursionOrder order, int recursionDepth);

		virtual EntryImpl getSelf()            { return getSelfAs<EntryImpl>();       }
		virtual EntryImpl getConstSelf() const { return getSelfAsConst<EntryImpl>(); }

		virtual Hub isHub() const   { return Hub( static_cast<Hub::element_type *>(NULL) ); }
};

#endif