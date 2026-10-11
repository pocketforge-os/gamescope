#include "compositor_diagnostics.hpp"

#include <cassert>
#include <cstdlib>
#include <string_view>

int main()
{
	using namespace gamescope::compositor_diagnostics;

	assert( std::string_view( frame_kind( FramePath::Composited ) ) == "composited" );
	assert( std::string_view( frame_kind( FramePath::Direct ) ) == "direct" );
	assert( std::string_view( buffer_kind( true ) ) == "dmabuf" );
	assert( std::string_view( buffer_kind( false ) ) == "shm-or-cpu" );
	assert( should_log_atomic_properties( 1 ) );
	assert( should_log_atomic_properties( 8 ) );
	assert( !should_log_atomic_properties( 0 ) );
	assert( !should_log_atomic_properties( 9 ) );

	unsetenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS" );
	assert( !enabled() );
	setenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS", "1", 1 );
	assert( enabled() );
	setenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS", "0", 1 );
	assert( !enabled() );
}
