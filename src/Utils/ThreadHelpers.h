#pragma once

#include <thread>

namespace gamescope
{

inline void StopAndJoin( std::jthread &thread )
{
	if ( !thread.joinable() )
		return;

	thread.request_stop();
	thread.join();
}

} // namespace gamescope
