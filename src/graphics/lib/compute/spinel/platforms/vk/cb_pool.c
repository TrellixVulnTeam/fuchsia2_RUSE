// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// This is the "basic" fence pool implementation.
//
// A host-OS-optimized platform will work directly with the VkFence
// payloads to avoid scanning for signaled fences.
//

#include "cb_pool.h"
#include "device.h"
#include "common/vk/vk_assert.h"

//
// FIXME -- verify if it's more performant to self-manage a
// VkCommandBuffer pool.
//

struct spn_cb_pool
{
  VkCommandPool cp;
};

//
//
//

void
spn_device_cb_pool_create(struct spn_device * const device)
{
  struct spn_cb_pool * cb_pool =
    spn_allocator_host_perm_alloc(&device->allocator.host.perm,
                                  SPN_MEM_FLAGS_READ_WRITE,
                                  sizeof(*cb_pool));
  device->cb_pool = cb_pool;

  VkCommandPoolCreateInfo const cpci = {
    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext            = NULL,
    .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    .queueFamilyIndex = device->vk->qfi
  };

  vk(CreateCommandPool(device->vk->d,
                       &cpci,
                       device->vk->ac,
                       &cb_pool->cp));
}

void
spn_device_cb_pool_dispose(struct spn_device * const device)
{
  vkDestroyCommandPool(device->vk->d,
                       device->cb_pool->cp,
                       device->vk->ac);

  spn_allocator_host_perm_free(&device->allocator.host.perm,
                               device->cb_pool);
}

//
//
//

VkCommandBuffer
spn_device_cb_pool_acquire(struct spn_device * const device)
{
  VkCommandBufferAllocateInfo const cbai = {
    .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .pNext              = NULL,
    .commandPool        = device->cb_pool->cp,
    .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1
  };

  VkCommandBuffer cb;

  vk(AllocateCommandBuffers(device->vk->d,
                            &cbai,
                            &cb));

  return cb;
}

void
spn_device_cb_pool_release(struct spn_device * const device,
                           VkCommandBuffer     const cb)
{
  vkFreeCommandBuffers(device->vk->d,
                       device->cb_pool->cp,
                       1,
                       &cb);
}

//
//
//
