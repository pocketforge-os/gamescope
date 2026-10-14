#include "BufferMemo.h"

#include "wlserver.hpp"

namespace gamescope
{
    static LogScope memo_log{ "BufferMemo" };

    /////////////////
    // CBufferMemo
    /////////////////

    CBufferMemo::CBufferMemo( CBufferMemoizer *pMemoizer, wlr_buffer *pBuffer, OwningRc<CVulkanTexture> pTexture,
        uint64_t uSurfaceId, uint64_t uMappingGeneration, bool bCpuBacked )
        : m_pMemoizer{ pMemoizer }
        , m_pBuffer{ pBuffer }
        , m_pVulkanTexture{ std::move( pTexture ) }
        , m_uSurfaceId{ uSurfaceId }
        , m_uMappingGeneration{ uMappingGeneration }
        , m_bCpuBacked{ bCpuBacked }
    {
        pixman_region32_init( &m_PendingDamage );
    }

    CBufferMemo::~CBufferMemo()
    {
        pixman_region32_fini( &m_PendingDamage );
        wl_list_remove( &m_DeleteListener.link );
    }

    void CBufferMemo::AddDamage( std::span<const pixman_box32_t> damage )
    {
        for ( const pixman_box32_t &rect : damage )
        {
            pixman_region32_union_rect( &m_PendingDamage, &m_PendingDamage,
                rect.x1, rect.y1, rect.x2 - rect.x1, rect.y2 - rect.y1 );
        }
    }

    std::vector<pixman_box32_t> CBufferMemo::TakeDamage()
    {
        int nCount = 0;
        const pixman_box32_t *pRects = pixman_region32_rectangles( &m_PendingDamage, &nCount );
        std::vector<pixman_box32_t> result;
        if ( nCount > 0 )
            result.assign( pRects, pRects + nCount );
        pixman_region32_clear( &m_PendingDamage );
        return result;
    }

    void CBufferMemo::ResetForMapping( uint64_t uSurfaceId, uint64_t uMappingGeneration,
        int nWidth, int nHeight )
    {
        m_uSurfaceId = uSurfaceId;
        m_uMappingGeneration = uMappingGeneration;
        pixman_region32_clear( &m_PendingDamage );
        pixman_region32_union_rect( &m_PendingDamage, &m_PendingDamage, 0, 0, nWidth, nHeight );
    }

    void CBufferMemo::Finalize()
    {
        wlserver_lock();
	    wl_signal_add( &m_pBuffer->events.destroy, &m_DeleteListener );
	    wlserver_unlock();
    }

    void CBufferMemo::OnBufferDestroyed( void *pUserData )
    {
        assert( m_pVulkanTexture->GetRefCount() == 0 );
        m_pMemoizer->UnmemoizeBuffer( m_pBuffer );
    }

    ///////////////////
    // CBufferMemoizer
    ///////////////////

    OwningRc<CVulkanTexture> CBufferMemoizer::LookupVulkanTexture( wlr_buffer *pBuffer, uint64_t uSurfaceId,
        uint64_t uMappingGeneration, std::span<const pixman_box32_t> damage,
        std::vector<pixman_box32_t> &uploadDamage )
    {
        std::scoped_lock lock{ m_mutBufferMemos };

        // Surface damage describes changes from the previous surface contents,
        // not changes from the last contents of each buffer in a client pool.
        // Every cached CPU-backed buffer for this surface therefore needs to
        // receive the damage before it is next attached.
        for ( auto &entry : m_BufferMemos )
        {
            CBufferMemo &memo = entry.second;
            if ( memo.IsCpuBacked() && memo.IsForMapping( uSurfaceId, uMappingGeneration ) )
                memo.AddDamage( damage );
        }

        auto iter = m_BufferMemos.find( pBuffer );
        if ( iter == m_BufferMemos.end() )
            return nullptr;

        CBufferMemo &memo = iter->second;
        if ( memo.IsCpuBacked() )
        {
            // A wl_buffer may move between surfaces or return after its
            // surface mapping changed. A full refresh is conservative because
            // the accumulated damage is in the previous buffer coordinate space.
            if ( !memo.IsForMapping( uSurfaceId, uMappingGeneration ) )
                memo.ResetForMapping( uSurfaceId, uMappingGeneration,
                    pBuffer->width, pBuffer->height );
            uploadDamage = memo.TakeDamage();
        }

        return memo.GetVulkanTexture();
    }

    void CBufferMemoizer::RequeueDamage( wlr_buffer *pBuffer, std::span<const pixman_box32_t> damage )
    {
        std::scoped_lock lock{ m_mutBufferMemos };
        auto iter = m_BufferMemos.find( pBuffer );
        if ( iter != m_BufferMemos.end() && iter->second.IsCpuBacked() )
            iter->second.AddDamage( damage );
    }

    void CBufferMemoizer::MemoizeBuffer( wlr_buffer *pBuffer, OwningRc<CVulkanTexture> pTexture,
        uint64_t uSurfaceId, uint64_t uMappingGeneration, bool bCpuBacked )
    {
        memo_log.debugf( "Memoizing new buffer: wlr_buffer %p -> texture: %p", pBuffer, pTexture.get() );

        // Can't hold m_mutBufferMemos while we finalize link from pMemo to buffer
        // as we can't have wlserver_lock held otherwise we can deadlock when
        // adding the wl_signal.
        //
        // This is fine as the lookups only happen on one thread, that calls this
        // or LookupVulkanTexture.
        CBufferMemo *pMemo = nullptr;
        {
            std::scoped_lock lock{ m_mutBufferMemos };
            auto [ iter, bSuccess ] = m_BufferMemos.emplace( std::piecewise_construct,
                std::forward_as_tuple( pBuffer ),
                std::forward_as_tuple( this, pBuffer, std::move( pTexture ), uSurfaceId,
                    uMappingGeneration, bCpuBacked ) );

            assert( bSuccess );
            pMemo = &iter->second;
        }
        pMemo->Finalize();
    }

    void CBufferMemoizer::UnmemoizeBuffer( wlr_buffer *pBuffer )
    {
        memo_log.debugf( "Unmemoizing buffer: wlr_buffer %p", pBuffer );

        std::scoped_lock lock{ m_mutBufferMemos };
        auto iter = m_BufferMemos.find( pBuffer );
        assert( iter != m_BufferMemos.end() );
        m_BufferMemos.erase( iter );
    }
}
