#include "drm_format_selection.hpp"

#include <drm_fourcc.h>

#include <array>
#include <cstdio>

using namespace gamescope::drm_format_selection;

namespace
{

int g_failures = 0;

#define CHECK( expression ) check( expression, #expression, __LINE__ )

void check( bool value, const char *expression, int line )
{
	if ( value )
		return;

	std::fprintf( stderr, "test_drm_format_selection.cpp:%d: CHECK(%s) failed\n",
		line, expression );
	g_failures++;
}

void test_bgr_8888_fallback()
{
	const std::array formats = { DRM_FORMAT_ABGR8888, DRM_FORMAT_XBGR8888 };
	CHECK( selectPrimary( formats ) == DRM_FORMAT_XBGR8888 );
	CHECK( selectOverlay( formats ) == DRM_FORMAT_ABGR8888 );
	CHECK( alphaEquivalent( DRM_FORMAT_XBGR8888 ) == DRM_FORMAT_ABGR8888 );
}

void test_existing_rgb_preference_is_preserved()
{
	const std::array formats = { DRM_FORMAT_XBGR8888, DRM_FORMAT_XRGB8888 };
	CHECK( selectPrimary( formats ) == DRM_FORMAT_XRGB8888 );
}

void test_alpha_fallback()
{
	const std::array formats = { DRM_FORMAT_ABGR8888 };
	CHECK( selectPrimary( formats ) == DRM_FORMAT_ABGR8888 );
	CHECK( alphaEquivalent( DRM_FORMAT_ABGR8888 ) == DRM_FORMAT_ABGR8888 );
}

void test_unsupported_formats_are_rejected()
{
	const std::array formats = { DRM_FORMAT_RGB565 };
	CHECK( selectPrimary( formats ) == DRM_FORMAT_INVALID );
	CHECK( selectOverlay( formats ) == DRM_FORMAT_INVALID );
	CHECK( alphaEquivalent( DRM_FORMAT_RGB565 ) == DRM_FORMAT_INVALID );
}

void test_all_primary_formats_have_alpha_equivalents()
{
	const std::array formats = {
		DRM_FORMAT_XRGB2101010,
		DRM_FORMAT_ARGB2101010,
		DRM_FORMAT_XBGR2101010,
		DRM_FORMAT_ABGR2101010,
		DRM_FORMAT_XRGB8888,
		DRM_FORMAT_ARGB8888,
		DRM_FORMAT_XBGR8888,
		DRM_FORMAT_ABGR8888,
	};

	for ( uint32_t format : formats )
		CHECK( alphaEquivalent( format ) != DRM_FORMAT_INVALID );
}

} // namespace

int main()
{
	test_bgr_8888_fallback();
	test_existing_rgb_preference_is_preserved();
	test_alpha_fallback();
	test_unsupported_formats_are_rejected();
	test_all_primary_formats_have_alpha_equivalents();
	return g_failures == 0 ? 0 : 1;
}
