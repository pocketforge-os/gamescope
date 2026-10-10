#include "wayland_security.hpp"
#include "x11_control_policy.hpp"
#include "xwayland_security.hpp"

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace
{

constexpr std::array<std::string_view, 7> kPrivilegedGlobals = {
	"zwlr_layer_shell_v1",
	"gamescope_input_method_manager",
	"gamescope_control",
	"gamescope_pipewire",
	"gamescope_private",
	"zwp_input_method_manager_v2",
	"zwp_virtual_keyboard_manager_v1",
};

struct TestState
{
	int failures = 0;

	void check( bool condition, const char *message )
	{
		if ( condition )
			return;
		std::fprintf( stderr, "FAIL: %s\n", message );
		failures++;
	}
};

struct RegistryView
{
	std::map<std::string, uint32_t> globals;
};

void registry_global( void *data, wl_registry *, uint32_t name,
	const char *interface, uint32_t )
{
	static_cast<RegistryView *>( data )->globals[interface] = name;
}

void registry_global_remove( void *, wl_registry *, uint32_t )
{
}

constexpr wl_registry_listener kRegistryListener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

std::atomic<int> g_bind_count = 0;

void bind_global( wl_client *client, void *data, uint32_t version, uint32_t id )
{
	const wl_interface *interface = static_cast<const wl_interface *>( data );
	wl_resource *resource = wl_resource_create( client, interface, version, id );
	if ( resource )
	{
		wl_resource_set_implementation( resource, nullptr, nullptr, nullptr );
		g_bind_count.fetch_add( 1 );
	}
}

struct FilterContext
{
	gamescope::wayland_security::TrustedClientRegistry *trusted_clients = nullptr;
};

bool filter_global( const wl_client *client, const wl_global *global, void *data )
{
	const wl_interface *interface = wl_global_get_interface( global );
	if ( !gamescope::wayland_security::is_privileged_global( interface->name ) )
		return true;
	return static_cast<FilterContext *>( data )->trusted_clients->contains( client );
}

struct ClientPair
{
	wl_client *server = nullptr;
	wl_display *client = nullptr;
};

ClientPair create_client( wl_display *display )
{
	int sockets[2] = { -1, -1 };
	if ( socketpair( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets ) != 0 )
		return {};

	ClientPair pair;
	pair.server = wl_client_create( display, sockets[0] );
	if ( !pair.server )
	{
		close( sockets[0] );
		close( sockets[1] );
		return {};
	}

	pair.client = wl_display_connect_to_fd( sockets[1] );
	if ( !pair.client )
	{
		wl_client_destroy( pair.server );
		close( sockets[1] );
		return {};
	}
	return pair;
}

void test_global_policy( TestState &state )
{
	wl_display *display = wl_display_create();
	state.check( display != nullptr, "create headless Wayland display" );
	if ( !display )
		return;

	gamescope::wayland_security::TrustedClientRegistry trusted_clients;
	auto broker = gamescope::wayland_security::TrustedClientBroker::create(
		display, trusted_clients );
	state.check( broker != nullptr, "create trusted-client capability broker" );
	if ( !broker )
	{
		wl_display_destroy( display );
		return;
	}
	const int launcher_fd = broker->take_launcher_fd();
	state.check( launcher_fd >= 0, "take trusted-launcher capability" );

	ClientPair untrusted = create_client( display );
	state.check( untrusted.client != nullptr, "create public client" );
	if ( !untrusted.client )
	{
		close( launcher_fd );
		broker.reset();
		wl_display_destroy( display );
		return;
	}

	FilterContext filter_context = { .trusted_clients = &trusted_clients };
	wl_display_set_global_filter( display, filter_global, &filter_context );

	std::array<wl_interface, kPrivilegedGlobals.size()> interfaces = {};
	std::array<wl_global *, kPrivilegedGlobals.size()> globals = {};
	for ( size_t i = 0; i < kPrivilegedGlobals.size(); i++ )
	{
		interfaces[i] = {
			.name = kPrivilegedGlobals[i].data(),
			.version = 1,
			.method_count = 0,
			.methods = nullptr,
			.event_count = 0,
			.events = nullptr,
		};
		globals[i] = wl_global_create( display, &interfaces[i], 1, &interfaces[i], bind_global );
		state.check( globals[i] != nullptr, "create privileged test global" );
	}

	static const wl_interface public_interface = {
		.name = "test_public_global",
		.version = 1,
		.method_count = 0,
		.methods = nullptr,
		.event_count = 0,
		.events = nullptr,
	};
	wl_global *public_global = wl_global_create( display, &public_interface, 1,
		const_cast<wl_interface *>( &public_interface ), bind_global );
	state.check( public_global != nullptr, "create public control global" );

	std::thread server_thread( [display]() { wl_display_run( display ); } );
	const int trusted_fd = gamescope::wayland_security::request_trusted_client_fd( launcher_fd );
	state.check( trusted_fd >= 0, "broker mints a fresh trusted connection" );
	wl_display *trusted_display = trusted_fd >= 0 ? wl_display_connect_to_fd( trusted_fd ) : nullptr;
	state.check( trusted_display != nullptr, "connect trusted client over minted fd" );
	if ( !trusted_display )
	{
		close( launcher_fd );
		wl_display_disconnect( untrusted.client );
		wl_display_terminate( display );
		server_thread.join();
		broker.reset();
		wl_display_destroy( display );
		return;
	}
	close( launcher_fd );

	RegistryView trusted_view;
	RegistryView untrusted_view;
	wl_registry *trusted_registry = wl_display_get_registry( trusted_display );
	wl_registry *untrusted_registry = wl_display_get_registry( untrusted.client );
	wl_registry_add_listener( trusted_registry, &kRegistryListener, &trusted_view );
	wl_registry_add_listener( untrusted_registry, &kRegistryListener, &untrusted_view );
	state.check( wl_display_roundtrip( trusted_display ) >= 0, "trusted registry roundtrip" );
	state.check( wl_display_roundtrip( untrusted.client ) >= 0, "public registry roundtrip" );

	for ( std::string_view name : kPrivilegedGlobals )
	{
		state.check( trusted_view.globals.contains( std::string( name ) ),
			"trusted client sees every privileged global" );
		state.check( !untrusted_view.globals.contains( std::string( name ) ),
			"public client cannot see any privileged global" );
	}
	state.check( trusted_view.globals.contains( "test_public_global" ) &&
		untrusted_view.globals.contains( "test_public_global" ),
		"positive control: both clients see a public global" );

	const uint32_t control_name = trusted_view.globals["gamescope_control"];
	g_bind_count.store( 0 );
	wl_registry_bind( trusted_registry, control_name, &interfaces[2], 1 );
	state.check( wl_display_roundtrip( trusted_display ) >= 0,
		"trusted client can bind a privileged global" );
	state.check( g_bind_count.load() == 1,
		"positive control: trusted bind reaches the handler" );

	// Global names are not secrets. A public client that guesses the trusted
	// registry name must be disconnected by libwayland's filtered bind path.
	wl_registry_bind( untrusted_registry, control_name, &interfaces[2], 1 );
	state.check( wl_display_roundtrip( untrusted.client ) < 0,
		"public direct bind to a hidden privileged global fails" );
	state.check( g_bind_count.load() == 1,
		"negative control: public direct bind never reaches the handler" );

	wl_registry_destroy( trusted_registry );
	wl_display_disconnect( trusted_display );
	wl_registry_destroy( untrusted_registry );
	wl_display_disconnect( untrusted.client );
	wl_display_terminate( display );
	server_thread.join();
	broker.reset();
	wl_display_destroy_clients( display );
	wl_display_destroy( display );

	state.check( gamescope::wayland_security::request_trusted_client_fd( -1 ) < 0,
		"negative control: invalid broker capability fails closed" );
}

size_t count_open_fds()
{
	return static_cast<size_t>( std::distance(
		std::filesystem::directory_iterator( "/proc/self/fd" ),
		std::filesystem::directory_iterator() ) );
}

void test_malformed_broker_response( TestState &state )
{
	int broker[2] = { -1, -1 };
	int payload[2] = { -1, -1 };
	state.check( socketpair( AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, broker ) == 0,
		"create malformed broker fixture" );
	state.check( pipe2( payload, O_CLOEXEC ) == 0,
		"create malformed broker fd payload" );
	if ( broker[0] < 0 || payload[0] < 0 )
	{
		for ( int fd : broker )
			if ( fd >= 0 )
				close( fd );
		for ( int fd : payload )
			if ( fd >= 0 )
				close( fd );
		return;
	}

	const size_t before = count_open_fds();
	std::thread server( [peer = broker[1], sent_fd = payload[0]]()
	{
		char request = 0;
		if ( recv( peer, &request, sizeof( request ), 0 ) != sizeof( request ) )
			return;

		char invalid_response = '!';
		iovec iov = { .iov_base = &invalid_response, .iov_len = 1 };
		std::array<char, CMSG_SPACE( sizeof( int ) )> control = {};
		msghdr message = {};
		message.msg_iov = &iov;
		message.msg_iovlen = 1;
		message.msg_control = control.data();
		message.msg_controllen = control.size();
		cmsghdr *header = CMSG_FIRSTHDR( &message );
		header->cmsg_level = SOL_SOCKET;
		header->cmsg_type = SCM_RIGHTS;
		header->cmsg_len = CMSG_LEN( sizeof( int ) );
		std::memcpy( CMSG_DATA( header ), &sent_fd, sizeof( sent_fd ) );
		sendmsg( peer, &message, MSG_NOSIGNAL );
	} );

	state.check( gamescope::wayland_security::request_trusted_client_fd( broker[0] ) < 0,
		"malformed broker response fails closed" );
	server.join();
	state.check( count_open_fds() == before,
		"negative control: malformed SCM_RIGHTS response does not leak its received fd" );

	close( broker[0] );
	close( broker[1] );
	close( payload[0] );
	close( payload[1] );
}

struct Listener
{
	int fd = -1;
	sockaddr_un address = {};
	socklen_t length = 0;
};

Listener create_listener( std::string_view name, bool abstract )
{
	Listener listener;
	listener.fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
	if ( listener.fd < 0 )
		return listener;

	listener.address.sun_family = AF_UNIX;
	const size_t offset = abstract ? 1 : 0;
	if ( name.size() + offset >= sizeof( listener.address.sun_path ) )
		return listener;
	std::memcpy( listener.address.sun_path + offset, name.data(), name.size() );
	listener.length = static_cast<socklen_t>( offsetof( sockaddr_un, sun_path ) +
		offset + name.size() + ( abstract ? 0 : 1 ) );
	if ( bind( listener.fd, reinterpret_cast<sockaddr *>( &listener.address ), listener.length ) != 0 ||
		listen( listener.fd, 4 ) != 0 )
	{
		close( listener.fd );
		listener.fd = -1;
	}
	return listener;
}

bool connect_once( const Listener &listener )
{
	int client = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
	if ( client < 0 )
		return false;
	const bool connected = connect( client,
		reinterpret_cast<const sockaddr *>( &listener.address ), listener.length ) == 0;
	if ( connected )
	{
		int accepted = accept4( listener.fd, nullptr, nullptr, SOCK_CLOEXEC );
		if ( accepted >= 0 )
			close( accepted );
	}
	close( client );
	return connected;
}

void test_xwayland_listeners( TestState &state )
{
	char directory_template[] = "/tmp/gamescope-xwayland-security.XXXXXX";
	char *directory = mkdtemp( directory_template );
	state.check( directory != nullptr, "create Xwayland socket fixture directory" );
	if ( !directory )
		return;

	const std::string abstract_name = "/tmp/.X11-unix/X-pf-" + std::to_string( getpid() );
	const std::string filesystem_name = std::string( directory ) + "/X0";
	Listener abstract_listener = create_listener( abstract_name, true );
	Listener filesystem_listener = create_listener( filesystem_name, false );
	state.check( abstract_listener.fd >= 0 && filesystem_listener.fd >= 0,
		"create abstract and filesystem X11 listeners" );
	if ( abstract_listener.fd < 0 || filesystem_listener.fd < 0 )
		return;

	state.check( connect_once( abstract_listener ),
		"RED control: Linux abstract X11 listener is reachable" );
	state.check( connect_once( filesystem_listener ),
		"positive control: filesystem X11 listener is reachable" );

	std::array<int, 2> listener_fds = { abstract_listener.fd, filesystem_listener.fd };
	state.check( gamescope::xwayland_security::harden_listeners( listener_fds ),
		"harden a valid wlroots listener pair" );
	state.check( !connect_once( abstract_listener ),
		"abstract X11 address is unreachable after hardening" );
	state.check( connect_once( filesystem_listener ),
		"positive control: filesystem X11 listener survives hardening" );

	int pipe_fds[2] = { -1, -1 };
	state.check( pipe2( pipe_fds, O_CLOEXEC ) == 0, "create malformed-listener control" );
	std::array<int, 2> invalid_fds = { pipe_fds[0], filesystem_listener.fd };
	state.check( !gamescope::xwayland_security::harden_listeners( invalid_fds ),
		"fail closed when an inherited listener is not AF_UNIX" );
	state.check( connect_once( filesystem_listener ),
		"negative control failure does not damage the filesystem listener" );

	close( pipe_fds[0] );
	close( pipe_fds[1] );
	close( abstract_listener.fd );
	close( filesystem_listener.fd );
	unlink( filesystem_name.c_str() );
	rmdir( directory );
}

void test_x11_control_atoms( TestState &state )
{
	for ( std::string_view atom : gamescope::x11_control_policy::kLegacyControlAtoms )
	{
		state.check( !gamescope::x11_control_policy::dispatches_effect( atom ),
			"untrusted X11 property set has no control effect by default" );
		state.check( gamescope::x11_control_policy::dispatches_effect( atom, true ),
			"positive control: explicit legacy mode dispatches the control effect" );
	}
	state.check( !gamescope::x11_control_policy::dispatches_effect( "TEST_UNRELATED_PROPERTY", true ),
		"negative control: an unrelated property is not a legacy control atom" );
}

} // namespace

int main()
{
	TestState state;
	test_global_policy( state );
	test_malformed_broker_response( state );
	test_xwayland_listeners( state );
	test_x11_control_atoms( state );
	if ( state.failures != 0 )
		std::fprintf( stderr, "privileged-wayland failures=%d\n", state.failures );
	return state.failures == 0 ? 0 : 1;
}
