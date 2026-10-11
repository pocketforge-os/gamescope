#include "xwayland_security.hpp"

#include <cstddef>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace gamescope::xwayland_security
{

bool harden_listeners( std::span<int> listener_fds )
{
	if ( listener_fds.empty() )
		return false;

	std::vector<int> abstract_fds;
	bool has_filesystem_listener = false;
	for ( int fd : listener_fds )
	{
		if ( fd < 0 )
			return false;

		sockaddr_un address = {};
		socklen_t length = sizeof( address );
		if ( getsockname( fd, reinterpret_cast<sockaddr *>( &address ), &length ) != 0 ||
			address.sun_family != AF_UNIX )
			return false;

		const socklen_t path_offset = offsetof( sockaddr_un, sun_path );
		if ( length <= path_offset )
			continue; // Already an inherited-only unnamed listener.
		if ( address.sun_path[0] == '\0' )
			abstract_fds.push_back( fd );
		else
			has_filesystem_listener = true;
	}

	if ( !has_filesystem_listener )
		return false;

	for ( int target_fd : abstract_fds )
	{
		int replacement = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
		if ( replacement < 0 )
			return false;

		sockaddr_un unnamed = {};
		unnamed.sun_family = AF_UNIX;
		const socklen_t unnamed_length = offsetof( sockaddr_un, sun_path );
		if ( bind( replacement, reinterpret_cast<sockaddr *>( &unnamed ), unnamed_length ) != 0 ||
			listen( replacement, 1 ) != 0 ||
			dup3( replacement, target_fd, O_CLOEXEC ) < 0 )
		{
			close( replacement );
			return false;
		}
		close( replacement );
	}
	return true;
}

} // namespace gamescope::xwayland_security
