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
