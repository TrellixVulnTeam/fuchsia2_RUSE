// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vkreadback.h"
#include "gtest/gtest.h"

int main(int argc, char** argv)
{
#if defined(MAGMA_USE_SHIM)
    VulkanShimInit();
#endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(Vulkan, Readback)
{
    VkReadbackTest test;
    ASSERT_TRUE(test.Initialize());
    ASSERT_TRUE(test.Exec());
    ASSERT_TRUE(test.Readback());
}

TEST(Vulkan, ManyReadback)
{
    std::vector<std::unique_ptr<VkReadbackTest>> tests;
    // This should be limited by the number of FDs in use. The maximum number of FDs is 256
    // (FDIO_MAX_FD), and the Intel mesa driver uses 2 per VkPhysicalDevice and 1 per VkDevice.
    for (uint32_t i = 0; i < 75; i++) {
        tests.push_back(std::make_unique<VkReadbackTest>());
        ASSERT_TRUE(tests.back()->Initialize());
        ASSERT_TRUE(tests.back()->Exec());
    }
    for (auto& test : tests) {
        ASSERT_TRUE(test->Readback());
    }
}
