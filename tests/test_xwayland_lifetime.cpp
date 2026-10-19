#include "xwayland_lifetime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef GAMESCOPE_WLSERVER_SOURCE
#error "GAMESCOPE_WLSERVER_SOURCE must name src/wlserver.cpp"
#endif

namespace
{

// True when the wlr_xwayland_server_options initializer in `source` passes
// the shared terminate delay (and nothing else sets a zero delay).
bool options_set_terminate_delay( std::string_view source )
{
	constexpr std::string_view marker = "struct wlr_xwayland_server_options xwayland_options";
	size_t start = source.find( marker );
	if ( start == std::string_view::npos )
		return false;
	size_t end = source.find( "};", start );
	if ( end == std::string_view::npos )
		return false;
	std::string_view block = source.substr( start, end - start );
	return block.find( ".terminate_delay = gamescope::xwayland_lifetime::terminate_delay_seconds" ) != std::string_view::npos;
}

std::string read_file( const char *path )
{
	std::ifstream stream( path );
	std::stringstream buffer;
	buffer << stream.rdbuf();
	return buffer.str();
}

} // namespace

TEST_CASE( "terminate delay is long enough for the first X connection and finite" )
{
	REQUIRE( gamescope::xwayland_lifetime::terminate_delay_seconds >= 5 );
	REQUIRE( gamescope::xwayland_lifetime::terminate_delay_seconds <= 60 );
}

TEST_CASE( "the contract checker accepts a block that sets the delay" )
{
	REQUIRE( options_set_terminate_delay(
		"struct wlr_xwayland_server_options xwayland_options = {\n"
		"\t.lazy = false,\n"
		"\t.terminate_delay = gamescope::xwayland_lifetime::terminate_delay_seconds,\n"
		"};\n" ) );
}

TEST_CASE( "the contract checker rejects blocks without the delay" )
{
	REQUIRE_FALSE( options_set_terminate_delay(
		"struct wlr_xwayland_server_options xwayland_options = {\n\t.lazy = false,\n};\n" ) );
	REQUIRE_FALSE( options_set_terminate_delay(
		"struct wlr_xwayland_server_options xwayland_options = {\n\t.terminate_delay = 0,\n};\n" ) );
	REQUIRE_FALSE( options_set_terminate_delay( "no options here" ) );
	// A delay set outside the initializer must not count.
	REQUIRE_FALSE( options_set_terminate_delay(
		"struct wlr_xwayland_server_options xwayland_options = {\n\t.lazy = false,\n};\n"
		".terminate_delay = gamescope::xwayland_lifetime::terminate_delay_seconds" ) );
}

TEST_CASE( "wlserver.cpp creates Xwayland with the terminate delay" )
{
	std::string source = read_file( GAMESCOPE_WLSERVER_SOURCE );
	REQUIRE_FALSE( source.empty() );
	REQUIRE( options_set_terminate_delay( source ) );
}

TEST_CASE( "the delay counts as effective only when wlroots kept it" )
{
	using gamescope::xwayland_lifetime::delay_effective;
	constexpr int requested = gamescope::xwayland_lifetime::terminate_delay_seconds;
	// Feature present: wlroots keeps the requested value.
	REQUIRE( delay_effective( requested, requested ) );
	// Feature absent: wlroots resets the option to 0 (xwayland/server.c).
	REQUIRE_FALSE( delay_effective( requested, 0 ) );
	// A zero request is never an effective delay.
	REQUIRE_FALSE( delay_effective( 0, 0 ) );
	REQUIRE_FALSE( delay_effective( requested, requested + 1 ) );
}

TEST_CASE( "wlserver.cpp fails closed when the created server lost the delay" )
{
	std::string source = read_file( GAMESCOPE_WLSERVER_SOURCE );
	std::string_view view = source;
	size_t create = view.find( "wlr_xwayland_server_create(display, &xwayland_options)" );
	REQUIRE( create != std::string_view::npos );
	size_t check = view.find( "delay_effective(", create );
	REQUIRE( check != std::string_view::npos );
	// The check must be right after creation (same constructor), read the
	// effective option from the created server, and abort.
	std::string_view after = view.substr( check, 600 );
	REQUIRE( after.find( "xwayland_server->options.terminate_delay" ) != std::string_view::npos );
	REQUIRE( after.find( "abort()" ) != std::string_view::npos );
	REQUIRE( check - create < 400 );
}
