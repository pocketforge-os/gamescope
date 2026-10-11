#include "input_protocols.hpp"

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>

#include <cstdio>
#include <map>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

namespace
{

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
	std::map<std::string, uint32_t> names;
	std::map<std::string, uint32_t> versions;
};

void registry_global( void *data, wl_registry *, uint32_t name,
	const char *interface, uint32_t version )
{
	RegistryView *view = static_cast<RegistryView *>( data );
	view->names[interface] = name;
	view->versions[interface] = version;
}

void registry_global_remove( void *, wl_registry *, uint32_t )
{
}

constexpr wl_registry_listener kRegistryListener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

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

// Client-side interface descriptors for binding. The proxy never dispatches
// requests or events in this test, so empty method/event tables are enough.
constexpr wl_interface kTextInputManagerInterface = {
	.name = "zwp_text_input_manager_v3",
	.version = 2,
	.method_count = 0,
	.methods = nullptr,
	.event_count = 0,
	.events = nullptr,
};

constexpr wl_interface kCursorShapeManagerInterface = {
	.name = "wp_cursor_shape_manager_v1",
	.version = 2,
	.method_count = 0,
	.methods = nullptr,
	.event_count = 0,
	.events = nullptr,
};

} // namespace

int main()
{
	TestState state;

	wl_display *display = wl_display_create();
	state.check( display != nullptr, "create headless Wayland display" );
	if ( !display )
		return 1;

	state.check( gamescope::input_protocols::create_globals( display ),
		"create text-input-v3 and cursor-shape-v1 globals" );

	ClientPair client = create_client( display );
	state.check( client.client != nullptr, "create test client" );
	if ( !client.client )
	{
		wl_display_destroy( display );
		return 1;
	}

	std::thread server_thread( [display]() { wl_display_run( display ); } );

	RegistryView view;
	wl_registry *registry = wl_display_get_registry( client.client );
	wl_registry_add_listener( registry, &kRegistryListener, &view );
	state.check( wl_display_roundtrip( client.client ) >= 0, "registry roundtrip" );

	state.check( view.versions.contains( "zwp_text_input_manager_v3" ) &&
		view.versions["zwp_text_input_manager_v3"] == 2,
		"zwp_text_input_manager_v3 is advertised at version 2" );
	state.check( view.versions.contains( "wp_cursor_shape_manager_v1" ) &&
		view.versions["wp_cursor_shape_manager_v1"] == 2,
		"wp_cursor_shape_manager_v1 is advertised at version 2" );

	// Positive control: both managers bind at version 2 without errors.
	wl_registry_bind( registry, view.names["zwp_text_input_manager_v3"],
		&kTextInputManagerInterface, 2 );
	wl_registry_bind( registry, view.names["wp_cursor_shape_manager_v1"],
		&kCursorShapeManagerInterface, 2 );
	state.check( wl_display_roundtrip( client.client ) >= 0,
		"version 2 binds roundtrip" );
	state.check( wl_display_get_error( client.client ) == 0,
		"version 2 binds produce no protocol error" );

	// Negative control: a version 1 client still binds successfully, so the
	// bump does not break existing clients.
	wl_registry_bind( registry, view.names["wp_cursor_shape_manager_v1"],
		&kCursorShapeManagerInterface, 1 );
	state.check( wl_display_roundtrip( client.client ) >= 0,
		"version 1 cursor-shape bind roundtrips" );
	state.check( wl_display_get_error( client.client ) == 0,
		"negative control: version 1 bind stays error-free" );

	// Negative control: binding beyond the advertised version is rejected by
	// the compositor with a registry invalid_version error.
	ClientPair second = create_client( display );
	state.check( second.client != nullptr, "create second test client" );
	if ( second.client )
	{
		RegistryView second_view;
		wl_registry *second_registry = wl_display_get_registry( second.client );
		wl_registry_add_listener( second_registry, &kRegistryListener, &second_view );
		state.check( wl_display_roundtrip( second.client ) >= 0,
			"second client registry roundtrip" );
		wl_registry_bind( second_registry,
			second_view.names["zwp_text_input_manager_v3"],
			&kTextInputManagerInterface, 3 );
		wl_display_roundtrip( second.client );
		state.check( wl_display_get_error( second.client ) != 0,
			"negative control: version 3 bind is rejected with invalid_version" );
		wl_registry_destroy( second_registry );
		wl_display_disconnect( second.client );
	}

	wl_registry_destroy( registry );
	wl_display_disconnect( client.client );
	wl_display_terminate( display );
	server_thread.join();
	wl_display_destroy_clients( display );
	wl_display_destroy( display );

	if ( state.failures != 0 )
		std::fprintf( stderr, "input-protocol-globals failures=%d\n", state.failures );
	return state.failures == 0 ? 0 : 1;
}
