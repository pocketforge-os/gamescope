// pf-focus-client: the gamescope compositor.sock focus client against a fake
// router, with the runtime golden fixtures.
//
// Golden fixtures (tests/fixtures/pf-focus-client/) are byte-identical copies
// from pocketforge-os/runtime commit 7b97af8ffe1acb2665b946f97f4473728ba2ccb1
// (runtime#199): crates/pf-input-ipc/tests/golden/{register,state_snapshot,focus}.json
// and crates/pf-peer-identity/tests/fixtures/app_id_from_unit.tsv.
//
// The fixture directory is PF_FOCUS_FIXTURES (see tests/meson.build).

#include <catch2/catch_test_macros.hpp>

#include "pf_focus_client.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace gamescope::pf_focus;
using namespace std::chrono_literals;

static const std::string g_fixtures = PF_FOCUS_FIXTURES;

static std::string read_golden( const std::string &name )
{
	std::ifstream in( g_fixtures + "/" + name, std::ios::binary );
	REQUIRE( in.good() );
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

static FocusState app_focus( const char *id )
{
	FocusState f;
	f.surface = FocusedSurface::app( id );
	return f;
}

// ---------------------------------------------------------------- encoder

TEST_CASE( "golden: register frame body is byte-identical" )
{
	CHECK( encode_register() == read_golden( "register.json" ) );
}

TEST_CASE( "golden: state_snapshot frame body is byte-identical" )
{
	CHECK( encode_state_snapshot( 1, FocusState{} ) == read_golden( "state_snapshot.json" ) );
}

TEST_CASE( "golden: focus frame body is byte-identical" )
{
	CHECK( encode_focus( 1, app_focus( "org.example.game" ) ) == read_golden( "focus.json" ) );
}

TEST_CASE( "encoder: pong and none focus" )
{
	CHECK( encode_pong( 9, 3 ) == R"({"version":1,"type":"pong","epoch":9,"seq":3})" );
	CHECK( encode_focus( 18446744073709551615ull, FocusState{} ) ==
		R"({"version":1,"type":"focus","epoch":18446744073709551615,"surface":{"kind":"none"},"text_input":{"active":false,"purpose":"normal"},"secure_entry":false})" );
}

// ---------------------------------------------------------------- framing

TEST_CASE( "framing: big-endian u32 length prefix and MaxFrame bound" )
{
	std::string out;
	REQUIRE( append_frame( out, "abc" ) );
	REQUIRE( out == std::string( "\0\0\0\3abc", 7 ) );
	std::string big( MaxFrame, 'x' );
	REQUIRE( append_frame( out, big ) );
	REQUIRE( !append_frame( out, big + "y" ) );

	// Split delivery: the reader reassembles across arbitrary chunk boundaries.
	FrameReader reader;
	std::string body;
	for ( char c : out )
	{
		reader.feed( &c, 1 );
		auto r = reader.next( body );
		if ( r == FrameReader::Result::Frame )
			break;
		REQUIRE( r == FrameReader::Result::NeedMore );
	}
	CHECK( body == "abc" );
	REQUIRE( reader.next( body ) == FrameReader::Result::NeedMore );

	FrameReader huge;
	const char prefix[4] = { 0, 1, 0, 1 }; // 65537 > MaxFrame
	huge.feed( prefix, 4 );
	CHECK( huge.next( body ) == FrameReader::Result::TooLarge );
}

TEST_CASE( "decoder: router frames" )
{
	auto reg = decode_router_message( R"({"version":1,"type":"registered","epoch":42})" );
	REQUIRE( reg );
	CHECK( reg->type == RouterMessage::Type::Registered );
	CHECK( reg->epoch == 42 );

	auto ping = decode_router_message( R"( { "type" : "ping", "seq":7, "epoch":42, "version":1 } )" );
	REQUIRE( ping );
	CHECK( ping->type == RouterMessage::Type::Ping );
	CHECK( ping->seq == 7 );

	auto other = decode_router_message( R"({"version":1,"type":"trusted_connection","epoch":1,"request_id":2,"role":"menu","unit":"x\"y.service","extra":[1,{"a":null}]})" );
	REQUIRE( other );
	CHECK( other->type == RouterMessage::Type::TrustedConnection );

	CHECK( !decode_router_message( R"({"version":2,"type":"registered","epoch":1})" ) );
	CHECK( !decode_router_message( R"({"type":"registered","epoch":1})" ) );
	CHECK( !decode_router_message( R"({"version":1,"type":"registered"})" ) );
	CHECK( !decode_router_message( R"({"version":1,"type":"registered","epoch":-1})" ) );
	CHECK( !decode_router_message( R"({"version":1,"type":"registered","epoch":1)" ) );
	CHECK( !decode_router_message( "not json" ) );
}

// ---------------------------------------------------------------- classifier

TEST_CASE( "classifier: app_id_from_unit matches the shared runtime table" )
{
	std::istringstream table( read_golden( "app_id_from_unit.tsv" ) );
	std::string line;
	int pos = 0, neg = 0;
	while ( std::getline( table, line ) )
	{
		if ( line.empty() || line[0] == '#' )
			continue;
		auto tab = line.find( '\t' );
		REQUIRE( tab != std::string::npos );
		std::string leaf = line.substr( 0, tab ), want = line.substr( tab + 1 );
		auto got = app_id_from_unit( leaf );
		INFO( "leaf '" << leaf << "'" );
		if ( want == "-" )
		{
			CHECK( !got );
			neg++;
		}
		else
		{
			REQUIRE( got );
			CHECK( *got == want );
			pos++;
		}
	}
	CHECK( pos >= 3 );
	CHECK( neg >= 5 );
}

TEST_CASE( "classifier: cgroup leaf pf-app@<id>.service -> App, everything else -> none" )
{
	const std::string slice = "0::/system.slice/system-pf\\x2dapp.slice/";
	ClientFacts app;
	app.has_surface = true;
	app.cgroup = slice + "pf-app@org.example.game.service\n";
	CHECK( classify( app ) == FocusedSurface::app( "org.example.game" ) );

	// Each line of the shared table as a cgroup leaf below the app slice.
	std::istringstream table( read_golden( "app_id_from_unit.tsv" ) );
	std::string line;
	while ( std::getline( table, line ) )
	{
		if ( line.empty() || line[0] == '#' )
			continue;
		auto tab = line.find( '\t' );
		std::string leaf = line.substr( 0, tab ), want = line.substr( tab + 1 );
		ClientFacts f;
		f.has_surface = true;
		f.cgroup = slice + leaf + "\n";
		INFO( "leaf '" << leaf << "'" );
		CHECK( classify( f ) == ( want == "-" ? FocusedSurface::none() : FocusedSurface::app( want ) ) );
	}

	// Xwayland -> none, even when the Xwayland process sits in a pf-app unit.
	ClientFacts xw = app;
	xw.is_xwayland = true;
	CHECK( classify( xw ) == FocusedSurface::none() );

	// Role-less trusted registry members -> none (v1 never emits trusted).
	ClientFacts trusted = app;
	trusted.is_trusted = true;
	CHECK( classify( trusted ) == FocusedSurface::none() );

	// No surface, unpinnable pid, wrong slice, nested below a pf-app unit,
	// non-systemd hierarchy, gamescope's own unit -> none.
	ClientFacts nosurf = app;
	nosurf.has_surface = false;
	CHECK( classify( nosurf ) == FocusedSurface::none() );
	ClientFacts nocg = app;
	nocg.cgroup.reset();
	CHECK( classify( nocg ) == FocusedSurface::none() );
	for ( const char *doc : {
			"0::/system.slice/pf-app@org.example.game.service\n",
			"0::/system.slice/system-pf\\x2dapp.slice/pf-app@a.service/pf-app@b.service\n",
			"0::/system.slice/system-pf\\x2dapp.slice/pf-app@a.service/sub.service\n",
			"1:cpu:/system.slice/system-pf\\x2dapp.slice/pf-app@a.service\n",
			"0::/system.slice/pocketforge-gamescope.service\n",
			"",
		} )
	{
		ClientFacts f = app;
		f.cgroup = doc;
		INFO( "cgroup '" << doc << "'" );
		CHECK( classify( f ) == FocusedSurface::none() );
	}

	// cgroup v1 name=systemd hierarchy is accepted, as in the Rust rule.
	ClientFacts v1 = app;
	v1.cgroup = "12:cpu:/x\n1:name=systemd:/system.slice/system-pf\\x2dapp.slice/pf-app@steamlink.service\n";
	CHECK( classify( v1 ) == FocusedSurface::app( "steamlink" ) );
	CHECK( service_unit_from_cgroup( "0::/system.slice/foo.service" ) == std::optional<std::string>( "foo.service" ) );
}

// ---------------------------------------------------------------- fake router

namespace
{

struct FakeRouter
{
	std::string dir;
	std::string path;
	int listen_fd = -1;
	int conn = -1;

	FakeRouter()
	{
		char tmpl[] = "/tmp/pf-focus-client-XXXXXX";
		REQUIRE( mkdtemp( tmpl ) );
		dir = tmpl;
		path = dir + "/compositor.sock";
	}
	~FakeRouter()
	{
		close_conn();
		stop();
		rmdir( dir.c_str() );
	}
	void listen()
	{
		listen_fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
		REQUIRE( listen_fd >= 0 );
		sockaddr_un addr{};
		addr.sun_family = AF_UNIX;
		std::strncpy( addr.sun_path, path.c_str(), sizeof( addr.sun_path ) - 1 );
		REQUIRE( bind( listen_fd, (sockaddr *)&addr, sizeof( addr ) ) == 0 );
		REQUIRE( ::listen( listen_fd, 4 ) == 0 );
	}
	void stop()
	{
		if ( listen_fd >= 0 )
			close( listen_fd );
		listen_fd = -1;
		unlink( path.c_str() );
	}
	void accept()
	{
		conn = ::accept4( listen_fd, nullptr, nullptr, SOCK_CLOEXEC );
		REQUIRE( conn >= 0 );
	}
	void close_conn()
	{
		if ( conn >= 0 )
			close( conn );
		conn = -1;
	}
	void send_raw( const std::string &bytes )
	{
		REQUIRE( ::send( conn, bytes.data(), bytes.size(), MSG_NOSIGNAL ) == (ssize_t)bytes.size() );
	}
	void send( const std::string &body )
	{
		std::string out;
		REQUIRE( append_frame( out, body ) );
		send_raw( out );
	}
	// Next frame body from the client; "" if none arrives within 200 ms.
	std::string recv()
	{
		std::string body;
		for ( ;; )
		{
			if ( reader.next( body ) == FrameReader::Result::Frame )
				return body;
			pollfd p{ conn, POLLIN, 0 };
			if ( poll( &p, 1, 200 ) <= 0 )
				return "";
			char buf[4096];
			ssize_t n = ::recv( conn, buf, sizeof( buf ), 0 );
			if ( n <= 0 )
				return "";
			reader.feed( buf, n );
		}
	}
	bool peer_closed()
	{
		pollfd p{ conn, POLLIN, 0 };
		if ( poll( &p, 1, 200 ) <= 0 )
			return false;
		char c;
		return ::recv( conn, &c, 1, MSG_DONTWAIT ) == 0;
	}
	FrameReader reader;
};

struct Harness
{
	std::vector<std::chrono::milliseconds> scheduled;
	std::vector<std::string> logs;
	int watched_fd = -1;
	Client client;

	explicit Harness( const std::string &path )
		: client( path, Client::Hooks{
			[this]( int fd, bool ) { watched_fd = fd; },
			[this]( std::chrono::milliseconds d ) { scheduled.push_back( d ); },
			[this]( const std::string &m ) { logs.push_back( m ); },
		} )
	{
	}
	// What the wlserver event loop does when the fd is readable.
	void pump()
	{
		REQUIRE( client.fd() >= 0 );
		pollfd p{ client.fd(), POLLIN, 0 };
		REQUIRE( poll( &p, 1, 1000 ) == 1 );
		client.dispatch( true, false );
	}
};

} // namespace

TEST_CASE( "fake router: register, state_snapshot, focus, EOF and reconnect" )
{
	FakeRouter router;
	Harness h( router.path );

	// Broker absent: back off 100 ms doubling to 2 s, keep going, log once.
	for ( int i = 0; i < 7; i++ )
		h.client.reconnect();
	REQUIRE( h.client.state() == Client::State::Disconnected );
	CHECK( h.scheduled == std::vector<std::chrono::milliseconds>{ 100ms, 200ms, 400ms, 800ms, 1600ms, 2000ms, 2000ms } );
	CHECK( h.logs.size() == 1 );
	CHECK( h.watched_fd == -1 );

	// Focus changes while disconnected are only recorded.
	h.client.set_focus( FocusState{} );

	// Broker appears: register is the first frame, byte-identical to golden.
	router.listen();
	h.client.reconnect();
	REQUIRE( h.client.state() == Client::State::AwaitRegistered );
	CHECK( h.watched_fd == h.client.fd() );
	router.accept();
	CHECK( router.recv() == read_golden( "register.json" ) );

	// No delta before the snapshot.
	h.client.set_focus( app_focus( "org.example.game" ) );
	h.client.set_focus( FocusState{} );
	CHECK( router.recv() == "" );

	// registered{epoch} -> exactly one full state_snapshot, byte-identical to golden.
	router.send( R"({"version":1,"type":"registered","epoch":1})" );
	h.pump();
	REQUIRE( h.client.state() == Client::State::Live );
	CHECK( h.client.epoch() == 1 );
	CHECK( router.recv() == read_golden( "state_snapshot.json" ) );
	CHECK( router.recv() == "" );

	// Focus delta to an App, byte-identical to golden.
	h.client.set_focus( app_focus( "org.example.game" ) );
	CHECK( router.recv() == read_golden( "focus.json" ) );
	// Unchanged triple: nothing.
	h.client.set_focus( app_focus( "org.example.game" ) );
	CHECK( router.recv() == "" );

	// ping -> pong; unknown/trusted frames are dropped without disconnecting.
	router.send( R"({"version":1,"type":"ping","epoch":1,"seq":5})" );
	router.send( R"({"version":1,"type":"trusted_connection","epoch":1,"request_id":1,"role":"menu","unit":"pf-system-menu.service"})" );
	router.send( "garbage" );
	router.send( R"({"version":1,"type":"ping","epoch":99,"seq":6})" );
	h.pump();
	CHECK( router.recv() == R"({"version":1,"type":"pong","epoch":1,"seq":5})" );
	CHECK( router.recv() == "" );
	REQUIRE( h.client.state() == Client::State::Live );

	// Surface destroy path: focus none.
	h.client.set_focus( FocusState{} );
	CHECK( router.recv() == encode_focus( 1, FocusState{} ) );
	h.client.set_focus( app_focus( "org.example.game" ) );
	CHECK( router.recv() == read_golden( "focus.json" ) );

	// Router EOF (broker restart): drop the epoch, reconnect after 100 ms
	// (backoff was reset by the successful registration).
	h.scheduled.clear();
	router.close_conn();
	h.pump();
	REQUIRE( h.client.state() == Client::State::Disconnected );
	CHECK( h.client.epoch() == 0 );
	CHECK( h.watched_fd == -1 );
	CHECK( h.scheduled == std::vector<std::chrono::milliseconds>{ 100ms } );

	// Focus moves while disconnected; nothing is queued for replay.
	h.client.set_focus( app_focus( "steamlink" ) );

	// reconnect: register again, new epoch, fresh full snapshot with the
	// current focus. The old-epoch delta is never replayed.
	h.client.reconnect();
	router.accept();
	CHECK( router.recv() == read_golden( "register.json" ) );
	router.send( R"({"version":1,"type":"registered","epoch":77})" );
	h.pump();
	CHECK( h.client.epoch() == 77 );
	FocusState steamlink = app_focus( "steamlink" );
	CHECK( router.recv() == encode_state_snapshot( 77, steamlink ) );
	CHECK( encode_state_snapshot( 77, steamlink ) ==
		R"({"version":1,"type":"state_snapshot","epoch":77,"focus":{"surface":{"kind":"app","app_id":"steamlink"},"text_input":{"active":false,"purpose":"normal"},"secure_entry":false},"overlays":[{"role":"keyboard","state":"unmapped"},{"role":"menu","state":"unmapped"},{"role":"toast","state":"unmapped"}],"power":{"pressed":false,"last_sequence":0}})" );
	CHECK( router.recv() == "" );
	h.client.set_focus( FocusState{} );
	CHECK( router.recv() == encode_focus( 77, FocusState{} ) );
}

TEST_CASE( "fake router: protocol violations disconnect and reconnect cleanly" )
{
	FakeRouter router;
	router.listen();

	SECTION( "registered epoch 0" )
	{
		Harness h( router.path );
		h.client.reconnect();
		router.accept();
		CHECK( router.recv() == read_golden( "register.json" ) );
		router.send( R"({"version":1,"type":"registered","epoch":0})" );
		h.pump();
		CHECK( h.client.state() == Client::State::Disconnected );
		CHECK( router.peer_closed() );
	}
	SECTION( "second registered on one connection" )
	{
		Harness h( router.path );
		h.client.reconnect();
		router.accept();
		router.recv();
		router.send( R"({"version":1,"type":"registered","epoch":3})" );
		h.pump();
		CHECK( router.recv() == encode_state_snapshot( 3, FocusState{} ) );
		router.send( R"({"version":1,"type":"registered","epoch":4})" );
		h.pump();
		CHECK( h.client.state() == Client::State::Disconnected );
		CHECK( h.client.epoch() == 0 );
	}
	SECTION( "oversized frame length" )
	{
		Harness h( router.path );
		h.client.reconnect();
		router.accept();
		router.recv();
		router.send_raw( std::string( "\x00\x01\x00\x01", 4 ) );
		h.pump();
		CHECK( h.client.state() == Client::State::Disconnected );
		CHECK( h.scheduled.back() == 100ms );
	}
	SECTION( "ping before registered is ignored" )
	{
		Harness h( router.path );
		h.client.reconnect();
		router.accept();
		router.recv();
		router.send( R"({"version":1,"type":"ping","epoch":1,"seq":1})" );
		h.pump();
		CHECK( h.client.state() == Client::State::AwaitRegistered );
		CHECK( router.recv() == "" );
	}
}
