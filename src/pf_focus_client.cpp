// PocketForge compositor.sock focus client. See pf_focus_client.hpp.

#include "pf_focus_client.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>


#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace gamescope::pf_focus
{

// ------------------------------------------------------------------ encoding

namespace
{

// serde_json string escaping (app ids are already [A-Za-z0-9._-]).
void append_json_string( std::string &out, std::string_view s )
{
	static const char hex[] = "0123456789abcdef";
	out += '"';
	for ( unsigned char c : s )
	{
		switch ( c )
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if ( c < 0x20 )
				{
					out += "\\u00";
					out += hex[c >> 4];
					out += hex[c & 0xf];
				}
				else
					out += (char)c;
		}
	}
	out += '"';
}

std::string header( const char *type )
{
	std::string out = "{\"version\":";
	out += std::to_string( SchemaMajor );
	out += ",\"type\":\"";
	out += type;
	out += '"';
	return out;
}

void append_surface( std::string &out, const FocusedSurface &surface )
{
	if ( surface.kind == FocusedSurface::Kind::App )
	{
		out += "{\"kind\":\"app\",\"app_id\":";
		append_json_string( out, surface.app_id );
		out += '}';
	}
	else
		out += "{\"kind\":\"none\"}";
}

// The FocusSnapshot / Focus field tail: surface, text_input, secure_entry.
void append_focus_fields( std::string &out, const FocusState &focus )
{
	out += "\"surface\":";
	append_surface( out, focus.surface );
	out += ",\"text_input\":{\"active\":";
	out += focus.text_input_active ? "true" : "false";
	out += ",\"purpose\":\"normal\"},\"secure_entry\":";
	out += focus.secure_entry ? "true" : "false";
}

} // namespace

std::string encode_register()
{
	return header( "register" ) + "}";
}

std::string encode_state_snapshot( uint64_t epoch, const FocusState &focus )
{
	std::string out = header( "state_snapshot" );
	out += ",\"epoch\":" + std::to_string( epoch ) + ",\"focus\":{";
	append_focus_fields( out, focus );
	// v1: every overlay is unmapped until the role-aware registry lands, and
	// gamescope tracks no protected Power yet.
	out += "},\"overlays\":[{\"role\":\"keyboard\",\"state\":\"unmapped\"},"
		"{\"role\":\"menu\",\"state\":\"unmapped\"},"
		"{\"role\":\"toast\",\"state\":\"unmapped\"}],"
		"\"power\":{\"pressed\":false,\"last_sequence\":0}}";
	return out;
}

std::string encode_focus( uint64_t epoch, const FocusState &focus )
{
	std::string out = header( "focus" );
	out += ",\"epoch\":" + std::to_string( epoch ) + ",";
	append_focus_fields( out, focus );
	out += '}';
	return out;
}

std::string encode_pong( uint64_t epoch, uint64_t seq )
{
	return header( "pong" ) + ",\"epoch\":" + std::to_string( epoch ) + ",\"seq\":" + std::to_string( seq ) + "}";
}

// ------------------------------------------------------------------ framing

bool append_frame( std::string &out, std::string_view body )
{
	if ( body.size() > MaxFrame )
		return false;
	const uint32_t n = (uint32_t)body.size();
	const char prefix[4] = { (char)( n >> 24 ), (char)( n >> 16 ), (char)( n >> 8 ), (char)n };
	out.append( prefix, 4 );
	out.append( body );
	return true;
}

FrameReader::Result FrameReader::next( std::string &body )
{
	if ( m_buf.size() < 4 )
		return Result::NeedMore;
	const auto *p = reinterpret_cast<const unsigned char *>( m_buf.data() );
	const uint32_t n = ( uint32_t( p[0] ) << 24 ) | ( uint32_t( p[1] ) << 16 ) | ( uint32_t( p[2] ) << 8 ) | p[3];
	if ( n > MaxFrame )
		return Result::TooLarge;
	if ( m_buf.size() < 4 + (size_t)n )
		return Result::NeedMore;
	body.assign( m_buf, 4, n );
	m_buf.erase( 0, 4 + (size_t)n );
	return Result::Frame;
}

// ------------------------------------------------------------------ decoding

namespace
{

// Minimal strict JSON reader: validates the whole document and collects the
// top-level object's string and unsigned-integer members. Nested values are
// validated and skipped.
class JsonReader
{
public:
	explicit JsonReader( std::string_view s ) : m_s( s ) {}

	std::map<std::string, std::string> strings;
	std::map<std::string, uint64_t> numbers;

	bool parse_document()
	{
		ws();
		if ( !object( true ) )
			return false;
		ws();
		return m_i == m_s.size();
	}

private:
	std::string_view m_s;
	size_t m_i = 0;
	int m_depth = 0;

	bool eof() const { return m_i >= m_s.size(); }
	char peek() const { return eof() ? '\0' : m_s[m_i]; }
	void ws()
	{
		while ( !eof() && ( peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r' ) )
			m_i++;
	}
	bool lit( std::string_view l )
	{
		if ( m_s.substr( m_i, l.size() ) != l )
			return false;
		m_i += l.size();
		return true;
	}
	bool string( std::string &out )
	{
		if ( peek() != '"' )
			return false;
		m_i++;
		while ( !eof() )
		{
			unsigned char c = m_s[m_i++];
			if ( c == '"' )
				return true;
			if ( c < 0x20 )
				return false;
			if ( c != '\\' )
			{
				out += (char)c;
				continue;
			}
			if ( eof() )
				return false;
			char e = m_s[m_i++];
			switch ( e )
			{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
					// Field values we read are ASCII; keep \u escapes opaque.
					for ( int k = 0; k < 4; k++ )
					{
						if ( eof() || !isxdigit( (unsigned char)m_s[m_i] ) )
							return false;
						m_i++;
					}
					out += '?';
					break;
				default: return false;
			}
		}
		return false;
	}
	// Returns true for any valid JSON number; sets `u` if it is a u64.
	bool number( std::optional<uint64_t> &u )
	{
		size_t start = m_i;
		bool neg = peek() == '-';
		if ( neg )
			m_i++;
		if ( !isdigit( (unsigned char)peek() ) )
			return false;
		if ( peek() == '0' )
			m_i++;
		else
			while ( isdigit( (unsigned char)peek() ) )
				m_i++;
		size_t int_end = m_i;
		bool integral = true;
		if ( peek() == '.' )
		{
			integral = false;
			m_i++;
			if ( !isdigit( (unsigned char)peek() ) )
				return false;
			while ( isdigit( (unsigned char)peek() ) )
				m_i++;
		}
		if ( peek() == 'e' || peek() == 'E' )
		{
			integral = false;
			m_i++;
			if ( peek() == '+' || peek() == '-' )
				m_i++;
			if ( !isdigit( (unsigned char)peek() ) )
				return false;
			while ( isdigit( (unsigned char)peek() ) )
				m_i++;
		}
		if ( neg || !integral )
			return true;
		uint64_t v = 0;
		for ( size_t k = start; k < int_end; k++ )
		{
			uint64_t d = (uint64_t)( m_s[k] - '0' );
			if ( v > ( UINT64_MAX - d ) / 10 )
				return true; // overflow: valid JSON, not a u64
			v = v * 10 + d;
		}
		u = v;
		return true;
	}
	bool value( const std::string *key )
	{
		ws();
		char c = peek();
		if ( c == '{' )
			return object( false );
		if ( c == '[' )
			return array();
		if ( c == '"' )
		{
			std::string s;
			if ( !string( s ) )
				return false;
			if ( key )
				strings[*key] = std::move( s );
			return true;
		}
		if ( c == 't' )
			return lit( "true" );
		if ( c == 'f' )
			return lit( "false" );
		if ( c == 'n' )
			return lit( "null" );
		std::optional<uint64_t> u;
		if ( !number( u ) )
			return false;
		if ( key && u )
			numbers[*key] = *u;
		return true;
	}
	bool array()
	{
		if ( ++m_depth > 32 )
			return false;
		m_i++; // [
		ws();
		if ( peek() == ']' )
		{
			m_i++;
			m_depth--;
			return true;
		}
		for ( ;; )
		{
			if ( !value( nullptr ) )
				return false;
			ws();
			if ( peek() == ',' )
			{
				m_i++;
				continue;
			}
			if ( peek() != ']' )
				return false;
			m_i++;
			m_depth--;
			return true;
		}
	}
	bool object( bool top )
	{
		if ( peek() != '{' || ++m_depth > 32 )
			return false;
		m_i++;
		ws();
		if ( peek() == '}' )
		{
			m_i++;
			m_depth--;
			return true;
		}
		for ( ;; )
		{
			ws();
			std::string key;
			if ( !string( key ) )
				return false;
			ws();
			if ( peek() != ':' )
				return false;
			m_i++;
			if ( !value( top ? &key : nullptr ) )
				return false;
			ws();
			if ( peek() == ',' )
			{
				m_i++;
				continue;
			}
			if ( peek() != '}' )
				return false;
			m_i++;
			m_depth--;
			return true;
		}
	}
};

} // namespace

std::optional<RouterMessage> decode_router_message( std::string_view body )
{
	JsonReader json( body );
	if ( !json.parse_document() )
		return std::nullopt;
	auto version = json.numbers.find( "version" );
	if ( version == json.numbers.end() || version->second != SchemaMajor )
		return std::nullopt;
	auto type = json.strings.find( "type" );
	if ( type == json.strings.end() )
		return std::nullopt;

	RouterMessage msg;
	msg.type_name = type->second;
	auto field = [&]( const char *name, uint64_t &out ) {
		auto it = json.numbers.find( name );
		if ( it == json.numbers.end() )
			return false;
		out = it->second;
		return true;
	};
	if ( msg.type_name == "registered" )
	{
		msg.type = RouterMessage::Type::Registered;
		if ( !field( "epoch", msg.epoch ) )
			return std::nullopt;
	}
	else if ( msg.type_name == "ping" )
	{
		msg.type = RouterMessage::Type::Ping;
		if ( !field( "epoch", msg.epoch ) || !field( "seq", msg.seq ) )
			return std::nullopt;
	}
	else if ( msg.type_name == "trusted_connection" )
	{
		msg.type = RouterMessage::Type::TrustedConnection;
		field( "epoch", msg.epoch );
	}
	else if ( msg.type_name == "surface_secure_entry" )
	{
		msg.type = RouterMessage::Type::SurfaceSecureEntry;
		field( "epoch", msg.epoch );
	}
	return msg;
}

// ------------------------------------------------------------ classification

std::optional<std::string> app_id_from_unit( std::string_view unit )
{
	constexpr std::string_view prefix = "pf-app@";
	constexpr std::string_view suffix = ".service";
	if ( unit.size() < prefix.size() + suffix.size() || unit.substr( 0, prefix.size() ) != prefix ||
		unit.substr( unit.size() - suffix.size() ) != suffix )
		return std::nullopt;
	std::string_view instance = unit.substr( prefix.size(), unit.size() - prefix.size() - suffix.size() );
	if ( instance.empty() )
		return std::nullopt;
	for ( unsigned char c : instance )
	{
		bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
			c == '.' || c == '-' || c == '_';
		if ( !ok )
			return std::nullopt;
	}
	return std::string( instance );
}

namespace
{

bool starts_with( std::string_view s, std::string_view p ) { return s.substr( 0, p.size() ) == p; }
bool ends_with( std::string_view s, std::string_view p )
{
	return s.size() >= p.size() && s.substr( s.size() - p.size() ) == p;
}

std::optional<std::string> unit_from_cgroup_line( std::string_view line )
{
	size_t c1 = line.find( ':' );
	if ( c1 == std::string_view::npos )
		return std::nullopt;
	size_t c2 = line.find( ':', c1 + 1 );
	if ( c2 == std::string_view::npos )
		return std::nullopt;
	std::string_view hierarchy = line.substr( 0, c1 );
	std::string_view controllers = line.substr( c1 + 1, c2 - c1 - 1 );
	std::string_view path = line.substr( c2 + 1 );

	bool is_systemd = hierarchy == "0" && controllers.empty();
	for ( size_t pos = 0; !is_systemd && pos <= controllers.size(); )
	{
		size_t comma = controllers.find( ',', pos );
		std::string_view name = controllers.substr( pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos );
		if ( name == "name=systemd" )
			is_systemd = true;
		if ( comma == std::string_view::npos )
			break;
		pos = comma + 1;
	}
	if ( !is_systemd )
		return std::nullopt;

	std::vector<std::string_view> components;
	for ( size_t pos = 0; pos <= path.size(); )
	{
		size_t slash = path.find( '/', pos );
		std::string_view comp = path.substr( pos, slash == std::string_view::npos ? std::string_view::npos : slash - pos );
		if ( !comp.empty() )
			components.push_back( comp );
		if ( slash == std::string_view::npos )
			break;
		pos = slash + 1;
	}
	if ( components.empty() )
		return std::nullopt;
	std::string_view leaf = components.back();
	if ( !ends_with( leaf, ".service" ) )
		return std::nullopt;
	for ( size_t k = 0; k + 1 < components.size(); k++ )
	{
		if ( starts_with( components[k], "pf-app@" ) && ends_with( components[k], ".service" ) )
			return std::nullopt;
	}
	if ( starts_with( leaf, "pf-app@" ) &&
		( !app_id_from_unit( leaf ) || components.size() < 2 ||
			components[components.size() - 2] != R"(system-pf\x2dapp.slice)" ) )
		return std::nullopt;
	return std::string( leaf );
}

} // namespace

std::optional<std::string> service_unit_from_cgroup( std::string_view cgroup )
{
	// Same as Rust `lines().find_map(...)`: the first systemd line that yields a unit.
	for ( size_t pos = 0; pos < cgroup.size(); )
	{
		size_t nl = cgroup.find( '\n', pos );
		std::string_view line = cgroup.substr( pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos );
		if ( !line.empty() && line.back() == '\r' )
			line.remove_suffix( 1 );
		if ( auto unit = unit_from_cgroup_line( line ) )
			return unit;
		if ( nl == std::string_view::npos )
			break;
		pos = nl + 1;
	}
	return std::nullopt;
}

FocusedSurface classify( const ClientFacts &facts )
{
	if ( !facts.has_surface || facts.is_xwayland || facts.is_trusted || !facts.cgroup )
		return FocusedSurface::none();
	auto unit = service_unit_from_cgroup( *facts.cgroup );
	if ( !unit )
		return FocusedSurface::none();
	auto app_id = app_id_from_unit( *unit );
	if ( !app_id )
		return FocusedSurface::none();
	return FocusedSurface::app( std::move( *app_id ) );
}

// ------------------------------------------------------------------ client

Client::Client( std::string socket_path, Hooks hooks )
	: m_path( std::move( socket_path ) )
	, m_hooks( std::move( hooks ) )
{
}

Client::~Client()
{
	if ( m_fd >= 0 )
		close( m_fd );
}

void Client::log_state( const std::string &msg )
{
	// Log once per state change, never on every retry.
	if ( msg == m_last_log )
		return;
	m_last_log = msg;
	if ( m_hooks.log )
		m_hooks.log( msg );
}

void Client::update_watch()
{
	bool want_write = m_fd >= 0 && !m_out.empty();
	if ( m_hooks.watch_fd )
		m_hooks.watch_fd( m_fd, want_write );
	m_watch_write = want_write;
}

void Client::reconnect()
{
	if ( m_fd >= 0 )
		return;

	int fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0 );
	int err = 0;
	if ( fd < 0 )
		err = errno;
	else
	{
		sockaddr_un addr{};
		addr.sun_family = AF_UNIX;
		if ( m_path.size() >= sizeof( addr.sun_path ) )
			err = ENAMETOOLONG;
		else
		{
			memcpy( addr.sun_path, m_path.c_str(), m_path.size() + 1 );
			if ( connect( fd, (sockaddr *)&addr, sizeof( addr ) ) != 0 )
				err = errno;
		}
	}
	if ( err != 0 )
	{
		if ( fd >= 0 )
			close( fd );
		log_state( "pf-focus: router unavailable at " + m_path + " (" + strerror( err ) + "), retrying" );
		auto delay = m_backoff;
		m_backoff = std::min( m_backoff * 2, ReconnectMax );
		if ( m_hooks.schedule_reconnect )
			m_hooks.schedule_reconnect( delay );
		return;
	}

	m_fd = fd;
	m_state = State::AwaitRegistered;
	m_epoch = 0;
	m_reader = FrameReader{};
	m_out.clear();
	log_state( "pf-focus: connected to " + m_path + ", registering" );
	send_body( encode_register() );
	if ( m_fd >= 0 )
		update_watch();
}

void Client::disconnect( const std::string &why )
{
	if ( m_fd < 0 )
		return;
	close( m_fd );
	m_fd = -1;
	m_state = State::Disconnected;
	m_epoch = 0; // never replay old-epoch frames
	m_out.clear();
	m_reader = FrameReader{};
	update_watch();
	log_state( "pf-focus: router connection closed (" + why + ")" );
	auto delay = m_backoff;
	m_backoff = std::min( m_backoff * 2, ReconnectMax );
	if ( m_hooks.schedule_reconnect )
		m_hooks.schedule_reconnect( delay );
}

void Client::send_body( const std::string &body )
{
	if ( m_fd < 0 )
		return;
	if ( !append_frame( m_out, body ) )
	{
		disconnect( "frame too large" );
		return;
	}
	flush();
}

// Returns false if the connection was dropped.
bool Client::flush()
{
	while ( !m_out.empty() )
	{
		ssize_t n = send( m_fd, m_out.data(), m_out.size(), MSG_NOSIGNAL | MSG_DONTWAIT );
		if ( n > 0 )
		{
			m_out.erase( 0, (size_t)n );
			continue;
		}
		if ( n < 0 && errno == EINTR )
			continue;
		if ( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) )
			break;
		disconnect( std::string( "send: " ) + strerror( errno ) );
		return false;
	}
	bool want_write = !m_out.empty();
	if ( want_write != m_watch_write )
		update_watch();
	return true;
}

void Client::handle_frame( const std::string &body )
{
	auto msg = decode_router_message( body );
	if ( !msg )
	{
		// Contract: drop the frame and log it.
		if ( m_hooks.log )
			m_hooks.log( "pf-focus: dropped malformed router frame" );
		return;
	}
	switch ( msg->type )
	{
		case RouterMessage::Type::Registered:
			if ( m_state != State::AwaitRegistered || msg->epoch == 0 )
			{
				disconnect( msg->epoch == 0 ? "registered with epoch 0" : "unexpected registered" );
				return;
			}
			m_epoch = msg->epoch;
			m_state = State::Live;
			m_backoff = ReconnectInitial;
			log_state( "pf-focus: registered with router" );
			// Exactly one full snapshot per epoch, before any delta.
			m_sent = m_focus;
			send_body( encode_state_snapshot( m_epoch, m_sent ) );
			return;
		case RouterMessage::Type::Ping:
			if ( m_state == State::Live && msg->epoch == m_epoch )
				send_body( encode_pong( m_epoch, msg->seq ) );
			else if ( m_hooks.log )
				m_hooks.log( "pf-focus: dropped ping for another epoch" );
			return;
		default:
			// v1: no role-bound trusted clients, no secure-entry surfaces.
			if ( m_hooks.log )
				m_hooks.log( "pf-focus: dropped router frame type=" + msg->type_name );
			return;
	}
}

void Client::dispatch( bool readable, bool writable )
{
	if ( m_fd < 0 )
		return;
	if ( writable && !flush() )
		return;
	if ( !readable )
		return;
	for ( ;; )
	{
		char buf[4096];
		ssize_t n = recv( m_fd, buf, sizeof( buf ), MSG_DONTWAIT );
		if ( n == 0 )
		{
			disconnect( "EOF" );
			return;
		}
		if ( n < 0 )
		{
			if ( errno == EINTR )
				continue;
			if ( errno == EAGAIN || errno == EWOULDBLOCK )
				break;
			disconnect( std::string( "recv: " ) + strerror( errno ) );
			return;
		}
		m_reader.feed( buf, (size_t)n );
		std::string body;
		for ( ;; )
		{
			auto r = m_reader.next( body );
			if ( r == FrameReader::Result::TooLarge )
			{
				disconnect( "frame exceeds MAX_FRAME" );
				return;
			}
			if ( r == FrameReader::Result::NeedMore )
				break;
			handle_frame( body );
			if ( m_fd < 0 )
				return;
		}
	}
}

void Client::set_focus( const FocusState &focus )
{
	m_focus = focus;
	if ( m_state != State::Live || m_focus == m_sent )
		return;
	m_sent = m_focus;
	send_body( encode_focus( m_epoch, m_sent ) );
}

} // namespace gamescope::pf_focus
