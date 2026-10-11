#include "drm_device_selection.hpp"

#include <cstdio>

using namespace gamescope::drm_device_selection;

namespace
{

int g_failures = 0;

#define CHECK( expression ) check( expression, #expression, __LINE__ )

void check( bool value, const char *expression, int line )
{
	if ( value )
		return;

	std::fprintf( stderr, "test_drm_device_selection.cpp:%d: CHECK(%s) failed\n",
		line, expression );
	g_failures++;
}

void test_preferred_device_overrides_render_device()
{
	const DeviceSelection selection = selectDevice( "card0", "/dev/dri/card1" );
	CHECK( selection.source == DeviceSource::Preferred );
	CHECK( selection.path == "/dev/dri/card0" );

	const DeviceSelection byPath = selectDevice(
		"/dev/dri/by-path/platform-1c0c000.lcd-card", "/dev/dri/card1" );
	CHECK( byPath.source == DeviceSource::Preferred );
	CHECK( byPath.path == "/dev/dri/by-path/platform-1c0c000.lcd-card" );
}

void test_unset_preferred_device_preserves_render_device()
{
	const DeviceSelection selection = selectDevice( "", "/dev/dri/card1" );
	CHECK( selection.source == DeviceSource::VulkanPrimary );
	CHECK( selection.path == "/dev/dri/card1" );
}

void test_empty_inputs_select_discovery()
{
	const DeviceSelection selection = selectDevice( "", "" );
	CHECK( selection.source == DeviceSource::Discovery );
	CHECK( selection.path.empty() );
}

} // namespace

int main()
{
	test_preferred_device_overrides_render_device();
	test_unset_preferred_device_preserves_render_device();
	test_empty_inputs_select_discovery();
	return g_failures == 0 ? 0 : 1;
}
