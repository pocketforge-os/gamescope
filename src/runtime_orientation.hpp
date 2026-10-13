#pragma once

#include "output_rotation.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gamescope::runtime_orientation
{

constexpr std::string_view DefaultSocket = "/run/pocketforge/session-authority.sock";
constexpr uint32_t ContractVersion = 1;
constexpr uint32_t MaxFrame = 64 * 1024;

enum class Token { Normal, Left, UpsideDown, Right };
enum class LogicalOrientation { Landscape, Portrait, LandscapeFlipped, PortraitFlipped };
enum class Presentation { Fill, Letterbox };

// Values intentionally match wl_output_transform's non-flipped values without
// importing Wayland into the policy/parser layer.
enum class ProtocolTransform : uint32_t { Normal, Rotate90, Rotate180, Rotate270 };

struct State
{
	uint64_t serial = 0;
	Token output = Token::Normal;
	LogicalOrientation logical = LogicalOrientation::Landscape;
	LogicalOrientation app = LogicalOrientation::Landscape;
	Presentation presentation = Presentation::Fill;
	Token wholeBody = Token::Normal;
	Token screenOnly = Token::Normal;
	bool locked = false;
	std::vector<std::string> degraded;

	bool operator==( const State & ) const = default;
};

struct Plan
{
	output_rotation::Transform outputTransform = output_rotation::Transform::Normal;
	ProtocolTransform protocolTransform = ProtocolTransform::Normal;
	output_rotation::Extent nativeExtent = {};
	output_rotation::Extent logicalExtent = {};
	output_rotation::Transform appToOutput = output_rotation::Transform::Normal;
	Presentation presentation = Presentation::Fill;

	bool operator==( const Plan & ) const = default;
};

struct Change
{
	bool physical = false;
	bool logicalSize = false;
	bool appCompatibility = false;
	bool reconfigureClients = false;
	bool preferBuffers = false;
	bool updateRandr = false;
	bool cancelTouches = false;
	bool invalidateDirectScanout = false;
	bool diagnostics = false;

	bool any() const;
};

std::optional<State> parseResponse( std::string_view json, std::string *error = nullptr );
Plan makePlan( const State &state, output_rotation::Extent nativeExtent );
State fixedMountState( output_rotation::Transform transform );

class ReconfigurationState
{
public:
	ReconfigurationState( output_rotation::Extent nativeExtent, State fixedMount );
	Change apply( const State &state, bool clientsConnected );
	Change fallbackToFixedMount( bool clientsConnected );
	const State &state() const { return m_state; }
	const Plan &plan() const { return m_plan; }

private:
	Change set( const State &state, bool clientsConnected );
	output_rotation::Extent m_nativeExtent;
	State m_fixedMount;
	State m_state;
	Plan m_plan;
};

enum class FrameStatus { NeedMore, Complete, Invalid };

class FrameDecoder
{
public:
	FrameStatus feed( std::span<const uint8_t> bytes );
	FrameStatus finish();
	std::string_view body() const { return m_body; }
	const std::string &error() const { return m_error; }

private:
	std::vector<uint8_t> m_prefix;
	std::string m_body;
	uint32_t m_expected = 0;
	FrameStatus m_status = FrameStatus::NeedMore;
	std::string m_error;
};

std::vector<uint8_t> encodeFrame( std::string_view body );
std::string makeRequest( std::optional<uint64_t> afterSerial, uint64_t waitMs );

struct PollResult
{
	std::optional<State> state;
	std::string error;
};

PollResult pollOnce( std::string_view socketPath, std::optional<uint64_t> afterSerial,
	uint64_t waitMs );

class Subscription
{
public:
	using Callback = std::function<void( const std::optional<State> &, std::string_view )>;

	Subscription( std::string socketPath, Callback callback );
	~Subscription();
	Subscription( const Subscription & ) = delete;
	Subscription &operator=( const Subscription & ) = delete;
	void start();
	void stop();

private:
	void run();
	std::string m_socketPath;
	Callback m_callback;
	int m_cancel[2] = { -1, -1 };
	std::thread m_thread;
};

class ReconnectBackoff
{
public:
	std::chrono::milliseconds failureDelay();
	void success() { m_next = std::chrono::milliseconds{ 100 }; }

private:
	std::chrono::milliseconds m_next{ 100 };
};

} // namespace gamescope::runtime_orientation
