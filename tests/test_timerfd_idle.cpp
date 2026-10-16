#include "waitable.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <string_view>
#include <time.h>

// Keep this regression test independent of the compositor's logging and option
// graph. The waitable header only needs these symbols for its error paths.
struct LogConVar_t {};

LogScope::LogScope( std::string_view name, LogPriority priority )
	: m_psvName{ name }
	, m_psvPrefix{ name }
	, m_eMaxPriority{ priority }
{
}

LogScope::LogScope( std::string_view name, std::string_view prefix, LogPriority priority )
	: m_psvName{ name }
	, m_psvPrefix{ prefix }
	, m_eMaxPriority{ priority }
{
}

LogScope::~LogScope() = default;

void LogScope::errorf( const char *, ... ) {}
void LogScope::errorf_errno( const char *, ... ) {}

LogScope g_WaitableLog{ "timerfd-idle-test" };

timespec nanos_to_timespec( uint64_t nanos )
{
	return {
		.tv_sec = time_t( nanos / 1'000'000'000ull ),
		.tv_nsec = long( nanos % 1'000'000'000ull ),
	};
}

uint64_t monotonic_nanos()
{
	timespec now = {};
	clock_gettime( CLOCK_MONOTONIC, &now );
	return uint64_t( now.tv_sec ) * 1'000'000'000ull + uint64_t( now.tv_nsec );
}

class CountingTimer final : public gamescope::ITimerWaitable
{
public:
	void OnPollIn() final
	{
		ITimerWaitable::OnPollIn();
		m_wakeups++;
	}

	int wakeups() const { return m_wakeups; }

private:
	int m_wakeups = 0;
};

int main()
{
	CountingTimer timer;
	gamescope::CWaiter<4> waiter;
	if ( !waiter.AddWaitable( &timer ) )
		return 2;

	timer.ArmTimer( monotonic_nanos() + 10'000'000ull );
	if ( waiter.PollEvents( 1000 ) != 1 || timer.wakeups() != 1 )
	{
		std::fprintf( stderr, "timerfd-idle: initial expiration was not delivered once\n" );
		return 1;
	}

	int idleWakeups = 0;
	for ( int i = 0; i < 10'000; i++ )
		idleWakeups += waiter.PollEvents( 0 );

	if ( idleWakeups != 0 || timer.wakeups() != 1 )
	{
		std::fprintf( stderr,
			"timerfd-idle: FAIL post_expiry_wakeups=%d callbacks=%d\n",
			idleWakeups, timer.wakeups() );
		return 1;
	}

	std::printf( "timerfd-idle: PASS post_expiry_wakeups=0 callbacks=1\n" );
	return 0;
}
