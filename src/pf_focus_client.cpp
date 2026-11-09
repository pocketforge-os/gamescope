// RED STUB: replaced by the real implementation in the next commit.
#include "pf_focus_client.hpp"
namespace gamescope::pf_focus {
std::string encode_register() { return {}; }
std::string encode_state_snapshot( uint64_t, const FocusState & ) { return {}; }
std::string encode_focus( uint64_t, const FocusState & ) { return {}; }
std::string encode_pong( uint64_t, uint64_t ) { return {}; }
bool append_frame( std::string &, std::string_view ) { return false; }
FrameReader::Result FrameReader::next( std::string & ) { return Result::NeedMore; }
std::optional<RouterMessage> decode_router_message( std::string_view ) { return {}; }
std::optional<std::string> app_id_from_unit( std::string_view ) { return {}; }
std::optional<std::string> service_unit_from_cgroup( std::string_view ) { return {}; }
FocusedSurface classify( const ClientFacts & ) { return {}; }
Client::Client( std::string p, Hooks h ) : m_path( std::move( p ) ), m_hooks( std::move( h ) ) {}
Client::~Client() {}
void Client::reconnect() {}
void Client::dispatch( bool, bool ) {}
void Client::set_focus( const FocusState & ) {}
}
