/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org

Copyright (c) 2000-present Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "Vao/OgreVulkanDynamicBuffer.h"

#include "Vao/OgreVulkanStagingBuffer.h"
#include "Vao/OgreVulkanVaoManager.h"

#include "OgreVulkanDevice.h"
#include "OgreVulkanUtils.h"

namespace Ogre
{
    VulkanDynamicBuffer::VulkanDynamicBuffer( VkDeviceMemory deviceMemory, size_t vboSize,
                                              VulkanVaoManager *vaoManager, BufferType persistentMethod,
                                              VulkanDevice *device ) :
        mDeviceMemory( deviceMemory ),
        mVboSize( vboSize ),
        mMappedPtr( 0 ),
        mPersistentMethod( persistentMethod ),
        mDevice( device ),
        mNonCoherentMemory( false )
    {
        if( mPersistentMethod == BT_DYNAMIC_DEFAULT )
            mPersistentMethod = BT_DYNAMIC_PERSISTENT;

        if( !vaoManager->supportsCoherentMapping() &&
            mPersistentMethod == BT_DYNAMIC_PERSISTENT_COHERENT )
        {
            mPersistentMethod = BT_DYNAMIC_PERSISTENT;
        }
        if( !vaoManager->supportsNonCoherentMapping() && mPersistentMethod == BT_DYNAMIC_PERSISTENT )
            mPersistentMethod = BT_DYNAMIC_PERSISTENT_COHERENT;

        mNonCoherentMemory = mPersistentMethod == BT_DYNAMIC_PERSISTENT;
    }
    //-----------------------------------------------------------------------------------
    VulkanDynamicBuffer::~VulkanDynamicBuffer() {}
    //-----------------------------------------------------------------------------------
    size_t VulkanDynamicBuffer::addMappedRange( size_t start, size_t count )
    {
        size_t ticket;

        if( !mFreeRanges.empty() )
        {
            ticket = mFreeRanges.back();
            mMappedRanges[ticket] = MappedRange( start, count );
            mFreeRanges.pop_back();
        }
        else
        {
            ticket = mMappedRanges.size();
            mMappedRanges.push_back( MappedRange( start, count ) );
        }

        return ticket;
    }
    //-----------------------------------------------------------------------------------
    void *RESTRICT_ALIAS_RETURN VulkanDynamicBuffer::map( size_t start, size_t count, size_t &outTicket )
    {
        assert( start <= mVboSize && start + count <= mVboSize );

        if( mMappedRanges.size() == mFreeRanges.size() )
        {
            VkResult result =
                vkMapMemory( mDevice->mDevice, mDeviceMemory, 0u, mVboSize, 0, &mMappedPtr );
            checkVkResult( result, "vkMapMemory" );
        }

        outTicket = addMappedRange( start, count );

        return static_cast<uint8 *>( mMappedPtr ) + start;
    }
    //-----------------------------------------------------------------------------------
    void VulkanDynamicBuffer::flush( size_t ticket, size_t start, size_t count )
    {
        assert( start <= mMappedRanges[ticket].count && start + count <= mMappedRanges[ticket].count );
        if( mNonCoherentMemory )
        {
            VkMappedMemoryRange mappedRange;
            // Not using makeVkStruct due to how frequent this function may get called
            mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mappedRange.pNext = 0;
            mappedRange.memory = mDeviceMemory;
            mappedRange.offset = start;
            mappedRange.size = count;
            VkResult result = vkFlushMappedMemoryRanges( mDevice->mDevice, 1u, &mappedRange );
            checkVkResult( result, "vkFlushMappedMemoryRanges" );
        }
    }
    //-----------------------------------------------------------------------------------
    void VulkanDynamicBuffer::unmap( size_t ticket )
    {
        assert( ticket < mMappedRanges.size() && "Invalid unmap ticket!" );
        assert( mMappedRanges.size() != mFreeRanges.size() &&
                "Unmapping an already unmapped buffer! Did you call unmap with the same ticket twice?" );

        mFreeRanges.push_back( ticket );

        if( mMappedRanges.size() == mFreeRanges.size() )
        {
            vkUnmapMemory( mDevice->mDevice, mDeviceMemory );
            mMappedPtr = 0;
        }
    }
}  // namespace Ogre