#ifndef CPSW_API_TIMEOUT_H

class CTimeout {
protected:
	static const time_t INDEFINITE_S = -1;
public:
	struct timespec tv_;

	// default is indefinite
	CTimeout()
	{
		tv_.tv_sec  = INDEFINITE_S;
		tv_.tv_nsec = 0;
	}

	CTimeout(uint64_t timeout_us)
	{
		if ( timeout_us < 1000000 ) {
			tv_.tv_sec  = 0;
			tv_.tv_nsec = timeout_us * 1000;
		} else {
			tv_.tv_sec  = (timeout_us / 1000000);
			tv_.tv_nsec = (timeout_us % 1000000) * 1000;
		}
	}

	CTimeout(struct timespec tv)
	:tv_(tv)
	{
	}

	CTimeout(time_t s, long ns)
	{
		tv_.tv_sec  = s;
		if ( ns >= 1000000000 )
			throw InvalidArgError("Timeout 'ns' must be < 1E9");
		tv_.tv_nsec = ns;
	}

	bool isIndefinite() const { return tv_.tv_sec == INDEFINITE_S; }
	bool isNone()       const { return tv_.tv_sec ==  0 && tv_.tv_nsec == 0; }

	CTimeout & operator +=(const CTimeout &rhs)
	{
		if ( ! isIndefinite() ) {
			if ( rhs.isIndefinite() )
				return (*this = rhs);
			tv_.tv_sec  += rhs.tv_.tv_sec;
			tv_.tv_nsec += rhs.tv_.tv_nsec;
			if ( tv_.tv_nsec >= 1000000000 ) {
				tv_.tv_nsec -= 1000000000;
				tv_.tv_sec  ++;
			}
		}
		return *this;
	}

	CTimeout & operator -=(const CTimeout &rhs)
	{
		if ( ! isIndefinite() ) {
			if ( rhs.isIndefinite() )
				return (*this = rhs);
			tv_.tv_sec  -= rhs.tv_.tv_sec;
			tv_.tv_nsec -= rhs.tv_.tv_nsec;
			if ( tv_.tv_nsec < 0 ) {
				tv_.tv_nsec += 1000000000;
				tv_.tv_sec  --;
			}
		}
		return *this;
	}

	friend CTimeout operator +(CTimeout lhs, const CTimeout &rhs)
	{
		lhs += rhs;
		return lhs;
	}

	friend CTimeout operator -(CTimeout lhs, const CTimeout &rhs)
	{
		lhs -= rhs;
		return lhs;
	}
};

#endif

