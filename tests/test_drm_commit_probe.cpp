#include "drm_commit_probe.hpp"

#include <cstdio>
#include <cstdlib>
#include <string_view>

using gamescope::drm_commit_probe::Phase;
using gamescope::drm_commit_probe::kMaxCapturedEvents;
using gamescope::drm_commit_probe::nextCapturedEvent;
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
	CHECK( std::string_view{ phaseName( Phase::Submit ) } == "submit" );
	CHECK( std::string_view{ phaseName( Phase::WaitForPageFlip ) } == "wait-page-flip" );
	CHECK( std::string_view{ phaseName( Phase::PageFlipHandler ) } == "page-flip-handler" );
	CHECK( std::string_view{ phaseName( Phase::Complete ) } == "complete" );
	CHECK( std::string_view{ phaseName( static_cast<Phase>( 255 ) ) } == "unknown" );

	unsetenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS" );
	CHECK( !shouldLog( 1 ) );
	std::atomic<uint64_t> eventCounter = 0;
	CHECK( nextCapturedEvent( eventCounter ) == 0 );
	CHECK( eventCounter.load() == 0 );
	setenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS", "0", 1 );
	CHECK( !shouldLog( 1 ) );
	setenv( "GAMESCOPE_COMPOSITOR_DIAGNOSTICS", "1", 1 );
	CHECK( !shouldLog( 0 ) );
	CHECK( shouldLog( 1 ) );
	CHECK( shouldLog( kMaxCapturedEvents ) );
	CHECK( !shouldLog( kMaxCapturedEvents + 1 ) );
	eventCounter = kMaxCapturedEvents - 1;
	CHECK( nextCapturedEvent( eventCounter ) == kMaxCapturedEvents );
	CHECK( nextCapturedEvent( eventCounter ) == 0 );
	return g_failures == 0 ? 0 : 1;
}
