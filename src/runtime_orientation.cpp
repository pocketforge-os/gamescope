#include "runtime_orientation.hpp"

#include <json-c/json.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace gamescope::runtime_orientation
{

namespace
{

void fail( std::string *error, std::string message )
{
	if ( error )
		*error = std::move( message );
}

json_object *required( json_object *object, const char *name, json_type type,
	std::string *error )
{
	json_object *value = nullptr;
	if ( !json_object_object_get_ex( object, name, &value ) ||
		json_object_get_type( value ) != type )
	{
		fail( error, std::string{ "missing or invalid field: " } + name );
		return nullptr;
	}
	return value;
}

std::optional<Token> token( std::string_view value )
{
	if ( value == "normal" ) return Token::Normal;
	if ( value == "left" ) return Token::Left;
	if ( value == "upsidedown" ) return Token::UpsideDown;
	if ( value == "right" ) return Token::Right;
	return std::nullopt;
}

std::optional<LogicalOrientation> logical( std::string_view value )
{
	if ( value == "landscape" ) return LogicalOrientation::Landscape;
	if ( value == "portrait" ) return LogicalOrientation::Portrait;
	if ( value == "landscape_flipped" ) return LogicalOrientation::LandscapeFlipped;
	if ( value == "portrait_flipped" ) return LogicalOrientation::PortraitFlipped;
	return std::nullopt;
}

output_rotation::Transform transform( Token value )
{
	using output_rotation::Transform;
	// runtime 5da5c97c orientation_source.rs:416-424 defines Right as cw90
	// and Left as cw270. orientation.rs:400-407 publishes it unchanged.
	switch ( value )
	{
		case Token::Normal: return Transform::Normal;
		case Token::Right: return Transform::Rotate90;
		case Token::UpsideDown: return Transform::Rotate180;
		case Token::Left: return Transform::Rotate270;
	}
	return Transform::Normal;
}

Token token( output_rotation::Transform value )
{
	switch ( value )
	{
		case output_rotation::Transform::Normal: return Token::Normal;
		case output_rotation::Transform::Rotate90: return Token::Right;
		case output_rotation::Transform::Rotate180: return Token::UpsideDown;
		case output_rotation::Transform::Rotate270: return Token::Left;
	}
	return Token::Normal;
}

output_rotation::Transform orientationTurn( LogicalOrientation value )
{
	using output_rotation::Transform;
	switch ( value )
	{
		case LogicalOrientation::Landscape: return Transform::Normal;
		case LogicalOrientation::Portrait: return Transform::Rotate270;
		case LogicalOrientation::LandscapeFlipped: return Transform::Rotate180;
		case LogicalOrientation::PortraitFlipped: return Transform::Rotate90;
	}
	return Transform::Normal;
}

output_rotation::Transform relativeTurn( LogicalOrientation from,
	LogicalOrientation to )
{
	const uint32_t first = uint32_t( orientationTurn( from ) );
	const uint32_t second = uint32_t( orientationTurn( to ) );
	return output_rotation::Transform( ( second + 4 - first ) % 4 );
}

bool writeAll( int fd, std::span<const uint8_t> bytes )
{
	while ( !bytes.empty() )
	{
		const ssize_t written = send( fd, bytes.data(), bytes.size(), MSG_NOSIGNAL );
		if ( written < 0 && errno == EINTR )
			continue;
		if ( written <= 0 )
			return false;
		bytes = bytes.subspan( size_t( written ) );
	}
	return true;
}

PollResult pollOnceWithCancel( std::string_view socketPath,
	std::optional<uint64_t> afterSerial, uint64_t waitMs, int cancelFd )
{
	if ( socketPath.size() >= sizeof( sockaddr_un::sun_path ) )
		return { {}, "socket path is too long" };
	const int fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 );
	if ( fd < 0 ) return { {}, std::string{ "socket: " } + std::strerror( errno ) };
	auto closeFd = [&] { close( fd ); };
	sockaddr_un address = { .sun_family = AF_UNIX };
	std::memcpy( address.sun_path, socketPath.data(), socketPath.size() );
	if ( connect( fd, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) < 0 )
	{
		const std::string message = std::string{ "connect: " } + std::strerror( errno );
		closeFd(); return { {}, message };
	}
	const auto request = encodeFrame( makeRequest( afterSerial, waitMs ) );
	if ( !writeAll( fd, request ) )
	{
		const std::string message = std::string{ "write: " } + std::strerror( errno );
		closeFd(); return { {}, message };
	}

	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds{ std::min<uint64_t>( waitMs + 1000, 31000 ) };
	FrameDecoder decoder;
	std::array<uint8_t, 4096> buffer;
	while ( true )
	{
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now() );
		if ( remaining.count() <= 0 )
		{
			closeFd(); return { {}, "read: timeout" };
		}
		std::array<pollfd, 2> fds = {{
			{ .fd = fd, .events = POLLIN },
			{ .fd = cancelFd, .events = POLLIN },
		}};
		const nfds_t count = cancelFd >= 0 ? 2 : 1;
		const int ready = poll( fds.data(), count, int( remaining.count() ) );
		if ( ready < 0 && errno == EINTR ) continue;
		if ( ready <= 0 )
		{
			const std::string message = ready == 0 ? "read: timeout" :
				std::string{ "poll: " } + std::strerror( errno );
			closeFd(); return { {}, message };
		}
		if ( cancelFd >= 0 && fds[1].revents )
		{
			closeFd(); return { {}, "cancelled" };
		}
		const ssize_t size = recv( fd, buffer.data(), buffer.size(), 0 );
		if ( size > 0 )
		{
			const FrameStatus status = decoder.feed( { buffer.data(), size_t( size ) } );
			if ( status == FrameStatus::Complete ) break;
			if ( status == FrameStatus::Invalid )
			{
				closeFd(); return { {}, decoder.error() };
			}
			continue;
		}
		if ( size < 0 && errno == EINTR ) continue;
		if ( size == 0 ) decoder.finish();
		const std::string message = size == 0 ? decoder.error() :
			std::string{ "read: " } + std::strerror( errno );
		closeFd(); return { {}, message };
	}
	closeFd();
	PollResult result;
	result.state = parseResponse( decoder.body(), &result.error );
	return result;
}

} // namespace

std::optional<State> parseResponse( std::string_view json, std::string *error )
{
	if ( error ) error->clear();
	json_tokener *tokener = json_tokener_new_ex( 16 );
	if ( !tokener )
	{
		fail( error, "json parser allocation failed" );
		return std::nullopt;
	}
	json_tokener_set_flags( tokener, JSON_TOKENER_STRICT );
	json_object *root = json_tokener_parse_ex( tokener, json.data(), int( json.size() ) );
	const enum json_tokener_error parseError = json_tokener_get_error( tokener );
	const size_t consumed = json_tokener_get_parse_end( tokener );
	json_tokener_free( tokener );
	if ( parseError != json_tokener_success || !root || consumed != json.size() )
	{
		if ( root ) json_object_put( root );
		fail( error, "invalid or trailing JSON" );
		return std::nullopt;
	}
	auto release = [&] { json_object_put( root ); };
	if ( json_object_get_type( root ) != json_type_object )
	{
		release(); fail( error, "response is not an object" ); return std::nullopt;
	}
	json_object *result = required( root, "result", json_type_string, error );
	json_object *version = required( root, "version", json_type_int, error );
	json_object *stateObject = required( root, "state", json_type_object, error );
	if ( !result || !version || !stateObject ||
		std::string_view{ json_object_get_string( result ) } != "orientation" ||
		json_object_get_int64( version ) != ContractVersion )
	{
		if ( result && version && stateObject ) fail( error, "unsupported orientation response" );
		release(); return std::nullopt;
	}

	json_object *serial = required( stateObject, "serial", json_type_int, error );
	json_object *output = required( stateObject, "output", json_type_string, error );
	json_object *logicalValue = required( stateObject, "logical", json_type_string, error );
	json_object *app = required( stateObject, "app", json_type_string, error );
	json_object *presentation = required( stateObject, "presentation", json_type_string, error );
	json_object *wholeBody = required( stateObject, "whole_body", json_type_string, error );
	json_object *screenOnly = required( stateObject, "screen_only", json_type_string, error );
	json_object *locked = required( stateObject, "locked", json_type_boolean, error );
	json_object *degraded = required( stateObject, "degraded", json_type_array, error );
	if ( !serial || !output || !logicalValue || !app || !presentation || !wholeBody ||
		!screenOnly || !locked || !degraded || json_object_get_int64( serial ) < 0 )
	{
		release(); return std::nullopt;
	}

	const auto outputToken = token( json_object_get_string( output ) );
	const auto logicalToken = logical( json_object_get_string( logicalValue ) );
	const auto appToken = logical( json_object_get_string( app ) );
	const auto wholeBodyToken = token( json_object_get_string( wholeBody ) );
	const auto screenOnlyToken = token( json_object_get_string( screenOnly ) );
	const std::string_view presentationString{ json_object_get_string( presentation ) };
	if ( !outputToken || !logicalToken || !appToken || !wholeBodyToken ||
		!screenOnlyToken || ( presentationString != "fill" &&
		presentationString != "letterbox" ) )
	{
		fail( error, "unknown orientation enum token" );
		release(); return std::nullopt;
	}

	State state;
	state.serial = uint64_t( json_object_get_int64( serial ) );
	state.output = *outputToken;
	state.logical = *logicalToken;
	state.app = *appToken;
	state.presentation = presentationString == "fill" ? Presentation::Fill : Presentation::Letterbox;
	state.wholeBody = *wholeBodyToken;
	state.screenOnly = *screenOnlyToken;
	state.locked = json_object_get_boolean( locked );
	const size_t degradedCount = json_object_array_length( degraded );
	for ( size_t i = 0; i < degradedCount; i++ )
	{
		json_object *entry = json_object_array_get_idx( degraded, i );
		if ( !entry || json_object_get_type( entry ) != json_type_string )
		{
			fail( error, "invalid degraded entry" );
			release(); return std::nullopt;
		}
		state.degraded.emplace_back( json_object_get_string( entry ) );
	}
	release();
	return state;
}

Plan makePlan( const State &state, output_rotation::Extent nativeExtent )
{
	const output_rotation::Transform output = transform( state.output );
	return {
		.outputTransform = output,
		.protocolTransform = ProtocolTransform( uint32_t( output ) ),
		.nativeExtent = nativeExtent,
		.logicalExtent = output_rotation::transformExtent( nativeExtent, output ),
		.appToOutput = relativeTurn( state.app, state.logical ),
		.presentation = state.presentation,
	};
}

State fixedMountState( output_rotation::Transform value )
{
	State state;
	state.output = token( value );
	switch ( value )
	{
		case output_rotation::Transform::Normal: state.logical = LogicalOrientation::Landscape; break;
		case output_rotation::Transform::Rotate90: state.logical = LogicalOrientation::PortraitFlipped; break;
		case output_rotation::Transform::Rotate180: state.logical = LogicalOrientation::LandscapeFlipped; break;
		case output_rotation::Transform::Rotate270: state.logical = LogicalOrientation::Portrait; break;
	}
	state.app = state.logical;
	return state;
}

bool Change::any() const
{
	return physical || logicalSize || appCompatibility || reconfigureClients ||
		preferBuffers || updateRandr || cancelTouches || invalidateDirectScanout || diagnostics;
}

ReconfigurationState::ReconfigurationState( output_rotation::Extent nativeExtent,
	State fixedMount )
	: m_nativeExtent( nativeExtent )
	, m_fixedMount( std::move( fixedMount ) )
	, m_state( m_fixedMount )
	, m_plan( makePlan( m_state, m_nativeExtent ) )
{
}

Change ReconfigurationState::apply( const State &state, bool clientsConnected )
{
	return set( state, clientsConnected );
}

Change ReconfigurationState::fallbackToFixedMount( bool clientsConnected )
{
	return set( m_fixedMount, clientsConnected );
}

Change ReconfigurationState::set( const State &state, bool clientsConnected )
{
	const Plan next = makePlan( state, m_nativeExtent );
	Change change;
	change.physical = next.outputTransform != m_plan.outputTransform;
	change.logicalSize = next.logicalExtent != m_plan.logicalExtent;
	change.appCompatibility = next.appToOutput != m_plan.appToOutput ||
		next.presentation != m_plan.presentation;
	const bool semantic = change.physical || change.appCompatibility;
	change.reconfigureClients = clientsConnected && semantic;
	change.preferBuffers = clientsConnected && change.physical;
	change.updateRandr = clientsConnected && change.physical;
	change.cancelTouches = change.physical;
	change.invalidateDirectScanout = change.physical;
	change.diagnostics = state.locked != m_state.locked || state.degraded != m_state.degraded;
	m_state = state;
	m_plan = next;
	return change;
}

FrameStatus FrameDecoder::feed( std::span<const uint8_t> bytes )
{
	if ( m_status != FrameStatus::NeedMore )
	{
		if ( !bytes.empty() && m_status == FrameStatus::Complete )
		{
			m_status = FrameStatus::Invalid;
			m_error = "trailing frame bytes";
		}
		return m_status;
	}
	while ( !bytes.empty() && m_prefix.size() < 4 )
	{
		m_prefix.push_back( bytes.front() );
		bytes = bytes.subspan( 1 );
	}
	if ( m_prefix.size() == 4 && m_expected == 0 )
	{
		m_expected = uint32_t( m_prefix[0] ) << 24 | uint32_t( m_prefix[1] ) << 16 |
			uint32_t( m_prefix[2] ) << 8 | uint32_t( m_prefix[3] );
		if ( m_expected == 0 || m_expected > MaxFrame )
		{
			m_status = FrameStatus::Invalid;
			m_error = "invalid frame length";
			return m_status;
		}
		m_body.reserve( m_expected );
	}
	if ( m_expected != 0 && !bytes.empty() )
	{
		const size_t wanted = m_expected - m_body.size();
		const size_t take = std::min( wanted, bytes.size() );
		m_body.append( reinterpret_cast<const char *>( bytes.data() ), take );
		bytes = bytes.subspan( take );
		if ( m_body.size() == m_expected )
			m_status = bytes.empty() ? FrameStatus::Complete : FrameStatus::Invalid;
		if ( !bytes.empty() ) m_error = "trailing frame bytes";
	}
	return m_status;
}

FrameStatus FrameDecoder::finish()
{
	if ( m_status == FrameStatus::NeedMore )
	{
		m_status = FrameStatus::Invalid;
		m_error = "truncated frame";
	}
	return m_status;
}

std::vector<uint8_t> encodeFrame( std::string_view body )
{
	if ( body.empty() || body.size() > MaxFrame ) return {};
	const uint32_t size = uint32_t( body.size() );
	std::vector<uint8_t> frame = {
		uint8_t( size >> 24 ), uint8_t( size >> 16 ), uint8_t( size >> 8 ), uint8_t( size )
	};
	frame.insert( frame.end(), body.begin(), body.end() );
	return frame;
}

std::string makeRequest( std::optional<uint64_t> afterSerial, uint64_t waitMs )
{
	std::string request = "{\"method\":\"orientation\",\"version\":1";
	if ( afterSerial ) request += ",\"after_serial\":" + std::to_string( *afterSerial );
	request += ",\"wait_ms\":" + std::to_string( waitMs ) + "}";
	return request;
}

PollResult pollOnce( std::string_view socketPath, std::optional<uint64_t> afterSerial,
	uint64_t waitMs )
{
	return pollOnceWithCancel( socketPath, afterSerial, waitMs, -1 );
}

Subscription::Subscription( std::string socketPath, Callback callback )
	: m_socketPath( std::move( socketPath ) )
	, m_callback( std::move( callback ) )
{
}

Subscription::~Subscription()
{
	stop();
}

void Subscription::start()
{
	if ( m_thread.joinable() ) return;
	if ( pipe2( m_cancel, O_CLOEXEC | O_NONBLOCK ) != 0 )
	{
		m_callback( std::nullopt, std::string{ "cancel pipe: " } + std::strerror( errno ) );
		return;
	}
	m_thread = std::thread( &Subscription::run, this );
}

void Subscription::stop()
{
	if ( m_cancel[1] >= 0 )
	{
		const uint8_t byte = 1;
		(void)write( m_cancel[1], &byte, 1 );
	}
	if ( m_thread.joinable() ) m_thread.join();
	for ( int &fd : m_cancel )
	{
		if ( fd >= 0 ) close( fd );
		fd = -1;
	}
}

void Subscription::run()
{
	ReconnectBackoff backoff;
	std::optional<uint64_t> afterSerial;
	bool unavailable = false;
	uint32_t jitterState = 0x9e3779b9u;
	while ( true )
	{
		const PollResult result = pollOnceWithCancel( m_socketPath, afterSerial,
			afterSerial ? 30000 : 0, m_cancel[0] );
		if ( result.error == "cancelled" ) return;
		if ( result.state )
		{
			m_callback( result.state, unavailable ? "recovered" : "connected" );
			unavailable = false;
			afterSerial = result.state->serial;
			backoff.success();
			continue;
		}
		if ( !unavailable ) m_callback( std::nullopt, result.error );
		unavailable = true;
		afterSerial.reset();
		auto delay = backoff.failureDelay();
		jitterState ^= jitterState << 13;
		jitterState ^= jitterState >> 17;
		jitterState ^= jitterState << 5;
		const int jitter = int( jitterState % 21 ) - 10;
		delay += delay * jitter / 100;
		pollfd cancel = { .fd = m_cancel[0], .events = POLLIN };
		if ( poll( &cancel, 1, int( delay.count() ) ) > 0 ) return;
	}
}

std::chrono::milliseconds ReconnectBackoff::failureDelay()
{
	const auto result = m_next;
	m_next = std::min( m_next * 2, std::chrono::milliseconds{ 5000 } );
	return result;
}

} // namespace gamescope::runtime_orientation
