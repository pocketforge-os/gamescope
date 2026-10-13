#include "vulkan_device_extensions.hpp"

#include <cstdio>
#include <cstdlib>

namespace
{
void check( bool condition, const char *expression, int line )
{
	if ( condition )
		return;

	std::fprintf( stderr, "test_vulkan_device_extensions.cpp:%d: CHECK(%s) failed\n", line, expression );
	std::exit( 1 );
}

#define CHECK( expression ) check( expression, #expression, __LINE__ )
}

int main()
{
	using gamescope::vulkan_device_extensions::ExternalSemaphoreFdSelection;
	using gamescope::vulkan_device_extensions::selectExternalSemaphoreFd;

	// A headless backend without explicit sync has a real no-extension path.
	CHECK( selectExternalSemaphoreFd( false, false, false ) ==
		ExternalSemaphoreFdSelection::Disabled );

	// Preserve explicit sync on headless drivers that do expose the extension.
	CHECK( selectExternalSemaphoreFd( false, false, true ) ==
		ExternalSemaphoreFdSelection::Enabled );

	// DRM scanout remains fail-closed even when explicit sync is unavailable.
	CHECK( selectExternalSemaphoreFd( true, false, false ) ==
		ExternalSemaphoreFdSelection::MissingRequired );
	CHECK( selectExternalSemaphoreFd( true, false, true ) ==
		ExternalSemaphoreFdSelection::Enabled );

	// Any backend exposing the explicit-sync protocol must require its fd bridge.
	CHECK( selectExternalSemaphoreFd( false, true, false ) ==
		ExternalSemaphoreFdSelection::MissingRequired );
	CHECK( selectExternalSemaphoreFd( false, true, true ) ==
		ExternalSemaphoreFdSelection::Enabled );

	return 0;
}
