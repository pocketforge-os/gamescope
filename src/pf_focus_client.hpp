// PocketForge compositor.sock focus client.
//
// gamescope is the Compositor peer of the PocketForge input router
// (pocketforge-os/runtime, crate pf-input-ipc, module `compositor`; design note
// runtime docs/gamescope-focus-client.md). It registers, sends one full
// state_snapshot per router epoch and then a focus delta whenever the focus
// triple changes. The router arms App Pad rumble only while the snapshot or the
// latest delta names an App surface.
//
// This unit is plain C++ with no wlroots or wlserver dependency, so the framing,
// the encoder, the epoch state machine, the reconnect backoff and the surface
// classifier are unit tested against a fake router (tests/test_pf_focus_client.cpp).
// wlserver.cpp owns the event-loop glue.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace gamescope::pf_focus
{

inline constexpr const char *CompositorSocket = "/run/pocketforge/input/compositor.sock";
inline constexpr uint32_t SchemaMajor = 1;
// pf-wire MAX_FRAME: a larger length prefix closes the connection.
inline constexpr size_t MaxFrame = 64 * 1024;
inline constexpr std::chrono::milliseconds ReconnectInitial{ 100 };
inline constexpr std::chrono::milliseconds ReconnectMax{ 2000 };

// FocusedSurface. v1 never emits `trusted`: the trusted client registry has no
// roles yet, so registry members are reported as `none`.
struct FocusedSurface
{
	enum class Kind { NoSurface, App }; // not `None`: X11 #defines it
	Kind kind = Kind::NoSurface;
	std::string app_id;

	static FocusedSurface none() { return {}; }
	static FocusedSurface app( std::string id ) { return { Kind::App, std::move( id ) }; }
	bool operator==( const FocusedSurface & ) const = default;
};

// The focus triple. v1 has no text-input-v3 tracking, so text_input is
// {active:false, purpose:normal} and secure_entry is false, the same values as
// the runtime golden fixtures.
struct FocusState
{
	FocusedSurface surface;
	bool text_input_active = false;
	bool secure_entry = false;
	bool operator==( const FocusState & ) const = default;
};

// ---- Encoding (byte-identical to serde_json output of pf_input_ipc::encode) ----
std::string encode_register();
std::string encode_state_snapshot( uint64_t epoch, const FocusState &focus );
std::string encode_focus( uint64_t epoch, const FocusState &focus );
std::string encode_pong( uint64_t epoch, uint64_t seq );

// Big-endian u32 length prefix + body. Returns false if body exceeds MaxFrame.
bool append_frame( std::string &out, std::string_view body );

// Accumulates stream bytes and yields complete frame bodies.
class FrameReader
{
public:
	enum class Result { Frame, NeedMore, TooLarge };
	void feed( const char *data, size_t len ) { m_buf.append( data, len ); }
	Result next( std::string &body );

private:
	std::string m_buf;
};

// ---- Decoding router -> compositor frames ----
struct RouterMessage
{
	enum class Type { Registered, Ping, TrustedConnection, SurfaceSecureEntry, Unknown };
	Type type = Type::Unknown;
	std::string type_name;
	uint64_t epoch = 0;
	uint64_t seq = 0;
};
// nullopt: malformed JSON, missing/foreign `version`, or missing required fields.
std::optional<RouterMessage> decode_router_message( std::string_view body );

// ---- Surface classification ----
// Read /proc/<pid>/cgroup while a pidfd pins the process. nullopt if the
// process cannot be pinned or exits during the lookup.
std::optional<std::string> pinned_cgroup( pid_t pid );

// Port of pf_peer_identity::app_id_from_unit.
std::optional<std::string> app_id_from_unit( std::string_view unit );
// Port of pf_peer_identity::service_unit_from_cgroup (leaf + app slice checks).
std::optional<std::string> service_unit_from_cgroup( std::string_view cgroup );

// What wlserver knows about the wl_client that owns the keyboard-focus surface.
struct ClientFacts
{
	bool has_surface = false;
	bool is_xwayland = false; // the wl_client is an Xwayland server
	bool is_trusted = false;  // member of the (role-less) trusted registry
	// /proc/<pid>/cgroup read while the process was pinned by a pidfd;
	// nullopt when the pid could not be resolved or pinned.
	std::optional<std::string> cgroup;
};
// Fails closed: anything that is not a public-socket pf-app@<id>.service
// client is `none`. Xwayland is always `none` (its X11 pid is client-asserted).
FocusedSurface classify( const ClientFacts &facts );

// ---- Client state machine ----
class Client
{
public:
	enum class State { Disconnected, AwaitRegistered, Live };

	struct Hooks
	{
		// The socket fd changed (-1 when disconnected); want_write requests
		// writable notifications while output is pending.
		std::function<void( int fd, bool want_write )> watch_fd;
		// Schedule reconnect() after `delay`.
		std::function<void( std::chrono::milliseconds delay )> schedule_reconnect;
		std::function<void( const std::string & )> log;
	};

	Client( std::string socket_path, Hooks hooks );
	~Client();
	Client( const Client & ) = delete;
	Client &operator=( const Client & ) = delete;

	// Attempt to connect now; on failure schedules a retry with backoff.
	void reconnect();
	// Readable/writable/hangup on fd().
	void dispatch( bool readable, bool writable );
	// Record the current focus triple; sends a delta when Live and changed.
	void set_focus( const FocusState &focus );

	int fd() const { return m_fd; }
	State state() const { return m_state; }
	uint64_t epoch() const { return m_epoch; }
	std::chrono::milliseconds next_backoff() const { return m_backoff; }

private:
	void disconnect( const std::string &why );
	void send_body( const std::string &body );
	bool flush();
	void handle_frame( const std::string &body );
	void log_state( const std::string &msg );
	void update_watch();

	std::string m_path;
	Hooks m_hooks;
	int m_fd = -1;
	State m_state = State::Disconnected;
	uint64_t m_epoch = 0;
	FocusState m_focus;
	FocusState m_sent;
	std::chrono::milliseconds m_backoff = ReconnectInitial;
	FrameReader m_reader;
	std::string m_out;
	bool m_watch_write = false;
	std::string m_last_log;
};

} // namespace gamescope::pf_focus
