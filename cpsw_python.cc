#include <cpsw_api_user.h>
#include <boost/python.hpp>
#include <boost/python/tuple.hpp>
#include <boost/python/list.hpp>
#include <boost/python/dict.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>
#include <vector>

using namespace boost::python;

namespace cpsw_python {

// Translate CPSW Errors/exceptions into python exceptions
//
// This is complicated by the fact that there is no easy
// way to use 'boost::python': the problem is that exceptions
// must be derived from PyErr_Exception (C-API) and AFAIK
// there is no way to tell the boost::python 'class_' template
// that a class is derived from a python C-API object; only
// c++ classes can be bases of class_''.
// If the python object is not a subclass of PyErr_Exception
// then
//
//   try:
//     function() #raises c++ exception
//   except ClassGeneratedBy_class_template as e:
//     handle(e)
//
// works fine if indeed the expected exception is thrown.
// However, if any *other* (wrapped) exception is thrown then python
// complains:
//
//   SystemError: 'finally' pops bad exception
//  
// probably because the class object that was thrown is not
// a PyErr_Exception.
//
// Thus, we must resort to some C-API Kludgery for dealing with
// CPSW Exceptions...
//
// See also:
//
// http://cplusplus-sig.plu5pluscp.narkive.com/uByjeyti/exception-translation-to-python-classes
//
// The first argument to PyErr_NewException must be a 'type' object which is
// derived from PyErr_Exception; AFAIK, boost::python does not support that yet.
// Thus, we must craft such an object ourselves...


template <typename ECL>
class ExceptionTranslator {
private:
	std::string  name_;
	PyObject    *excTypeObj_;

	ExceptionTranslator &operator=(const ExceptionTranslator &orig);

public:

	ExceptionTranslator(const ExceptionTranslator &orig)
	: name_(orig.name_),
	  excTypeObj_(orig.excTypeObj_)
	{
		if ( excTypeObj_ )
			Py_INCREF(excTypeObj_);
	}

	static bool firstTime()
	{
	static bool firstTime_ = true;
		bool rval  = firstTime_;
		firstTime_ = false;
		return rval;
	}

	// obtain exception type (C-API) object - WITHOUT incrementing
	// the reference count.
	PyObject *getTypeObj() const
	{
		return excTypeObj_;
	}

	ExceptionTranslator(const char *name, PyObject *base=0)
	: name_(name),
	  excTypeObj_(0)
	{
		if ( ! firstTime() )
			throw std::exception();

		std::string scopeName = extract<std::string>(scope().attr("__name__"));

		char qualifiedName[ scopeName.size() + name_.size() + 1 + 1 ];
		::strcpy(qualifiedName, scopeName.c_str());
		::strcat(qualifiedName, ".");
		::strcat(qualifiedName, name_.c_str());

		// create a new C-API class which is derived from 'base'
		// (must be a subclass of PyErr_Exception)
		excTypeObj_ = PyErr_NewException( qualifiedName, base, 0 );

		//std::cout << qualifiedName << " typeObj_ refcnt " << Py_REFCNT( excTypeObj_ ) << "\n";

		// Register in the current scope
		scope current;

		current.attr(name_.c_str()) = object( handle<>( borrowed( excTypeObj_ ) ) ); \

     	//std::cout << "EXCEPTION TYPE REFCOUNT CRE " << Py_REFCNT(getTypeObj()) << "\n";
	}

	~ExceptionTranslator()
	{
		//std::cout << "~" << name_ << " typeObj_ refcnt " << Py_REFCNT( getTypeObj() ) << "\n";
		if ( excTypeObj_ )
			Py_DECREF( excTypeObj_ );
	}

	// The actual translation
	void operator() (const ECL &e) const
	{
     //std::cout << "EXCEPTION TYPE REFCOUNT PRE " << Py_REFCNT(getTypeObj()) << "\n";
		PyErr_SetString( getTypeObj(), e.what() );
     //std::cout << "EXCEPTION TYPE REFCOUNT PST " << Py_REFCNT(getTypeObj()) << "\n";
	}

// Macros to save typing
#define ExceptionTranslatorInstall(clazz) \
	ExceptionTranslator<clazz> tr_##clazz(#clazz); \
	register_exception_translator<clazz>( tr_##clazz );

#define ExceptionTranslatorInstallDerived(clazz, base) \
	ExceptionTranslator<clazz> tr_##clazz(#clazz, tr_##base.getTypeObj()); \
	register_exception_translator<clazz>( tr_##clazz );
};

static void wrap_Path_loadConfigFromYaml(Path p, const char *filename)
{
YAML::Node conf( YAML::LoadFile( filename ) );
	p->loadConfigFromYaml( conf );
}

// raii for file stream
class ofs : public std::fstream {
public:
	ofs(const char *filename)
	: std::fstream( filename, out )
	{
	}

	~ofs()
	{
		close();
	}
};

static void wrap_Path_dumpConfigToYaml(Path p, const char *filename)
{
YAML::Node conf;
	p->dumpConfigToYaml( conf );

YAML::Emitter emit;
	emit << conf;

ofs strm( filename );
	strm << emit.c_str() << "\n";
}

// Need wrappers for methods which take 
// shared pointers to const objects which
// do not seem to be handled correctly
// by boost::python.
static void wrap_Path_clear(Path p, shared_ptr<IHub> h)
{
Hub hc(h);
	p->clear(hc);
}

static Path wrap_Path_create(shared_ptr<IHub> h)
{
Hub hc(h);
	return IPath::create(hc);
}

static boost::python::dict wrap_Enum_getItems(Enum enm)
{
boost::python::dict d;

IEnum::iterator it  = enm->begin();
IEnum::iterator ite = enm->end();
	while ( it != ite ) {
		d[ *(*it).first ] = (*it).second;
		++it;
	}

	return d;
}

// FIXME: write a better function; one which also supports the buffer interface
static boost::python::object wrap_ScalVal_RO_getVal(ScalVal_RO val)
{
boost::python::object o;

Enum     enm   = val->getEnum();
unsigned nelms = val->getNelms();
unsigned got;

	if ( enm ) {

	std::vector<CString>  str;

		str.reserve(nelms);
		got = val->getVal( &str[0], nelms );
		if ( 1 == got ) {
			return boost::python::object( *str[0] );	
		}

		boost::python::list l;
		for ( unsigned i = 0; i<got; i++ ) {
			l.append( *str[i] );
		}
		return l;
	} else {

	std::vector<uint64_t> v64;

		v64.reserve(nelms);

		got = val->getVal( &v64[0], nelms );
		if ( 1 == got ) {
			return boost::python::object( v64[0] );
		}

		boost::python::list l;
		for ( unsigned i = 0; i<got; i++ ) {
			l.append( v64[i] );
		}
		return l;
	}
}

class ViewGuard {
private:
	Py_buffer *theview_;

public:
	ViewGuard(Py_buffer *view)
	: theview_(view)
	{
	}

	~ViewGuard()
	{
		PyBuffer_Release( theview_ );
	}
};

static unsigned wrap_ScalVal_setVal(ScalVal val, object &o)
{
PyObject *op = o.ptr(); // no need for incrementing the refcnt while 'o' is alive
Py_buffer view;

	if (    PyObject_CheckBuffer( op )
	     && 0 == PyObject_GetBuffer( op, &view, PyBUF_C_CONTIGUOUS ) ) {
		// new style buffer interface
		ViewGuard guard( &view );

		Py_ssize_t nelms = view.len / view.itemsize;

		IndexRange rng(0, nelms-1);

		if        ( view.itemsize == sizeof(uint8_t ) ) {
			uint8_t *bufp = reinterpret_cast<uint8_t*>(view.buf);
			// set same value to all elements ? 
			return 1==nelms ? val->setVal( (uint64_t)*bufp ) : val->setVal( bufp, nelms, &rng );
		} else if ( view.itemsize == sizeof(uint16_t) ) {
			uint16_t *bufp = reinterpret_cast<uint16_t*>(view.buf);
			return 1==nelms ? val->setVal( (uint64_t)*bufp ) : val->setVal( bufp, nelms, &rng );
		} else if ( view.itemsize == sizeof(uint32_t) ) {
			uint32_t *bufp = reinterpret_cast<uint32_t*>(view.buf);
			return 1==nelms ? val->setVal( (uint64_t)*bufp ) : val->setVal( bufp, nelms, &rng );
		} else if ( view.itemsize == sizeof(uint64_t) ) {
			uint64_t *bufp = reinterpret_cast<uint64_t*>(view.buf);
			return 1==nelms ? val->setVal( (uint64_t)*bufp ) : val->setVal( bufp, nelms, &rng );
		}

		// if we get here then we couldn't deal with the buffer interface;
		// try the hard way...
	}

	if ( ! PySequence_Check( o.ptr() ) ) {
		return val->setVal( extract<uint64_t>( o ) );
	}

	unsigned nelms = len(o);

	IndexRange rng(0, nelms-1);

	std::vector<uint64_t> v64;
	for ( int i = 0; i < nelms; ++i ) {
			v64.push_back( extract<uint64_t>( o[i] ) );
	}
	return val->setVal( &v64[0], nelms, &rng );


	throw InvalidArgError("Unable to convert python argument");
}

static int64_t wrap_Stream_read(Stream val, object &o)
{
PyObject *op = o.ptr(); // no need for incrementing the refcnt while 'o' is alive
Py_buffer view;
	if ( !  PyObject_CheckBuffer( op )
	     || 0 != PyObject_GetBuffer( op, &view, PyBUF_C_CONTIGUOUS | PyBUF_WRITEABLE ) ) {
		throw InvalidArgError("Require an object which implements the buffer interface");
	}
	ViewGuard guard( &view );

	return val->read( reinterpret_cast<uint8_t*>(view.buf), view.len );
}

static int64_t wrap_Stream_write(Stream val, object &o)
{
PyObject *op = o.ptr(); // no need for incrementing the refcnt while 'o' is alive
Py_buffer view;
	if ( !  PyObject_CheckBuffer( op )
	     || 0 != PyObject_GetBuffer( op, &view, PyBUF_C_CONTIGUOUS ) ) {
		throw InvalidArgError("Require an object which implements the buffer interface");
	}
	ViewGuard guard( &view );

	return val->write( reinterpret_cast<uint8_t*>(view.buf), view.len );
}

// wrap IPathVisitor to call back into python (assuming the visitor
// is implemented there, of course)
class WrapPathVisitor : public IPathVisitor {
private:
	PyObject *self_;

public:
	WrapPathVisitor(PyObject *self)
	: self_(self)
	{
		std::cout << "creating WrapPathVisitor\n";
	}

	virtual bool visitPre(ConstPath here)
	{
		return call_method<bool>(self_, "visitPre", here);
	}

	virtual void visitPost(ConstPath here)
	{
		call_method<void>(self_, "visitPost", here);
	}

	virtual ~WrapPathVisitor()
	{
		std::cout << "deleting WrapPathVisitor\n";
	}
};

BOOST_PYTHON_MODULE(pycpsw)
{

	register_ptr_to_python<Child                          >();
	register_ptr_to_python<Hub                            >();
	register_ptr_to_python<Path                           >();
	register_ptr_to_python<ConstPath                      >();
	register_ptr_to_python<Enum                           >();
	register_ptr_to_python< shared_ptr<IScalVal_Base>     >();
	register_ptr_to_python<ScalVal_RO                     >();
	register_ptr_to_python<ScalVal                        >();
	register_ptr_to_python<Stream                         >();

	register_ptr_to_python< shared_ptr<std::string const> >();

	// wrap 'IEntry' interface
	class_<IEntry, boost::noncopyable> EntryClazz("Entry", no_init);
	EntryClazz
		.def("getName",        &IEntry::getName)
		.def("getSize",        &IEntry::getSize)
		.def("getDescription", &IEntry::getDescription)
		.def("isHub",          &IEntry::isHub)
	;

	// wrap 'IChild' interface
	class_<IChild, bases<IEntry>, boost::noncopyable> ChildClazz("Child", no_init);
	ChildClazz
		.def("getOwner",       &IChild::getOwner)
		.def("getNelms",       &IChild::getNelms)
	;

	// wrap 'IHub' interface
	class_<IHub, bases<IEntry>, boost::noncopyable> HubClazz("Hub", no_init);
	HubClazz
		.def("findByName",     &IHub::findByName)
		.def("getChild",       &IHub::getChild)
		.def("loadYamlFile",   &IHub::loadYamlFile)
		.staticmethod("loadYamlFile")
	;

	// wrap 'IPath' interface
    class_<IPath, boost::noncopyable> PathClazz("Path", no_init);
	PathClazz
		.def("findByName",   &IPath::findByName)
		.def("up",           &IPath::up)
		.def("empty",        &IPath::empty)
		.def("size",         &IPath::size)
		.def("clear",        wrap_Path_clear)
		.def("origin",       &IPath::origin)
		.def("parent",       &IPath::parent)
		.def("tail",         &IPath::tail)
		.def("toString",     &IPath::toString)
		.def("verifyAtTail", &IPath::verifyAtTail)
		.def("append",       &IPath::append)
		.def("explore",      &IPath::explore)
		.def("concat",       &IPath::concat)
		.def("clone",        &IPath::clone)
		.def("getNelms",     &IPath::getNelms)
		.def("getTailFrom",  &IPath::getTailFrom)
		.def("getTailTo",    &IPath::getTailTo)
		.def("loadConfigFromYaml", wrap_Path_loadConfigFromYaml)
		.def("dumpConfigToYaml",   wrap_Path_dumpConfigToYaml)
		.def("create",       wrap_Path_create)
		.staticmethod("create")
	;

	class_<IPathVisitor, WrapPathVisitor, boost::noncopyable, boost::shared_ptr<WrapPathVisitor> > WrapPathVisitorClazz("PathVisitor");

	// wrap 'IEnum' interface
	class_<IEnum, boost::noncopyable> Enum_Clazz("Enum", no_init);
	Enum_Clazz
		.def("getNelms",     &IEnum::getNelms)
		.def("getItems",     wrap_Enum_getItems)
	;

	// wrap 'IScalVal_Base' interface
	class_<IScalVal_Base, bases<IEntry>, boost::noncopyable> ScalVal_BaseClazz("ScalVal_Base", no_init);
	ScalVal_BaseClazz
		.def("getNelms",     &IScalVal_Base::getNelms)
		.def("getSizeBits",  &IScalVal_Base::getSizeBits)
		.def("isSigned",     &IScalVal_Base::isSigned)
		.def("getPath",      &IScalVal_Base::getPath)
		.def("getEnum",      &IScalVal_Base::getEnum)
	;

	// wrap 'IScalVal_RO' interface
	class_<IScalVal_RO, bases<IScalVal_Base>, boost::noncopyable> ScalVal_ROClazz("ScalVal_RO", no_init);
	ScalVal_ROClazz
		.def("getVal",       wrap_ScalVal_RO_getVal)
		.def("create",       &IScalVal_RO::create)
		.staticmethod("create")
	;

	// wrap 'IScalVal' interface
	class_<IScalVal, bases<IScalVal_RO>, boost::noncopyable> ScalVal_Clazz("ScalVal", no_init);
	ScalVal_Clazz
		.def("setVal",       wrap_ScalVal_setVal)
		.def("create",       &IScalVal::create)
		.staticmethod("create")
	;

	// wrap 'IStream' interface
	class_<IStream, boost::noncopyable> Stream_Clazz("Stream", no_init);
	Stream_Clazz
		.def("read",         wrap_Stream_read)
		.def("write",        wrap_Stream_write)
		.def("create",       &IStream::create)
		.staticmethod("create")
	;

	// wrap 'ICommand' interface
	class_<ICommand, bases<IEntry>, boost::noncopyable> Command_Clazz("Command", no_init);
	Command_Clazz
		.def("execute",      &ICommand::execute)
		.def("create",       &ICommand::create)
		.staticmethod("create")
	;

	// these macros must all be executed from the same scope so
	// that the 'translator' objects of base classes are still
	// 'alive'.

	ExceptionTranslatorInstall(CPSWError);
	ExceptionTranslatorInstallDerived(DuplicateNameError,           CPSWError);
	ExceptionTranslatorInstallDerived(NotDevError,                  CPSWError);
	ExceptionTranslatorInstallDerived(NotFoundError,                CPSWError);
	ExceptionTranslatorInstallDerived(InvalidPathError,             CPSWError);
	ExceptionTranslatorInstallDerived(InvalidIdentError,            CPSWError);
	ExceptionTranslatorInstallDerived(InvalidArgError,              CPSWError);
	ExceptionTranslatorInstallDerived(AddressAlreadyAttachedError,  CPSWError);
	ExceptionTranslatorInstallDerived(ConfigurationError,           CPSWError);
	ExceptionTranslatorInstallDerived(ErrnoError,                   CPSWError);
	ExceptionTranslatorInstallDerived(InternalError,                ErrnoError);
	ExceptionTranslatorInstallDerived(AddrOutOfRangeError,          CPSWError);
	ExceptionTranslatorInstallDerived(ConversionError,              CPSWError);
	ExceptionTranslatorInstallDerived(InterfaceNotImplementedError, CPSWError);
	ExceptionTranslatorInstallDerived(IOError,                      ErrnoError);
	ExceptionTranslatorInstallDerived(BadStatusError,               CPSWError);
	ExceptionTranslatorInstallDerived(IntrError,                    CPSWError);
	ExceptionTranslatorInstallDerived(StreamDoneError,              CPSWError);
	ExceptionTranslatorInstallDerived(FailedStreamError,            CPSWError);
	ExceptionTranslatorInstallDerived(MissingOnceTagError,          CPSWError);
	ExceptionTranslatorInstallDerived(MissingIncludeFileNameError,  CPSWError);
	ExceptionTranslatorInstallDerived(NoYAMLSupportError,           CPSWError);

}

}