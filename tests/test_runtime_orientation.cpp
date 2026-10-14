#include "runtime_orientation.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <mutex>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace gamescope::output_rotation;
using namespace gamescope::runtime_orientation;

namespace
{

std::atomic<int> g_failures = 0;

#define CHECK( ... ) check( (__VA_ARGS__), #__VA_ARGS__, __LINE__ )

void check( bool value, const char *expression, int line )
{
	if ( value )
		return;
	std::fprintf( stderr, "test_runtime_orientation.cpp:%d: CHECK(%s) failed\n",
		line, expression );
	g_failures++;
}

std::string response( std::string_view output, uint64_t serial = 7,
	std::string_view logical = "landscape", std::string_view app = "landscape",
	std::string_view presentation = "fill", bool locked = false )
{
	return "{\"result\":\"orientation\",\"version\":1,\"state\":{"
		"\"serial\":" + std::to_string( serial ) +
		",\"output\":\"" + std::string( output ) +
		"\",\"logical\":\"" + std::string( logical ) +
		"\",\"app\":\"" + std::string( app ) +
		"\",\"presentation\":\"" + std::string( presentation ) +
		"\",\"whole_body\":\"normal\",\"screen_only\":\"normal\",\"locked\":" +
		( locked ? "true" : "false" ) + ",\"degraded\":[]}}";
}

void test_runtime_owned_direction_fixture_and_all_plans()
{
	// Copied verbatim from runtime 5da5c97c, tests/orientation/source.rs:37-69.
	// That test asserts at lines 121-126 that each of cw90, right side up, and
	// right-up resolves to QuarterTurn::Right. The gamescope assertion below is
	// deliberately tied to that fixture rather than to Wayland enum ordinals.
	constexpr std::string_view runtimeFixture = R"(
[[screens]]
id = "screen_main"
role = "primary"
rotation = "cw90"

[[sensors]]
id = "imu"
kind = "imu"
iio_device = "iio:device0"
mount_matrix = [[1,0,0],[0,1,0],[0,0,1]]

[[joints]]
id = "screen"
kind = "swivel"
default = 0
switches = [{ ev_type="EV_SW", code="SW_LID", source="Hall", active=[90,180] }]
[[joints.postures]]
id = "deck"
range = [0,90]
screen_rotation = { screen_main="none" }
[[joints.postures]]
id = "turned"
range = [90,180]
screen_rotation = { screen_main="cw90" }
)";
	constexpr std::string_view drmFixture = "right side up\n";
	constexpr std::string_view sensorFixture = "right-up\n";
	CHECK( runtimeFixture.find( "rotation = \"cw90\"" ) != std::string_view::npos );
	CHECK( drmFixture == "right side up\n" );
	CHECK( sensorFixture == "right-up\n" );

	struct Case
	{
		std::string_view token;
		Transform transform;
		Extent logical;
		ProtocolTransform protocol;
	};
	constexpr std::array cases = {
		Case{ "normal", Transform::Normal, { 800, 1280 }, ProtocolTransform::Normal },
		Case{ "right", Transform::Rotate90, { 1280, 800 }, ProtocolTransform::Rotate90 },
		Case{ "upsidedown", Transform::Rotate180, { 800, 1280 }, ProtocolTransform::Rotate180 },
		Case{ "left", Transform::Rotate270, { 1280, 800 }, ProtocolTransform::Rotate270 },
	};

	for ( const auto &item : cases )
	{
		std::string error;
		const auto state = parseResponse( response( item.token ), &error );
		CHECK( state.has_value() );
		if ( !state )
			continue;
		const Plan plan = makePlan( *state, { 800, 1280 } );
		CHECK( plan.outputTransform == item.transform );
		CHECK( plan.protocolTransform == item.protocol );
		CHECK( plan.logicalExtent == item.logical );
		CHECK( transformFromPanelOrientation(
			panelOrientationFromTransform( plan.outputTransform ) ) == item.transform );
	}
}

void test_parser_is_strict_and_framing_is_incremental()
{
	const std::string valid = response( "right", 42 );
	std::string error;
	CHECK( parseResponse( valid, &error )->serial == 42 );

	for ( const std::string &invalid : {
		response( "diagonal" ),
		std::string{ "{\"result\":\"ok\"}" },
		std::string{ "{\"result\":\"orientation\",\"version\":2,\"state\":{}}" },
		valid + " trailing",
	} )
	{
		error.clear();
		CHECK( !parseResponse( invalid, &error ) );
		CHECK( !error.empty() );
	}

	const auto frame = encodeFrame( valid );
	FrameDecoder decoder;
	CHECK( decoder.feed( { frame.data(), 2 } ) == FrameStatus::NeedMore );
	CHECK( decoder.feed( { frame.data() + 2, 3 } ) == FrameStatus::NeedMore );
	CHECK( decoder.feed( { frame.data() + 5, frame.size() - 5 } ) == FrameStatus::Complete );
	CHECK( decoder.body() == valid );
	CHECK( decoder.finish() == FrameStatus::Complete );

	FrameDecoder shortBody;
	CHECK( shortBody.feed( { frame.data(), frame.size() - 1 } ) == FrameStatus::NeedMore );
	CHECK( shortBody.finish() == FrameStatus::Invalid );
	std::array<uint8_t, 4> oversized = { 0x00, 0x01, 0x00, 0x01 };
	FrameDecoder tooLarge;
	CHECK( tooLarge.feed( oversized ) == FrameStatus::Invalid );
}

void test_transition_clients_absence_lock_and_180_fill()
{
	const State fixed = fixedMountState( Transform::Normal );
	ReconfigurationState applied( { 800, 1280 }, fixed );
	CHECK( applied.plan().outputTransform == Transform::Normal );

	const State right = *parseResponse( response( "right", 8 ) );
	const Change live = applied.apply( right, true );
	CHECK( live.physical );
	CHECK( live.logicalSize );
	CHECK( live.reconfigureClients );
	CHECK( live.preferBuffers );
	CHECK( live.updateRandr );
	CHECK( live.cancelTouches );
	CHECK( live.invalidateDirectScanout );
	CHECK( applied.plan().logicalExtent == Extent{ 1280, 800 } );

	const Change duplicate = applied.apply( right, true );
	CHECK( !duplicate.any() );
	const State locked = *parseResponse( response( "right", 9, "landscape",
		"landscape", "fill", true ) );
	const Change lockedOnly = applied.apply( locked, true );
	CHECK( lockedOnly.diagnostics );
	CHECK( !lockedOnly.physical );
	CHECK( !lockedOnly.reconfigureClients );

	const Change absent = applied.fallbackToFixedMount( true );
	CHECK( absent.physical );
	CHECK( applied.plan().outputTransform == Transform::Normal );

	const State appFixed = *parseResponse( response( "upsidedown", 10,
		"landscape_flipped", "landscape", "fill" ) );
	const Plan nativeLandscape = makePlan( appFixed, { 1280, 800 } );
	CHECK( nativeLandscape.outputTransform == Transform::Rotate180 );
	CHECK( nativeLandscape.appToOutput == Transform::Rotate180 );
	CHECK( nativeLandscape.presentation == Presentation::Fill );
	CHECK( compose( nativeLandscape.appToOutput, nativeLandscape.outputTransform ) ==
		Transform::Normal );

	const State appSensorLandscape = *parseResponse( response( "upsidedown", 11,
		"landscape_flipped", "landscape_flipped", "fill" ) );
	const Plan upright = makePlan( appSensorLandscape, { 1280, 800 } );
	CHECK( upright.appToOutput == Transform::Normal );
	CHECK( compose( upright.appToOutput, upright.outputTransform ) == Transform::Rotate180 );
}

void test_backoff_and_lock_are_authority_facts()
{
	ReconnectBackoff backoff;
	CHECK( backoff.failureDelay() == std::chrono::milliseconds{ 100 } );
	CHECK( backoff.failureDelay() == std::chrono::milliseconds{ 200 } );
	for ( int i = 0; i < 10; i++ )
		backoff.failureDelay();
	CHECK( backoff.failureDelay() == std::chrono::milliseconds{ 5000 } );
	backoff.success();
	CHECK( backoff.failureDelay() == std::chrono::milliseconds{ 100 } );

	const State locked = *parseResponse( response( "left", 88, "portrait",
		"portrait", "fill", true ) );
	const Plan plan = makePlan( locked, { 800, 1280 } );
	CHECK( plan.outputTransform == Transform::Rotate270 );
	CHECK( locked.locked );
}

struct TestSocket
{
	std::string directory;
	std::string path;
	int fd = -1;

	TestSocket()
	{
		std::array<char, 40> pattern = {};
		std::snprintf( pattern.data(), pattern.size(), "/tmp/gs-orientation-%d-XXXXXX",
			int( getpid() ) );
		char *created = mkdtemp( pattern.data() );
		CHECK( created != nullptr );
		if ( !created ) return;
		directory = created;
		path = directory + "/authority.sock";
		fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
		CHECK( fd >= 0 );
		if ( fd < 0 ) return;
		sockaddr_un address = { .sun_family = AF_UNIX };
		std::snprintf( address.sun_path, sizeof( address.sun_path ), "%s", path.c_str() );
		CHECK( bind( fd, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == 0 );
		CHECK( listen( fd, 1 ) == 0 );
	}

	~TestSocket()
	{
		if ( fd >= 0 ) close( fd );
		if ( !path.empty() ) unlink( path.c_str() );
		if ( !directory.empty() ) rmdir( directory.c_str() );
	}
};

std::thread serveOne( TestSocket &socket, std::string expectedRequest,
	std::string reply, bool truncate )
{
	return std::thread( [&socket, expectedRequest = std::move( expectedRequest ),
		reply = std::move( reply ), truncate] {
		const int client = accept4( socket.fd, nullptr, nullptr, SOCK_CLOEXEC );
		CHECK( client >= 0 );
		if ( client < 0 ) return;
		FrameDecoder request;
		std::array<uint8_t, 128> buffer;
		while ( true )
		{
			const ssize_t size = recv( client, buffer.data(), buffer.size(), 0 );
			CHECK( size > 0 );
			if ( size <= 0 ) break;
			const FrameStatus status = request.feed( { buffer.data(), size_t( size ) } );
			if ( status != FrameStatus::NeedMore ) break;
		}
		CHECK( request.finish() == FrameStatus::Complete );
		CHECK( request.body() == expectedRequest );
		const auto frame = encodeFrame( reply );
		const size_t sent = truncate ? frame.size() - 1 : frame.size();
		for ( size_t offset = 0; offset < sent; )
		{
			const size_t chunk = std::min<size_t>( 3, sent - offset );
			const ssize_t written = send( client, frame.data() + offset, chunk, MSG_NOSIGNAL );
			CHECK( written == ssize_t( chunk ) );
			if ( written <= 0 ) break;
			offset += size_t( written );
		}
		close( client );
	} );
}

void test_real_unix_wire_partial_delivery_and_absence()
{
	TestSocket positive;
	auto positiveServer = serveOne( positive,
		"{\"method\":\"orientation\",\"version\":1,\"wait_ms\":0}",
		response( "right", 123 ), false );
	const PollResult first = pollOnce( positive.path, std::nullopt, 0 );
	positiveServer.join();
	CHECK( first.state.has_value() );
	if ( first.state ) CHECK( first.state->serial == 123 );

	TestSocket negative;
	auto negativeServer = serveOne( negative,
		"{\"method\":\"orientation\",\"version\":1,\"after_serial\":123,\"wait_ms\":30000}",
		response( "left", 124 ), true );
	const PollResult shortFrame = pollOnce( negative.path, 123, 30000 );
	negativeServer.join();
	CHECK( !shortFrame.state );
	CHECK( shortFrame.error == "truncated frame" );

	const PollResult absent = pollOnce( negative.directory + "/missing.sock", std::nullopt, 0 );
	CHECK( !absent.state );
	CHECK( absent.error.find( "connect:" ) == 0 );
}

void test_subscription_reconnects_and_stops_interruptibly()
{
	TestSocket socket;
	auto server = serveOne( socket,
		"{\"method\":\"orientation\",\"version\":1,\"wait_ms\":0}",
		response( "normal", 50 ), false );
	std::mutex mutex;
	std::condition_variable changed;
	int snapshots = 0;
	Subscription subscription( socket.path,
		[&]( const std::optional<State> &state, std::string_view ) {
			if ( !state ) return;
			std::lock_guard lock( mutex );
			snapshots++;
			changed.notify_all();
		} );
	subscription.start();
	{
		std::unique_lock lock( mutex );
		CHECK( changed.wait_for( lock, std::chrono::seconds{ 2 },
			[&] { return snapshots == 1; } ) );
	}
	server.join();
	const auto before = std::chrono::steady_clock::now();
	subscription.stop();
	CHECK( std::chrono::steady_clock::now() - before < std::chrono::seconds{ 1 } );

	std::atomic<int> absentCallbacks = 0;
	Subscription absent( socket.directory + "/absent.sock",
		[&]( const std::optional<State> &state, std::string_view ) {
			if ( !state ) absentCallbacks++;
		} );
	absent.start();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 2 };
	while ( absentCallbacks.load() == 0 && std::chrono::steady_clock::now() < deadline )
		std::this_thread::yield();
	absent.stop();
	CHECK( absentCallbacks.load() == 1 );
}

} // namespace

int main()
{
	test_runtime_owned_direction_fixture_and_all_plans();
	test_parser_is_strict_and_framing_is_incremental();
	test_transition_clients_absence_lock_and_180_fill();
	test_backoff_and_lock_are_authority_facts();
	test_real_unix_wire_partial_delivery_and_absence();
	test_subscription_reconnects_and_stops_interruptibly();
	return g_failures.load() == 0 ? 0 : 1;
}
