#include "commit_fence.hpp"
#include "gamescope-control-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <wayland-client.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{

constexpr int kWidth = 64;
constexpr int kHeight = 64;
std::string g_resultPath;
bool g_holdAfterResult = false;

void writeResult( std::string_view result )
{
	if ( g_resultPath.empty() )
		return;
	const int fd = open( g_resultPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600 );
	if ( fd < 0 )
		return;
	const char *bytes = result.data();
	size_t remaining = result.size();
	while ( remaining )
	{
		const ssize_t amount = write( fd, bytes, remaining );
		if ( amount < 0 && errno == EINTR ) continue;
		if ( amount <= 0 ) break;
		bytes += amount;
		remaining -= size_t( amount );
	}
	close( fd );
}

void check( bool condition, const char *message )
{
	if ( condition )
		return;
	std::fprintf( stderr, "no-fence-presentation: %s\n", message );
	writeResult( std::string{ "FAIL " } + message + "\n" );
	while ( g_holdAfterResult ) pause();
	std::exit( 1 );
}

struct Scratch
{
	std::string directory;
	std::string xwaylandWrapper;
	std::string screenshot;
	std::string initialScreenshot;
	std::string result;
	std::string mangoapp;

	Scratch()
	{
		std::array<char, 56> pattern = {};
		std::snprintf( pattern.data(), pattern.size(), "/tmp/gs-no-fence-%d-XXXXXX",
			int( getpid() ) );
		char *created = mkdtemp( pattern.data() );
		if ( !created )
			return;
		directory = created;
		xwaylandWrapper = directory + "/Xwayland";
		screenshot = directory + "/changed.png";
		initialScreenshot = directory + "/initial.png";
		result = directory + "/result";
		mangoapp = directory + "/mangoapp";
		const int wrapper = open( xwaylandWrapper.c_str(), O_WRONLY | O_CREAT | O_EXCL,
			0700 );
		constexpr char contents[] = "#!/bin/sh\nexec /usr/bin/Xwayland \"$@\" -ac\n";
		if ( wrapper < 0 || write( wrapper, contents, sizeof( contents ) - 1 ) !=
			ssize_t( sizeof( contents ) - 1 ) )
		{
			if ( wrapper >= 0 ) close( wrapper );
			xwaylandWrapper.clear();
			return;
		}
		close( wrapper );
	}

	~Scratch()
	{
		if ( !result.empty() ) unlink( result.c_str() );
		if ( !screenshot.empty() ) unlink( screenshot.c_str() );
		if ( !initialScreenshot.empty() ) unlink( initialScreenshot.c_str() );
		if ( !mangoapp.empty() ) unlink( mangoapp.c_str() );
		if ( !xwaylandWrapper.empty() ) unlink( xwaylandWrapper.c_str() );
		if ( !directory.empty() ) rmdir( directory.c_str() );
	}
};

void fencedControl()
{
	using gamescope::commit_fence::Action;
	check( gamescope::commit_fence::requiresPostEnqueueNudge( Action::Ready ),
		"a ready no-fence commit must wake presentation after enqueue" );
	check( !gamescope::commit_fence::requiresPostEnqueueNudge( Action::Wait ) &&
		!gamescope::commit_fence::requiresPostEnqueueNudge( Action::Error ),
		"only the ready path needs the post-enqueue wake" );

	const int fence = eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
	check( fence >= 0, "create fenced control eventfd" );
	const auto result = gamescope::commit_fence::waitForFd( fence );
	check( result.action == gamescope::commit_fence::Action::Wait,
		"real fence must take the wait path" );

	const int epoll = epoll_create1( EPOLL_CLOEXEC );
	check( epoll >= 0, "create fenced control epoll" );
	epoll_event registration = { .events = EPOLLIN, .data = {} };
	check( epoll_ctl( epoll, EPOLL_CTL_ADD, fence, &registration ) == 0,
		"register fenced control" );
	epoll_event ready = {};
	check( epoll_wait( epoll, &ready, 1, 0 ) == 0,
		"unsignaled fence is the negative control" );
	const eventfd_t signal = 1;
	check( eventfd_write( fence, signal ) == 0, "signal fenced control" );
	check( epoll_wait( epoll, &ready, 1, 1000 ) == 1 && ( ready.events & EPOLLIN ),
		"signaled fence must become ready within the bound" );
	close( epoll );
	close( fence );
}

struct Client
{
	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor = nullptr;
	wl_shm *shm = nullptr;
	xdg_wm_base *wmBase = nullptr;
	gamescope_control *control = nullptr;
	wl_surface *surface = nullptr;
	xdg_surface *xdgSurface = nullptr;
	xdg_toplevel *toplevel = nullptr;
	bool configured = false;
	bool frameDone = false;
	bool screenshotDone = false;
	std::string screenshotPath;
};

void registryGlobal( void *data, wl_registry *registry, uint32_t name,
	const char *interface, uint32_t version )
{
	auto &client = *static_cast<Client *>( data );
	if ( std::strcmp( interface, wl_compositor_interface.name ) == 0 )
		client.compositor = static_cast<wl_compositor *>( wl_registry_bind(
			registry, name, &wl_compositor_interface, std::min( version, 6u ) ) );
	else if ( std::strcmp( interface, wl_shm_interface.name ) == 0 )
		client.shm = static_cast<wl_shm *>( wl_registry_bind(
			registry, name, &wl_shm_interface, 1 ) );
	else if ( std::strcmp( interface, xdg_wm_base_interface.name ) == 0 )
		client.wmBase = static_cast<xdg_wm_base *>( wl_registry_bind(
			registry, name, &xdg_wm_base_interface, std::min( version, 6u ) ) );
	else if ( std::strcmp( interface, gamescope_control_interface.name ) == 0 )
		client.control = static_cast<gamescope_control *>( wl_registry_bind(
			registry, name, &gamescope_control_interface, std::min( version, 6u ) ) );
}

void registryRemove( void *, wl_registry *, uint32_t ) {}

const wl_registry_listener registryListener = {
	.global = registryGlobal,
	.global_remove = registryRemove,
};

void wmPing( void *, xdg_wm_base *wmBase, uint32_t serial )
{
	xdg_wm_base_pong( wmBase, serial );
}

const xdg_wm_base_listener wmBaseListener = { .ping = wmPing };

void surfaceConfigure( void *data, xdg_surface *surface, uint32_t serial )
{
	auto &client = *static_cast<Client *>( data );
	xdg_surface_ack_configure( surface, serial );
	client.configured = true;
}

const xdg_surface_listener xdgSurfaceListener = { .configure = surfaceConfigure };

void toplevelConfigure( void *, xdg_toplevel *, int32_t, int32_t, wl_array * ) {}
void toplevelClose( void *, xdg_toplevel * ) {}
void toplevelBounds( void *, xdg_toplevel *, int32_t, int32_t ) {}
void toplevelCapabilities( void *, xdg_toplevel *, wl_array * ) {}

const xdg_toplevel_listener toplevelListener = {
	.configure = toplevelConfigure,
	.close = toplevelClose,
	.configure_bounds = toplevelBounds,
	.wm_capabilities = toplevelCapabilities,
};

void frameComplete( void *data, wl_callback *callback, uint32_t )
{
	auto &client = *static_cast<Client *>( data );
	client.frameDone = true;
	wl_callback_destroy( callback );
}

const wl_callback_listener frameListener = { .done = frameComplete };

void controlFeature( void *, gamescope_control *, uint32_t, uint32_t, uint32_t ) {}
void controlDisplay( void *, gamescope_control *, const char *, const char *, const char *,
	uint32_t, wl_array * ) {}
void screenshotTaken( void *data, gamescope_control *, const char *path )
{
	auto &client = *static_cast<Client *>( data );
	client.screenshotDone = path && client.screenshotPath == path;
}
void performanceStats( void *, gamescope_control *, uint32_t, uint32_t, uint32_t ) {}

const gamescope_control_listener controlListener = {
	.feature_support = controlFeature,
	.active_display_info = controlDisplay,
	.screenshot_taken = screenshotTaken,
	.app_performance_stats = performanceStats,
};

bool dispatchUntil( Client &client, bool Client::*field, std::chrono::milliseconds timeout )
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while ( !( client.*field ) )
	{
		if ( wl_display_dispatch_pending( client.display ) < 0 ) return false;
		if ( client.*field ) return true;
		if ( wl_display_flush( client.display ) < 0 && errno != EAGAIN ) return false;
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now() );
		if ( remaining.count() <= 0 ) return false;
		pollfd descriptor = {
			.fd = wl_display_get_fd( client.display ),
			.events = POLLIN,
			.revents = 0,
		};
		const int result = poll( &descriptor, 1, int( remaining.count() ) );
		if ( result <= 0 || !( descriptor.revents & POLLIN ) ) return false;
		if ( wl_display_dispatch( client.display ) < 0 ) return false;
	}
	return true;
}

struct Buffer
{
	int fd = -1;
	void *pixels = MAP_FAILED;
	wl_shm_pool *pool = nullptr;
	wl_buffer *buffer = nullptr;

	Buffer() = default;
	Buffer( const Buffer & ) = delete;
	Buffer &operator=( const Buffer & ) = delete;
	Buffer( Buffer &&other ) noexcept
		: fd( std::exchange( other.fd, -1 ) )
		, pixels( std::exchange( other.pixels, MAP_FAILED ) )
		, pool( std::exchange( other.pool, nullptr ) )
		, buffer( std::exchange( other.buffer, nullptr ) )
	{
	}

	~Buffer()
	{
		if ( buffer ) wl_buffer_destroy( buffer );
		if ( pool ) wl_shm_pool_destroy( pool );
		if ( pixels != MAP_FAILED ) munmap( pixels, kWidth * kHeight * 4 );
		if ( fd >= 0 ) close( fd );
	}
};

Buffer makeBuffer( Client &client, uint32_t color )
{
	Buffer result;
	result.fd = memfd_create( "gamescope-no-fence-frame", MFD_CLOEXEC );
	check( result.fd >= 0, "create wl_shm memfd" );
	const size_t size = kWidth * kHeight * 4;
	check( ftruncate( result.fd, off_t( size ) ) == 0, "size wl_shm memfd" );
	result.pixels = mmap( nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, result.fd, 0 );
	check( result.pixels != MAP_FAILED, "map wl_shm memfd" );
	std::fill_n( static_cast<uint32_t *>( result.pixels ), kWidth * kHeight, color );
	result.pool = wl_shm_create_pool( client.shm, result.fd, int( size ) );
	result.buffer = wl_shm_pool_create_buffer( result.pool, 0, kWidth, kHeight,
		kWidth * 4, WL_SHM_FORMAT_XRGB8888 );
	check( result.pool && result.buffer, "create wl_shm buffer" );
	return result;
}

void present( Client &client, wl_buffer *buffer )
{
	client.frameDone = false;
	wl_surface_attach( client.surface, buffer, 0, 0 );
	wl_surface_damage_buffer( client.surface, 0, 0, kWidth, kHeight );
	wl_callback *callback = wl_surface_frame( client.surface );
	wl_callback_add_listener( callback, &frameListener, &client );
	wl_surface_commit( client.surface );
	check( wl_display_flush( client.display ) >= 0 || errno == EAGAIN,
		"flush committed frame" );
}

int runClient( const char *screenshotPath )
{
	Client client;
	client.screenshotPath = screenshotPath;
	client.display = wl_display_connect( nullptr );
	check( client.display, "connect trusted Wayland client" );
	client.registry = wl_display_get_registry( client.display );
	wl_registry_add_listener( client.registry, &registryListener, &client );
	check( wl_display_roundtrip( client.display ) >= 0, "discover Wayland globals" );
	check( client.compositor && client.shm && client.wmBase && client.control,
		"required trusted Wayland globals are present" );
	xdg_wm_base_add_listener( client.wmBase, &wmBaseListener, &client );
	gamescope_control_add_listener( client.control, &controlListener, &client );
	client.surface = wl_compositor_create_surface( client.compositor );
	client.xdgSurface = xdg_wm_base_get_xdg_surface( client.wmBase, client.surface );
	xdg_surface_add_listener( client.xdgSurface, &xdgSurfaceListener, &client );
	client.toplevel = xdg_surface_get_toplevel( client.xdgSurface );
	xdg_toplevel_add_listener( client.toplevel, &toplevelListener, &client );
	xdg_toplevel_set_title( client.toplevel, "no-fence-presentation" );
	xdg_toplevel_set_fullscreen( client.toplevel, nullptr );
	wl_surface_commit( client.surface );
	check( dispatchUntil( client, &Client::configured, std::chrono::seconds{ 3 } ),
		"receive initial xdg-shell configure" );

	Buffer initial = makeBuffer( client, 0x00ff0000u );
	present( client, initial.buffer );
	const std::string screenshotDirectory = std::string{ screenshotPath }.substr(
		0, std::string{ screenshotPath }.find_last_of( '/' ) + 1 );
	const std::string initialScreenshot = screenshotDirectory + "initial.png";
	client.screenshotPath = initialScreenshot;
	gamescope_control_take_screenshot( client.control, initialScreenshot.c_str(),
		GAMESCOPE_CONTROL_SCREENSHOT_TYPE_SCREEN_BUFFER, 0 );
	check( dispatchUntil( client, &Client::frameDone, std::chrono::seconds{ 3 } ),
		"present initial no-fence frame" );
	check( dispatchUntil( client, &Client::screenshotDone, std::chrono::seconds{ 5 } ),
		"prime the initial frame with an external compositor request" );
	unlink( initialScreenshot.c_str() );

	Buffer changed = makeBuffer( client, 0x000000ffu );
	present( client, changed.buffer );
	check( dispatchUntil( client, &Client::frameDone, std::chrono::milliseconds{ 750 } ),
		"changed no-fence frame stayed stale past the bounded wait" );

	client.screenshotDone = false;
	client.screenshotPath = screenshotPath;
	gamescope_control_take_screenshot( client.control, screenshotPath,
		GAMESCOPE_CONTROL_SCREENSHOT_TYPE_SCREEN_BUFFER, 0 );
	check( dispatchUntil( client, &Client::screenshotDone, std::chrono::seconds{ 5 } ),
		"capture the presented screen buffer" );

	int width = 0;
	int height = 0;
	int channels = 0;
	unsigned char *pixels = stbi_load( screenshotPath, &width, &height, &channels, 4 );
	check( pixels && width == kWidth && height == kHeight,
		"decode the 64x64 presented frame" );
	const unsigned char *center = pixels + ( ( height / 2 ) * width + width / 2 ) * 4;
	const bool changedVisible = center[0] < 64 && center[1] < 64 && center[2] > 192;
	stbi_image_free( pixels );
	check( changedVisible, "presented frame did not contain the changed blue buffer" );
	writeResult( "PASS\n" );
	while ( g_holdAfterResult ) pause();

	xdg_toplevel_destroy( client.toplevel );
	xdg_surface_destroy( client.xdgSurface );
	wl_surface_destroy( client.surface );
	gamescope_control_destroy( client.control );
	xdg_wm_base_destroy( client.wmBase );
	wl_shm_destroy( client.shm );
	wl_compositor_destroy( client.compositor );
	wl_registry_destroy( client.registry );
	wl_display_disconnect( client.display );
	return 0;
}

int runParent( const char *self, const char *gamescope, const char *reaper )
{
	fencedControl();
	Scratch scratch;
	check( !scratch.directory.empty() && !scratch.xwaylandWrapper.empty(),
		"create fixture scratch directory" );
	check( symlink( self, scratch.mangoapp.c_str() ) == 0,
		"create trusted mangoapp fixture link" );
	const pid_t child = fork();
	check( child >= 0, "fork gamescope fixture" );
	if ( child == 0 )
	{
		setenv( "XDG_RUNTIME_DIR", scratch.directory.c_str(), 1 );
		setenv( "WLR_XWAYLAND", scratch.xwaylandWrapper.c_str(), 1 );
		setenv( "GS_NO_FENCE_SCREENSHOT", scratch.screenshot.c_str(), 1 );
		setenv( "GS_NO_FENCE_RESULT", scratch.result.c_str(), 1 );
		const std::string reaperPath = reaper;
		const size_t separator = reaperPath.find_last_of( '/' );
		if ( separator == std::string::npos ) _exit( 126 );
		const std::string path = scratch.directory + ":" +
			reaperPath.substr( 0, separator ) + ":/usr/bin:/bin";
		setenv( "PATH", path.c_str(), 1 );
		execl( gamescope, gamescope, "--backend", "headless", "--expose-wayland",
			"-W", "64", "-H", "64", "-r", "60", "--mangoapp", "--keep-alive", "--",
			"/usr/bin/sleep", "60", static_cast<char *>( nullptr ) );
		_exit( 127 );
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 15 };
	int status = 0;
	while ( std::chrono::steady_clock::now() < deadline )
	{
		const int resultFd = open( scratch.result.c_str(), O_RDONLY | O_CLOEXEC );
		if ( resultFd >= 0 )
		{
			std::array<char, 256> result = {};
			const ssize_t size = read( resultFd, result.data(), result.size() - 1 );
			close( resultFd );
			if ( size <= 0 )
			{
				usleep( 10000 );
				continue;
			}
			kill( child, SIGTERM );
			waitpid( child, &status, 0 );
			result[size] = '\0';
			if ( std::strncmp( result.data(), "PASS\n", 5 ) == 0 )
				return 0;
			std::fprintf( stderr, "no-fence-presentation: client result: %s", result.data() );
			return 1;
		}
		const pid_t result = waitpid( child, &status, WNOHANG );
		if ( result == child )
			return 1;
		check( result >= 0, "wait for gamescope fixture" );
		usleep( 10000 );
	}
	kill( child, SIGTERM );
	waitpid( child, &status, 0 );
	check( false, "gamescope fixture exceeded the 15 second bound" );
	return 1;
}

} // namespace

int main( int argc, char **argv )
{
	if ( const char *result = std::getenv( "GS_NO_FENCE_RESULT" ) )
	{
		const char *screenshot = std::getenv( "GS_NO_FENCE_SCREENSHOT" );
		if ( !screenshot ) return 2;
		g_resultPath = result;
		g_holdAfterResult = true;
		return runClient( screenshot );
	}
	if ( argc == 3 && std::strcmp( argv[1], "--client" ) == 0 )
		return runClient( argv[2] );
	if ( argc != 3 )
		return 2;
	std::array<char, 4096> self = {};
	const ssize_t size = readlink( "/proc/self/exe", self.data(), self.size() - 1 );
	check( size > 0, "resolve test executable" );
	self[size] = '\0';
	return runParent( self.data(), argv[1], argv[2] );
}
