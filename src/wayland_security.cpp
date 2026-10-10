#include "wayland_security.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace gamescope::wayland_security
{

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

constexpr char kMintRequest = 'W';
constexpr char kMintResponse = 'w';

bool send_fd( int socket_fd, int fd )
{
	iovec iov = {
		.iov_base = const_cast<char *>( &kMintResponse ),
		.iov_len = sizeof( kMintResponse ),
	};
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
	std::memcpy( CMSG_DATA( header ), &fd, sizeof( fd ) );

	return sendmsg( socket_fd, &message, MSG_NOSIGNAL ) == sizeof( kMintResponse );
}

} // namespace

bool is_privileged_global( std::string_view name )
{
	for ( std::string_view privileged : kPrivilegedGlobals )
	{
		if ( name == privileged )
			return true;
	}
	return false;
}

TrustedClientRegistry::~TrustedClientRegistry()
{
	for ( auto &[ client, entry ] : m_entries )
		wl_list_remove( &entry->destroy.link );
}

void TrustedClientRegistry::trust( wl_client *client )
{
	if ( !client || m_entries.contains( client ) )
		return;

	auto entry = std::make_unique<Entry>();
	entry->registry = this;
	entry->client = client;
	entry->destroy.notify = handle_client_destroy;
	wl_client_add_destroy_listener( client, &entry->destroy );
	m_entries.emplace( client, std::move( entry ) );
}

bool TrustedClientRegistry::contains( const wl_client *client ) const
{
	return client && m_entries.contains( const_cast<wl_client *>( client ) );
}

void TrustedClientRegistry::handle_client_destroy( wl_listener *listener, void * )
{
	Entry *entry = wl_container_of( listener, entry, destroy );
	TrustedClientRegistry *registry = entry->registry;
	wl_client *client = entry->client;
	wl_list_remove( &entry->destroy.link );
	registry->m_entries.erase( client );
}

TrustedClientBroker::TrustedClientBroker( wl_display *display,
	TrustedClientRegistry &registry, int server_fd, int launcher_fd )
	: m_display( display )
	, m_registry( registry )
	, m_server_fd( server_fd )
	, m_launcher_fd( launcher_fd )
{
}

std::unique_ptr<TrustedClientBroker> TrustedClientBroker::create(
	wl_display *display, TrustedClientRegistry &registry )
{
	if ( !display )
		return nullptr;

	int sockets[2] = { -1, -1 };
	if ( socketpair( AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets ) != 0 )
		return nullptr;

	auto broker = std::unique_ptr<TrustedClientBroker>(
		new TrustedClientBroker( display, registry, sockets[0], sockets[1] ) );
	wl_event_loop *event_loop = wl_display_get_event_loop( display );
	broker->m_event_source = wl_event_loop_add_fd( event_loop, broker->m_server_fd,
		WL_EVENT_READABLE, handle_broker_event, broker.get() );
	if ( !broker->m_event_source )
		return nullptr;
	return broker;
}

TrustedClientBroker::~TrustedClientBroker()
{
	if ( m_event_source )
		wl_event_source_remove( m_event_source );
	if ( m_server_fd >= 0 )
		close( m_server_fd );
	if ( m_launcher_fd >= 0 )
		close( m_launcher_fd );
}

int TrustedClientBroker::take_launcher_fd()
{
	return std::exchange( m_launcher_fd, -1 );
}

void TrustedClientBroker::close_server_endpoint()
{
	if ( m_event_source )
	{
		wl_event_source_remove( m_event_source );
		m_event_source = nullptr;
	}
	if ( m_server_fd >= 0 )
	{
		close( m_server_fd );
		m_server_fd = -1;
	}
}

int TrustedClientBroker::handle_broker_event( int fd, uint32_t mask, void *data )
{
	auto *broker = static_cast<TrustedClientBroker *>( data );
	if ( mask & ( WL_EVENT_HANGUP | WL_EVENT_ERROR ) )
	{
		broker->close_server_endpoint();
		return 0;
	}
	if ( !( mask & WL_EVENT_READABLE ) )
		return 0;

	char request = 0;
	const ssize_t amount = recv( fd, &request, sizeof( request ), 0 );
	if ( amount != sizeof( request ) || request != kMintRequest || !broker->mint_client() )
		broker->close_server_endpoint();
	return 0;
}

bool TrustedClientBroker::mint_client()
{
	int sockets[2] = { -1, -1 };
	if ( socketpair( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets ) != 0 )
		return false;

	wl_client *client = wl_client_create( m_display, sockets[0] );
	if ( !client )
	{
		close( sockets[0] );
		close( sockets[1] );
		return false;
	}

	m_registry.trust( client );
	const bool sent = send_fd( m_server_fd, sockets[1] );
	close( sockets[1] );
	if ( !sent )
		wl_client_destroy( client );
	return sent;
}

int request_trusted_client_fd( int broker_fd )
{
	if ( broker_fd < 0 ||
		send( broker_fd, &kMintRequest, sizeof( kMintRequest ), MSG_NOSIGNAL ) != sizeof( kMintRequest ) )
		return -1;

	char response = 0;
	iovec iov = {
		.iov_base = &response,
		.iov_len = sizeof( response ),
	};
	std::array<char, CMSG_SPACE( sizeof( int ) )> control = {};
	msghdr message = {};
	message.msg_iov = &iov;
	message.msg_iovlen = 1;
	message.msg_control = control.data();
	message.msg_controllen = control.size();
	const ssize_t amount = recvmsg( broker_fd, &message, MSG_CMSG_CLOEXEC );
	if ( amount != sizeof( response ) || response != kMintResponse ||
		( message.msg_flags & ( MSG_CTRUNC | MSG_TRUNC ) ) )
		return -1;

	int received_fd = -1;
	for ( cmsghdr *header = CMSG_FIRSTHDR( &message ); header;
		header = CMSG_NXTHDR( &message, header ) )
	{
		if ( header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
			header->cmsg_len != CMSG_LEN( sizeof( int ) ) || received_fd >= 0 )
		{
			if ( received_fd >= 0 )
				close( received_fd );
			return -1;
		}
		std::memcpy( &received_fd, CMSG_DATA( header ), sizeof( received_fd ) );
	}
	return received_fd;
}

} // namespace gamescope::wayland_security
