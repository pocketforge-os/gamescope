#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

#include <wayland-server-core.h>

namespace gamescope::wayland_security
{

// Central policy hook for globals that are never public application APIs.
bool is_privileged_global( std::string_view name );

class TrustedClientRegistry
{
public:
	TrustedClientRegistry() = default;
	~TrustedClientRegistry();

	TrustedClientRegistry( const TrustedClientRegistry & ) = delete;
	TrustedClientRegistry &operator=( const TrustedClientRegistry & ) = delete;

	void trust( wl_client *client );
	bool contains( const wl_client *client ) const;

private:
	struct Entry
	{
		TrustedClientRegistry *registry = nullptr;
		wl_client *client = nullptr;
		wl_listener destroy = {};
	};
	static void handle_client_destroy( wl_listener *listener, void *data );
	std::unordered_map<wl_client *, std::unique_ptr<Entry>> m_entries;
};

class TrustedClientBroker
{
public:
	static std::unique_ptr<TrustedClientBroker> create(
		wl_display *display, TrustedClientRegistry &registry );
	~TrustedClientBroker();

	TrustedClientBroker( const TrustedClientBroker & ) = delete;
	TrustedClientBroker &operator=( const TrustedClientBroker & ) = delete;

	// Transfers the capability endpoint that a trusted launcher inherits.
	int take_launcher_fd();

private:
	TrustedClientBroker( wl_display *display, TrustedClientRegistry &registry,
		int server_fd, int launcher_fd );
	static int handle_broker_event( int fd, uint32_t mask, void *data );
	bool mint_client();
	void close_server_endpoint();

	wl_display *m_display = nullptr;
	TrustedClientRegistry &m_registry;
	wl_event_source *m_event_source = nullptr;
	int m_server_fd = -1;
	int m_launcher_fd = -1;
};

// Requests a fresh connected Wayland fd from an inherited broker capability.
// Returns a CLOEXEC fd on success and -1 on every protocol/transport failure.
int request_trusted_client_fd( int broker_fd );

} // namespace gamescope::wayland_security
