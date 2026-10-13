#include "runtime_orientation.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <wayland-client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace gamescope::runtime_orientation;

namespace
{

struct Scratch
{
	std::string directory;
	std::string socket;
	std::string xwaylandWrapper;

	Scratch()
	{
		std::array<char, 48> pattern = {};
		std::snprintf( pattern.data(), pattern.size(), "/tmp/gs-xrandr-%d-XXXXXX",
			int( getpid() ) );
		char *created = mkdtemp( pattern.data() );
		if ( !created ) return;
		directory = created;
		socket = directory + "/authority.sock";
		xwaylandWrapper = directory + "/Xwayland";
		const int wrapper = open( xwaylandWrapper.c_str(), O_WRONLY | O_CREAT | O_EXCL,
			0700 );
		constexpr std::string_view contents =
			"#!/bin/sh\nexec /usr/bin/Xwayland \"$@\" -ac\n";
		if ( wrapper < 0 || write( wrapper, contents.data(), contents.size() ) !=
			ssize_t( contents.size() ) )
		{
			if ( wrapper >= 0 ) close( wrapper );
			xwaylandWrapper.clear();
			return;
		}
		close( wrapper );
	}

	~Scratch()
	{
		if ( !socket.empty() ) unlink( socket.c_str() );
		if ( !xwaylandWrapper.empty() ) unlink( xwaylandWrapper.c_str() );
		if ( !directory.empty() ) rmdir( directory.c_str() );
	}
};

std::string logicalFor( std::string_view token )
{
	if ( token == "right" ) return "portrait_flipped";
	if ( token == "upsidedown" ) return "landscape_flipped";
	if ( token == "left" ) return "portrait";
	return "landscape";
}

std::string response( std::string_view output, uint64_t serial )
{
	const std::string logical = logicalFor( output );
	return "{\"result\":\"orientation\",\"version\":1,\"state\":{"
		"\"serial\":" + std::to_string( serial ) +
		",\"output\":\"" + std::string( output ) +
		"\",\"logical\":\"" + logical +
		"\",\"app\":\"" + logical +
		"\",\"presentation\":\"fill\",\"whole_body\":\"normal\","
		"\"screen_only\":\"normal\",\"locked\":false,\"degraded\":[]}}";
}

bool writeAll( int fd, const std::vector<uint8_t> &bytes )
{
	size_t offset = 0;
	while ( offset < bytes.size() )
	{
		const ssize_t size = send( fd, bytes.data() + offset, bytes.size() - offset,
			MSG_NOSIGNAL );
		if ( size < 0 && errno == EINTR ) continue;
		if ( size <= 0 ) return false;
		offset += size_t( size );
	}
	return true;
}

class Authority
{
public:
	explicit Authority( const std::string &path )
	{
		m_fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
		if ( m_fd < 0 ) return;
		sockaddr_un address = { .sun_family = AF_UNIX };
		std::snprintf( address.sun_path, sizeof( address.sun_path ), "%s", path.c_str() );
		if ( bind( m_fd, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) < 0 ||
			listen( m_fd, 8 ) < 0 )
		{
			close( m_fd );
			m_fd = -1;
			return;
		}
		m_thread = std::thread( &Authority::run, this );
	}

	~Authority()
	{
		m_stop.store( true );
		m_changed.notify_all();
		if ( m_fd >= 0 ) shutdown( m_fd, SHUT_RDWR );
		if ( m_thread.joinable() ) m_thread.join();
		if ( m_fd >= 0 ) close( m_fd );
	}

	bool valid() const { return m_fd >= 0; }

	void publish( std::string token )
	{
		std::lock_guard lock( m_mutex );
		m_token = std::move( token );
		m_serial++;
		m_changed.notify_all();
	}

private:
	void run()
	{
		while ( !m_stop.load() )
		{
			const int client = accept4( m_fd, nullptr, nullptr, SOCK_CLOEXEC );
			if ( client < 0 )
			{
				if ( errno == EINTR ) continue;
				return;
			}
			serve( client );
			close( client );
		}
	}

	void serve( int client )
	{
		FrameDecoder decoder;
		std::array<uint8_t, 256> bytes;
		while ( !m_stop.load() )
		{
			const ssize_t size = recv( client, bytes.data(), bytes.size(), 0 );
			if ( size <= 0 ) return;
			const FrameStatus status = decoder.feed( { bytes.data(), size_t( size ) } );
			if ( status == FrameStatus::Invalid ) return;
			if ( status == FrameStatus::Complete ) break;
		}
		std::optional<uint64_t> after;
		const std::string_view request = decoder.body();
		const std::string_view marker = "\"after_serial\":";
		const size_t position = request.find( marker );
		if ( position != std::string_view::npos )
			after = std::strtoull( request.data() + position + marker.size(), nullptr, 10 );

		std::unique_lock lock( m_mutex );
		m_changed.wait_for( lock, std::chrono::seconds{ 5 }, [&] {
			return m_stop.load() || !after || *after != m_serial;
		} );
		if ( m_stop.load() ) return;
		const auto frame = encodeFrame( response( m_token, m_serial ) );
		lock.unlock();
		writeAll( client, frame );
	}

	int m_fd = -1;
	std::thread m_thread;
	std::atomic<bool> m_stop = false;
	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::string m_token = "normal";
	uint64_t m_serial = 1;
};

struct Child
{
	pid_t pid = -1;
	~Child()
	{
		if ( pid <= 0 ) return;
		kill( pid, SIGTERM );
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 3 };
		while ( std::chrono::steady_clock::now() < deadline )
		{
			if ( waitpid( pid, nullptr, WNOHANG ) == pid ) return;
			std::this_thread::sleep_for( std::chrono::milliseconds{ 10 } );
		}
		kill( pid, SIGKILL );
		waitpid( pid, nullptr, 0 );
	}
};

bool readReady( int fd, std::string *display, std::string *wayland )
{
	pollfd ready = { .fd = fd, .events = POLLIN };
	if ( poll( &ready, 1, 15000 ) <= 0 ) return false;
	std::array<char, 128> line = {};
	const ssize_t size = read( fd, line.data(), line.size() - 1 );
	if ( size <= 0 ) return false;
	char *space = std::strchr( line.data(), ' ' );
	if ( !space ) return false;
	*space = '\0';
	*display = line.data();
	char *end = std::strchr( space + 1, '\n' );
	if ( end ) *end = '\0';
	*wayland = space + 1;
	return !display->empty() && !wayland->empty();
}

struct SurfaceEvents
{
	std::vector<uint32_t> transforms;
};

void surfaceEnter( void *, wl_surface *, wl_output * ) {}
void surfaceLeave( void *, wl_surface *, wl_output * ) {}
void surfaceScale( void *, wl_surface *, int32_t ) {}
void surfaceTransform( void *data, wl_surface *, uint32_t transform )
{
	static_cast<SurfaceEvents *>( data )->transforms.push_back( transform );
}

constexpr wl_surface_listener surfaceListener = {
	.enter = surfaceEnter,
	.leave = surfaceLeave,
	.preferred_buffer_scale = surfaceScale,
	.preferred_buffer_transform = surfaceTransform,
};

struct WaylandProbe
{
	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor6 = nullptr;
	wl_compositor *compositor5 = nullptr;
	wl_output *output = nullptr;
	int transform = WL_OUTPUT_TRANSFORM_NORMAL;
	int modeWidth = 0;
	int modeHeight = 0;
	int done = 0;

	~WaylandProbe()
	{
		if ( output ) wl_output_destroy( output );
		if ( compositor5 ) wl_compositor_destroy( compositor5 );
		if ( compositor6 ) wl_compositor_destroy( compositor6 );
		if ( registry ) wl_registry_destroy( registry );
		if ( display ) wl_display_disconnect( display );
	}
};

void outputGeometry( void *data, wl_output *, int32_t, int32_t, int32_t, int32_t,
	int32_t, const char *, const char *, int32_t transform )
{
	static_cast<WaylandProbe *>( data )->transform = transform;
}
void outputMode( void *data, wl_output *, uint32_t flags, int32_t width,
	int32_t height, int32_t )
{
	if ( flags & WL_OUTPUT_MODE_CURRENT )
	{
		auto *probe = static_cast<WaylandProbe *>( data );
		probe->modeWidth = width;
		probe->modeHeight = height;
	}
}
void outputDone( void *data, wl_output * )
{
	static_cast<WaylandProbe *>( data )->done++;
}
void outputScale( void *, wl_output *, int32_t ) {}
void outputName( void *, wl_output *, const char * ) {}
void outputDescription( void *, wl_output *, const char * ) {}

constexpr wl_output_listener outputListener = {
	.geometry = outputGeometry,
	.mode = outputMode,
	.done = outputDone,
	.scale = outputScale,
	.name = outputName,
	.description = outputDescription,
};

void registryGlobal( void *data, wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version )
{
	auto *probe = static_cast<WaylandProbe *>( data );
	if ( std::strcmp( interface, wl_compositor_interface.name ) == 0 )
	{
		probe->compositor6 = static_cast<wl_compositor *>( wl_registry_bind(
			registry, name, &wl_compositor_interface, std::min( version, 6u ) ) );
		probe->compositor5 = static_cast<wl_compositor *>( wl_registry_bind(
			registry, name, &wl_compositor_interface, std::min( version, 5u ) ) );
	}
	else if ( std::strcmp( interface, wl_output_interface.name ) == 0 )
	{
		probe->output = static_cast<wl_output *>( wl_registry_bind(
			registry, name, &wl_output_interface, std::min( version, 4u ) ) );
		wl_output_add_listener( probe->output, &outputListener, probe );
	}
}
void registryRemove( void *, wl_registry *, uint32_t ) {}
constexpr wl_registry_listener registryListener = {
	.global = registryGlobal,
	.global_remove = registryRemove,
};

bool currentMatches( Display *display, int width, int height, Rotation expected )
{
	XRRScreenConfiguration *configuration = XRRGetScreenInfo( display,
		DefaultRootWindow( display ) );
	if ( !configuration ) return false;
	Rotation rotation = RR_Rotate_0;
	XRRConfigCurrentConfiguration( configuration, &rotation );
	XRRFreeScreenConfigInfo( configuration );
	return DisplayWidth( display, DefaultScreen( display ) ) == width &&
		DisplayHeight( display, DefaultScreen( display ) ) == height &&
		rotation == expected;
}

bool waitForChange( Display *display, Authority &authority, std::string token,
	int width, int height, Rotation rotation, int randrEventBase )
{
	authority.publish( std::move( token ) );
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
	bool eventSeen = false;
	while ( std::chrono::steady_clock::now() < deadline )
	{
		while ( XPending( display ) )
		{
			XEvent event;
			XNextEvent( display, &event );
			if ( event.type == randrEventBase + RRScreenChangeNotify )
			{
				XRRUpdateConfiguration( &event );
				eventSeen = true;
			}
		}
		if ( eventSeen && currentMatches( display, width, height, rotation ) ) return true;
		std::this_thread::sleep_for( std::chrono::milliseconds{ 10 } );
	}
	return false;
}

} // namespace

int main( int argc, char **argv )
{
	if ( argc != 3 ) return 2;
	Scratch scratch;
	Authority authority( scratch.socket );
	if ( scratch.directory.empty() || scratch.xwaylandWrapper.empty() ||
		!authority.valid() ) return 2;
	int readyPipe[2] = { -1, -1 };
	if ( pipe2( readyPipe, O_CLOEXEC ) != 0 ) return 2;
	const int inherited = fcntl( readyPipe[1], F_DUPFD, 3 );
	if ( inherited < 0 ) return 2;
	fcntl( inherited, F_SETFD, 0 );

	Child child;
	child.pid = fork();
	if ( child.pid == 0 )
	{
		close( readyPipe[0] );
		close( readyPipe[1] );
		setenv( "XDG_RUNTIME_DIR", scratch.directory.c_str(), 1 );
		setenv( "GAMESCOPE_ORIENTATION_SOCKET", scratch.socket.c_str(), 1 );
		// Keep the fixture on the CI-pinned Xwayland binary while disabling the
		// ambient X11 cookie policy, which is unrelated to RandR behavior.
		setenv( "WLR_XWAYLAND", scratch.xwaylandWrapper.c_str(), 1 );
		const std::string reaperPath = argv[2];
		const size_t separator = reaperPath.find_last_of( '/' );
		if ( separator == std::string::npos ) _exit( 126 );
		const std::string path = reaperPath.substr( 0, separator ) + ":/usr/bin:/bin";
		setenv( "PATH", path.c_str(), 1 );
		const std::string ready = "/proc/self/fd/" + std::to_string( inherited );
		execl( argv[1], argv[1], "--backend", "headless", "-W", "800", "-H", "1280",
			"-R", ready.c_str(), "--keep-alive", "--", "/usr/bin/sleep", "60",
			static_cast<char *>( nullptr ) );
		_exit( 127 );
	}
	close( inherited );
	close( readyPipe[1] );
	if ( child.pid < 0 ) return 2;

	std::string displayName;
	std::string waylandName;
	if ( !readReady( readyPipe[0], &displayName, &waylandName ) )
	{
		std::fprintf( stderr, "gamescope/Xwayland did not become ready\n" );
		return 1;
	}
	close( readyPipe[0] );
	setenv( "XDG_RUNTIME_DIR", scratch.directory.c_str(), 1 );
	WaylandProbe wayland;
	wayland.display = wl_display_connect( waylandName.c_str() );
	if ( !wayland.display ) return 1;
	wayland.registry = wl_display_get_registry( wayland.display );
	wl_registry_add_listener( wayland.registry, &registryListener, &wayland );
	wl_display_roundtrip( wayland.display );
	wl_display_roundtrip( wayland.display );
	if ( !wayland.compositor6 || !wayland.compositor5 || !wayland.output ) return 1;
	SurfaceEvents existingEvents;
	SurfaceEvents oldClientEvents;
	wl_surface *existing = wl_compositor_create_surface( wayland.compositor6 );
	wl_surface *oldClient = wl_compositor_create_surface( wayland.compositor5 );
	wl_surface_add_listener( existing, &surfaceListener, &existingEvents );
	wl_surface_add_listener( oldClient, &surfaceListener, &oldClientEvents );
	wl_display_roundtrip( wayland.display );
	Display *display = XOpenDisplay( displayName.c_str() );
	if ( !display )
	{
		std::fprintf( stderr, "could not open pinned Xwayland display %s\n", displayName.c_str() );
		return 1;
	}
	const Window root = DefaultRootWindow( display );
	int randrEventBase = 0;
	int randrErrorBase = 0;
	if ( !XRRQueryExtension( display, &randrEventBase, &randrErrorBase ) )
	{
		std::fprintf( stderr, "pinned Xwayland has no RandR extension\n" );
		return 1;
	}
	XRRSelectInput( display, root, RRScreenChangeNotifyMask );
	const Window window = XCreateSimpleWindow( display, root, 10, 10, 64, 48, 0, 0, 0 );
	XMapWindow( display, window );
	XSync( display, False );

	const bool initial = currentMatches( display, 800, 1280, RR_Rotate_0 );
	const bool right = waitForChange( display, authority, "right", 1280, 800,
		RR_Rotate_90, randrEventBase );
	wl_display_roundtrip( wayland.display );
	SurfaceEvents newEvents;
	wl_surface *newSurface = wl_compositor_create_surface( wayland.compositor6 );
	wl_surface_add_listener( newSurface, &surfaceListener, &newEvents );
	wl_display_roundtrip( wayland.display );
	const bool waylandRight = wayland.transform == WL_OUTPUT_TRANSFORM_90 &&
		wayland.modeWidth == 800 && wayland.modeHeight == 1280 &&
		!existingEvents.transforms.empty() &&
		existingEvents.transforms.back() == WL_OUTPUT_TRANSFORM_90 &&
		newEvents.transforms.size() == 1 &&
		newEvents.transforms.back() == WL_OUTPUT_TRANSFORM_90 &&
		oldClientEvents.transforms.empty();
	const bool upside = waitForChange( display, authority, "upsidedown", 800, 1280,
		RR_Rotate_180, randrEventBase );
	wl_display_roundtrip( wayland.display );
	const bool waylandUpside = wayland.transform == WL_OUTPUT_TRANSFORM_180 &&
		existingEvents.transforms.back() == WL_OUTPUT_TRANSFORM_180;
	const bool left = waitForChange( display, authority, "left", 1280, 800,
		RR_Rotate_270, randrEventBase );
	wl_display_roundtrip( wayland.display );
	const bool waylandLeft = wayland.transform == WL_OUTPUT_TRANSFORM_270 &&
		existingEvents.transforms.back() == WL_OUTPUT_TRANSFORM_270;
	const bool normal = waitForChange( display, authority, "normal", 800, 1280,
		RR_Rotate_0, randrEventBase );
	wl_display_roundtrip( wayland.display );
	const bool waylandNormal = wayland.transform == WL_OUTPUT_TRANSFORM_NORMAL &&
		existingEvents.transforms.back() == WL_OUTPUT_TRANSFORM_NORMAL;

	while ( XPending( display ) )
	{
		XEvent event;
		XNextEvent( display, &event );
	}
	authority.publish( "normal" );
	pollfd xfd = { .fd = ConnectionNumber( display ), .events = POLLIN };
	bool duplicateEvent = false;
	if ( poll( &xfd, 1, 300 ) > 0 )
	{
		while ( XPending( display ) )
		{
			XEvent event;
			XNextEvent( display, &event );
			duplicateEvent |= event.type == randrEventBase + RRScreenChangeNotify;
		}
	}

	XDestroyWindow( display, window );
	XCloseDisplay( display );
	wl_surface_destroy( newSurface );
	wl_surface_destroy( oldClient );
	wl_surface_destroy( existing );
	if ( !initial || !right || !upside || !left || !normal || duplicateEvent ||
		!waylandRight || !waylandUpside || !waylandLeft || !waylandNormal )
	{
		std::fprintf( stderr,
			"orientation mismatch initial=%d right=%d upside=%d left=%d normal=%d "
			"duplicate=%d wl=%d/%d/%d/%d\n", initial, right, upside, left,
			normal, duplicateEvent, waylandRight, waylandUpside, waylandLeft,
			waylandNormal );
		return 1;
	}
	return 0;
}
