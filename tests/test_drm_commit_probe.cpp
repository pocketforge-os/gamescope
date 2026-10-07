#include "drm_commit_probe.hpp"

#include <cstdio>
#include <string_view>

using gamescope::drm_commit_probe::Phase;
using gamescope::drm_commit_probe::phaseName;

namespace
{

int g_failures = 0;

#define CHECK( expression ) check( expression, #expression, __LINE__ )

void check( bool value, const char *expression, int line )
{
	if ( value )
		return;

	std::fprintf( stderr, "test_drm_commit_probe.cpp:%d: CHECK(%s) failed\n",
		line, expression );
	g_failures++;
}

} // namespace

int main()
{
	CHECK( std::string_view{ phaseName( Phase::Submit ) } == "submit" );
	CHECK( std::string_view{ phaseName( Phase::WaitForPageFlip ) } == "wait-page-flip" );
	CHECK( std::string_view{ phaseName( Phase::PageFlipHandler ) } == "page-flip-handler" );
	CHECK( std::string_view{ phaseName( Phase::Complete ) } == "complete" );
	CHECK( std::string_view{ phaseName( static_cast<Phase>( 255 ) ) } == "unknown" );
	return g_failures == 0 ? 0 : 1;
}
