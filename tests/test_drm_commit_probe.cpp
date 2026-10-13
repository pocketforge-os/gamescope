#include "drm_commit_probe.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

using gamescope::drm_commit_probe::Phase;
using gamescope::drm_commit_probe::enabled;
using gamescope::drm_commit_probe::phaseName;
using gamescope::drm_commit_probe::shouldLog;

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
	unsetenv( "GAMESCOPE_DRM_COMMIT_DIAGNOSTICS" );
	CHECK( !enabled() );
	CHECK( !shouldLog( 1 ) );

	setenv( "GAMESCOPE_DRM_COMMIT_DIAGNOSTICS", "0", 1 );
	CHECK( !enabled() );
	setenv( "GAMESCOPE_DRM_COMMIT_DIAGNOSTICS", "unexpected", 1 );
	CHECK( !enabled() );

	setenv( "GAMESCOPE_DRM_COMMIT_DIAGNOSTICS", "1", 1 );
	CHECK( enabled() );
	CHECK( !shouldLog( 0 ) );
	CHECK( shouldLog( 1 ) );
	CHECK( shouldLog( gamescope::drm_commit_probe::kMaxCapturedCommits ) );
	CHECK( !shouldLog( gamescope::drm_commit_probe::kMaxCapturedCommits + 1 ) );

	CHECK( std::string_view{ phaseName( Phase::Master ) } == "master" );
	CHECK( std::string_view{ phaseName( Phase::Submit ) } == "submit" );
	CHECK( std::string_view{ phaseName( Phase::WaitForPageFlip ) } == "wait-page-flip" );
	CHECK( std::string_view{ phaseName( Phase::PageFlipHandler ) } == "page-flip-handler" );
	CHECK( std::string_view{ phaseName( Phase::Complete ) } == "complete" );
	CHECK( std::string_view{ phaseName( static_cast<Phase>( 255 ) ) } == "unknown" );

	return g_failures == 0 ? 0 : 1;
}
