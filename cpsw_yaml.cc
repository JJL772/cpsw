#include <cpsw_yaml.h>
#include <cpsw_entry.h>
#include <cpsw_hub.h>
#include <cpsw_error.h>
#include <cpsw_sval.h>
#include <cpsw_mem_dev.h>
#include <cpsw_mmio_dev.h>
#include <cpsw_netio_dev.h>

#include <sstream>

using boost::dynamic_pointer_cast;

#ifndef getNode
#ifndef RECURSIVE_GET_NODE
const YAML::Node getNode(const YAML::Node &node, const char *key)
{
YAML::Node n(node);

	do {
		const YAML::Node & val_node( n[key] );
		if ( val_node )
			return val_node;

		const YAML::Node & merge_node( n["<<"] );

		// seems we have to be really careful when 
		// manipulating nodes (c++ - we don't really
		// know what's happening under the hood).
		// The 'natural' way to write things would
		// result in crashes:
		//
		//   } while ( (n = n["<<"]) );
		//
		// seems we shouldn't assign to 'n' if n["<<"] is
		// not defined.

		if ( ! merge_node )
			return merge_node;
		n = merge_node;

	} while ( 1 );
}
#else
const YAML::Node getNode(const YAML::Node &node, const char *key)
{
	if ( ! node )
		return node;

	const YAML::Node &val( node[key] );

	if ( val )
		return val;

	return getNode( node["<<"], key );
}
#endif
#endif

class CYamlFactoryBaseImpl::TypeRegistry {

	struct StrCmp {
		bool operator () (const char *a, const char *b) const {
			return strcmp(a,b) < 0 ? true : false;
		}
	};

	typedef std::map<const char *, CYamlFactoryBaseImpl *, StrCmp> Map;

	typedef std::pair<const char *, CYamlFactoryBaseImpl *>        Member;

	Map map_;

public:
	virtual void add(const char *name, CYamlFactoryBaseImpl *f)
	{
	std::pair< Map::iterator, bool > ret = map_.insert( Member(name, f) );

		if ( ! ret.second )
			throw DuplicateNameError( name );
	}

	virtual CYamlFactoryBaseImpl *lkup(const char *name)
	{
	CYamlFactoryBaseImpl *rval = map_[ name ];	
		if ( ! rval )
			throw NotFoundError(name);
		return rval;
	}

	virtual void dump()
	{
		Map::iterator it( map_.begin() );
		while ( it != map_.end() ) {
			std::cout << it->first << "\n";
			++it;
		}
	}
};

CYamlFactoryBaseImpl::TypeRegistry * CYamlFactoryBaseImpl::getRegistry()
{
static TypeRegistry theRegistry_;
	return &theRegistry_;
}

CYamlFactoryBaseImpl::CYamlFactoryBaseImpl(const char *typeLabel)
{
	std::cerr << "registering " << typeLabel << "\n";
	getRegistry()->add(typeLabel, this);
}

void
CYamlFactoryBaseImpl::addChildren(CEntryImpl &e, const YAML::Node &node)
{
}

void
CYamlFactoryBaseImpl::addChildren(CDevImpl &d, const YAML::Node &node)
{
const YAML::Node & children( node["children"] );

	std::cout << "node size " << node.size() << "\n";

	YAML::const_iterator it = node.begin();
	while ( it != node.end() ) {
		std::cout << it->first << "\n";
		++it;
	}

	std::cout<<"adding " << children.size() << " children\n";

	if ( children ) {
		YAML::const_iterator it = children.begin();
		while ( it != children.end() ) {
			Field c = dispatchMakeField( *it );
			d.addAtAddress( c, *it );
			++it;
		}
	}
}

Field
CYamlFactoryBaseImpl::dispatchMakeField(const YAML::Node & node)
{
std::string typeLabel;

	mustReadNode(node,"class", &typeLabel);
	std::cout<<"making " << typeLabel << "\n";

	return getRegistry()->lkup( typeLabel.c_str() )->makeField( node );
}

Dev
CYamlFactoryBaseImpl::loadYamlFile(const char *file_name)
{
	/* Root node must be a Dev */
	return dynamic_pointer_cast<Dev::element_type>( dispatchMakeField( YAML::LoadFile( file_name ) ) );
}

Dev
CYamlFactoryBaseImpl::loadYamlStream(std::istream &in)
{
	/* Root node must be a Dev */
	return dynamic_pointer_cast<Dev::element_type>( dispatchMakeField( YAML::Load( in ) ) );
}

Dev
CYamlFactoryBaseImpl::loadYamlStream(const char *yaml)
{
std::string  str( yaml );
std::stringstream sstrm( str );
	return loadYamlStream( sstrm );
}


void 
CYamlFactoryBaseImpl::dumpTypes()
{
	getRegistry()->dump();
}


YAML::Emitter& operator << (YAML::Emitter& out, const ScalVal_RO& s) {
    uint64_t u64;
    s->getVal( &u64 );
    out << YAML::BeginMap;
    out << YAML::Key << s->getName();
    out << YAML::Value << u64;
    out << YAML::EndMap;
    return out;
}

YAML::Emitter& operator << (YAML::Emitter& out, const Hub& h) {
    Children ch = h->getChildren();
    for( unsigned i = 0; i < ch->size(); i++ )
    {
//       out << ch[i]; 
    }
    return out;
}

DECLARE_YAML_FACTORY(EntryImpl);
DECLARE_YAML_FACTORY(DevImpl);
DECLARE_YAML_FACTORY(IntEntryImpl);
DECLARE_YAML_FACTORY(MemDevImpl);
DECLARE_YAML_FACTORY(MMIODevImpl);
DECLARE_YAML_FACTORY(NetIODevImpl);
