#pragma once

#include <cstdint>
#include <span>

namespace gamescope::drm_format_selection
{

uint32_t selectPrimary( std::span<const uint32_t> supportedFormats );
uint32_t selectOverlay( std::span<const uint32_t> supportedFormats );
uint32_t alphaEquivalent( uint32_t format );

} // namespace gamescope::drm_format_selection
