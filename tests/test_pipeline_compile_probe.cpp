#include "pipeline_compile_probe.hpp"

#include <cassert>
#include <string_view>

int main()
{
	using namespace gamescope::pipeline_compile_probe;

	assert( std::string_view( source_name( Source::Precompile ) ) == "precompile" );
	assert( std::string_view( source_name( Source::Demand ) ) == "demand" );
	assert( std::string_view( shader_name( 0 ) ) == "BLIT" );
	assert( std::string_view( shader_name( 8 ) ) == "OUTPUT_ROTATE" );

	// Negative controls keep diagnostics fail-closed when a future caller adds an enum value.
	assert( std::string_view( source_name( static_cast<Source>( 99 ) ) ) == "unknown" );
	assert( std::string_view( shader_name( 99 ) ) == "UNKNOWN" );
}
