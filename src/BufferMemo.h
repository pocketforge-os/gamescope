#pragma once

#include "rc.h"
#include "rendervulkan.hpp"

#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <span>
#include <vector>

#include <pixman-1/pixman.h>

struct wl_listener;
struct wlr_buffer;

// TODO: Move to common code when we want it more.
#define WAYLAND_LISTENER( member_listener, func ) \
    wl_listener { .notify = []( wl_listener *pListener, void *pUserData ) { decltype( this ) pObject = wl_container_of( pListener, pObject, member_listener ); pObject->func( pUserData ); } }

namespace gamescope
{
    class CBufferMemoizer;
    class CBufferMemo;

    class CBufferMemo
    {
    public:
        CBufferMemo( CBufferMemoizer *pMemoizer, wlr_buffer *pBuffer, OwningRc<CVulkanTexture> pTexture,
            uint64_t uSurfaceId, bool bCpuBacked );
        ~CBufferMemo();

        void Finalize();

        CBufferMemoizer *GetMemoizer() const { return m_pMemoizer; }

        const OwningRc<CVulkanTexture> &GetVulkanTexture() const { return m_pVulkanTexture; }

        bool IsCpuBacked() const { return m_bCpuBacked; }
        bool IsForSurface( uint64_t uSurfaceId ) const { return m_uSurfaceId == uSurfaceId; }
        void AddDamage( std::span<const pixman_box32_t> damage );
        std::vector<pixman_box32_t> TakeDamage();
        void ResetForSurface( uint64_t uSurfaceId, int nWidth, int nHeight );

        void OnBufferDestroyed( void *pUserData );
    private:
        CBufferMemoizer *m_pMemoizer = nullptr;
        wlr_buffer *m_pBuffer = nullptr;
        wl_listener m_DeleteListener = WAYLAND_LISTENER( m_DeleteListener, OnBufferDestroyed );

        // OwningRc to have a private reference:
        // So we can keep the CVulkanTexture, as public references
        // determine when we give the texture/buffer back to the app.
        OwningRc<CVulkanTexture> m_pVulkanTexture;
        uint64_t m_uSurfaceId = 0;
        bool m_bCpuBacked = false;
        pixman_region32_t m_PendingDamage;
    };

    class CBufferMemoizer
    {
    public:
        // Must return an OwningRc for the locking to make sense and not deadlock.
        OwningRc<CVulkanTexture> LookupVulkanTexture( wlr_buffer *pBuffer, uint64_t uSurfaceId,
            std::span<const pixman_box32_t> damage, std::vector<pixman_box32_t> &uploadDamage );
        void RequeueDamage( wlr_buffer *pBuffer, std::span<const pixman_box32_t> damage );

        void MemoizeBuffer( wlr_buffer *pBuffer, OwningRc<CVulkanTexture> pTexture,
            uint64_t uSurfaceId, bool bCpuBacked );
        void UnmemoizeBuffer( wlr_buffer *pBuffer );
    private:
        mutable std::mutex m_mutBufferMemos;
        std::unordered_map<wlr_buffer *, CBufferMemo> m_BufferMemos;
    };

}
