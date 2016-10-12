#include <cpsw_preproc.h>
#include <iostream>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

static void usage(const char *nm)
{
	fprintf(stderr,"usage: %s [-D <yaml_dir>] [-Y <yaml_file>] [-v]\n", nm);
	fprintf(stderr,"       preprocess YAML file <yaml_file> (or stdin).\n");
	fprintf(stderr,"       Included files are searched for in <yaml_dir>\n");
	fprintf(stderr,"       or in the 'dirname' of <yaml_file>.\n");
	fprintf(stderr,"  -v   enable verbose mode; comments are added\n");
	fprintf(stderr,"       indicating the start and end of included files.\n");

}

class NoOpDeletor {
public:
	void operator()(StreamMuxBuf::Stream::element_type *obj)
	{
	}
};

int
main(int argc, char **argv)
{
int              opt;
const char      *sep;
std::string main_dir;
const char *yaml_dir = 0;
const char *yaml_fil = 0;
int             rval = 1;
bool            verb = false;

	while ( (opt = getopt(argc, argv, "D:Y:hv")) > 0 ) {
		switch (opt) {	
			case 'h':
				rval = 0;
			default:
				usage(argv[0]);
				return rval;

			case 'v': verb = true;       break;

			case 'D': yaml_dir = optarg; break;
			case 'Y': yaml_fil = optarg; break;
		}
	}


	if ( ! yaml_dir && yaml_fil && (sep = ::strrchr(yaml_fil, '/')) ) {
		main_dir = std::string(yaml_fil);
		main_dir.resize(sep - yaml_fil);
		yaml_dir = main_dir.c_str();
	}

StreamMuxBuf::Stream top_strm;

	if ( yaml_fil ) {
		top_strm = StreamMuxBuf::mkstrm( yaml_fil );
	} else {
		top_strm = StreamMuxBuf::Stream( &std::cin, NoOpDeletor() );
	}

StreamMuxBuf     muxer;
YamlPreprocessor preprocessor( top_strm, &muxer, yaml_dir );

	if ( verb ) {
    	preprocessor.setVerbose( true );
	}

	preprocessor.process();

	std::istream preprocessed_stream( &muxer );

	if ( verb && preprocessor.getSchemaVersionMajor() >= 0 ) {
		std::cout << "#schemaversion "
		          << preprocessor.getSchemaVersionMajor()
		          << "."
		          << preprocessor.getSchemaVersionMinor()
		          << "."
		          << preprocessor.getSchemaVersionRevision()
		          << "\n";
	}

	std::cout << preprocessed_stream.rdbuf();

	return 0;
}

