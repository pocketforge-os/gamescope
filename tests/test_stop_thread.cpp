#include "Utils/ThreadHelpers.h"

#include <atomic>
#include <thread>

int main()
{
	std::atomic<bool> stopped = false;
	std::jthread active( [&stopped]( std::stop_token token ) {
		while ( !token.stop_requested() )
			std::this_thread::yield();
		stopped = true;
	} );
	gamescope::StopAndJoin( active );
	if ( active.joinable() || !stopped.load() )
		return 1;

	std::jthread empty;
	gamescope::StopAndJoin( empty );
	return empty.joinable() ? 1 : 0;
}
