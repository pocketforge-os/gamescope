#include "pipeline_compile_probe.hpp"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <time.h>

static uint64_t process_cpu_ns()
{
	timespec ts{};
	assert( clock_gettime( CLOCK_PROCESS_CPUTIME_ID, &ts ) == 0 );
	return uint64_t( ts.tv_sec ) * 1'000'000'000ull + uint64_t( ts.tv_nsec );
}

int main()
{
	using namespace gamescope::pipeline_compile_probe;

	assert( std::string_view( source_name( Source::Precompile ) ) == "precompile" );
	assert( std::string_view( source_name( Source::Demand ) ) == "demand" );
	assert( std::string_view( shader_name( 0 ) ) == "BLIT" );
	assert( std::string_view( shader_name( 8 ) ) == "OUTPUT_ROTATE" );

	unsetenv( "GAMESCOPE_PIPELINE_COMPILE_LOG" );
	assert( !enabled() );
	setenv( "GAMESCOPE_PIPELINE_COMPILE_LOG", "1", 1 );
	assert( enabled() );
	setenv( "GAMESCOPE_PIPELINE_COMPILE_LOG", "0", 1 );
	assert( !enabled() );

	// Eager compilation is opt-in: demand compilation remains the idle-safe default.
	unsetenv( "GAMESCOPE_PIPELINE_PRECOMPILE" );
	assert( !precompile_enabled() );
	setenv( "GAMESCOPE_PIPELINE_PRECOMPILE", "1", 1 );
	assert( precompile_enabled() );
	setenv( "GAMESCOPE_PIPELINE_PRECOMPILE", "0", 1 );
	assert( !precompile_enabled() );
	setenv( "GAMESCOPE_PIPELINE_PRECOMPILE", "invalid", 1 );
	assert( !precompile_enabled() );

	// Negative and positive controls prove that the default idle path starts no
	// CPU worker while an explicit opt-in still starts the precompiler.
	std::atomic<uint64_t> iterations = 0;
	unsetenv( "GAMESCOPE_PIPELINE_PRECOMPILE" );
	const uint64_t idle_cpu_start = process_cpu_ns();
	auto idle_worker = start_precompile( [&]( std::stop_token ) { iterations++; } );
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	const uint64_t idle_cpu_ns = process_cpu_ns() - idle_cpu_start;
	assert( !idle_worker.joinable() );
	assert( iterations == 0 );
	assert( idle_cpu_ns < 10'000'000 );

	setenv( "GAMESCOPE_PIPELINE_PRECOMPILE", "1", 1 );
	const uint64_t busy_cpu_start = process_cpu_ns();
	auto busy_worker = start_precompile( [&]( std::stop_token stop ) {
		while ( !stop.stop_requested() )
			iterations++;
	} );
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
	busy_worker.request_stop();
	busy_worker.join();
	const uint64_t busy_cpu_ns = process_cpu_ns() - busy_cpu_start;
	assert( iterations > 0 );
	assert( busy_cpu_ns > 10'000'000 );

	// Negative controls keep diagnostics fail-closed when a future caller adds an enum value.
	assert( std::string_view( source_name( static_cast<Source>( 99 ) ) ) == "unknown" );
	assert( std::string_view( shader_name( 99 ) ) == "UNKNOWN" );
}
