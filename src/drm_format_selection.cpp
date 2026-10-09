#include "drm_format_selection.hpp"

#include <drm_fourcc.h>

#include <algorithm>
#include <array>

namespace gamescope::drm_format_selection
{

namespace
{

struct FormatPair
{
	uint32_t opaque;
	uint32_t alpha;
};

constexpr std::array kPrimaryPreferences = {
	FormatPair{ DRM_FORMAT_XRGB2101010, DRM_FORMAT_ARGB2101010 },
	FormatPair{ DRM_FORMAT_XBGR2101010, DRM_FORMAT_ABGR2101010 },
	FormatPair{ DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888 },
	FormatPair{ DRM_FORMAT_XBGR8888, DRM_FORMAT_ABGR8888 },
};

constexpr std::array kOverlayPreferences = {
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
};

bool contains( std::span<const uint32_t> formats, uint32_t wanted )
{
	return std::find( formats.begin(), formats.end(), wanted ) != formats.end();
}

} // namespace

uint32_t selectPrimary( std::span<const uint32_t> supportedFormats )
{
	for ( const FormatPair &preference : kPrimaryPreferences )
	{
		if ( contains( supportedFormats, preference.opaque ) )
			return preference.opaque;
		if ( contains( supportedFormats, preference.alpha ) )
			return preference.alpha;
	}

	return DRM_FORMAT_INVALID;
}

uint32_t selectOverlay( std::span<const uint32_t> supportedFormats )
{
	for ( uint32_t preference : kOverlayPreferences )
	{
		if ( contains( supportedFormats, preference ) )
			return preference;
	}

	return DRM_FORMAT_INVALID;
}

uint32_t alphaEquivalent( uint32_t format )
{
	for ( const FormatPair &preference : kPrimaryPreferences )
	{
		if ( format == preference.opaque || format == preference.alpha )
			return preference.alpha;
	}

	return DRM_FORMAT_INVALID;
}

} // namespace gamescope::drm_format_selection
