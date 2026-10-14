#pragma once

#include <cstdint>

namespace gamescope
{
    struct SurfaceDamageMapping
    {
        int buffer_width;
        int buffer_height;
        int scale;
        uint32_t transform;
        bool viewport_has_src;
        bool viewport_has_dst;
        double viewport_src_x;
        double viewport_src_y;
        double viewport_src_width;
        double viewport_src_height;
        int viewport_dst_width;
        int viewport_dst_height;

        bool operator==( const SurfaceDamageMapping & ) const = default;
    };
}
