// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/unwind.h"

#include <lib/zx/process.h>
#include <lib/zx/suspend_token.h>

#include <condition_variable>
#include <thread>

#include "gtest/gtest.h"

namespace debug_agent {

namespace {

// This would be simpler using a mutex instead of the condition variable since
// there are only two threads, but the Clang lock checker gets very upset.
struct ThreadData {
  std::mutex mutex;

  // Set by thread itself before thread_ready is signaled.
  // zx::thread::native_handle doesn't seem to do what we want.
  zx::thread thread;

  bool thread_ready = false;
  std::condition_variable thread_ready_cv;

  bool backtrace_done = false;
  std::condition_variable backtrace_done_cv;
};

void __attribute__((noinline)) ThreadFunc2(ThreadData* data) {
  // Tell the main thread we're ready for backtrace computation.
  data->thread_ready = true;
  data->thread_ready_cv.notify_one();

  // Block until the backtrace is done being completed.
  std::unique_lock<std::mutex> lock(data->mutex);
  if (!data->backtrace_done) {
    data->backtrace_done_cv.wait(lock,
                                 [data]() { return data->backtrace_done; });
  }
}

void __attribute__((noinline)) ThreadFunc1(ThreadData* data) {
  // Fill in our thread handle.
  zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &data->thread);

  // Put another function on the stack.
  ThreadFunc2(data);

  // This doesn't do anything useful but we need some code the compiler can't
  // remove after the ThreadFunc2 call to ensure the compiler doesn't optimize
  // out the return.
  data->thread_ready_cv.notify_one();
}

// Synchronously suspends the thread. Returns a valid suspend token on success.
zx::suspend_token SyncSuspendThread(zx::thread& thread) {
  zx::suspend_token token;
  zx_status_t status = thread.suspend(&token);
  EXPECT_EQ(ZX_OK, status);

  zx_signals_t observed = 0;
  status = thread.wait_one(ZX_THREAD_SUSPENDED,
                           zx::deadline_after(zx::msec(100)), &observed);
  EXPECT_TRUE(observed & ZX_THREAD_SUSPENDED);
  if (status != ZX_OK)
    return zx::suspend_token();

  return token;
}

void DoUnwindTest() {
  ThreadData data;
  std::thread background(ThreadFunc1, &data);

  // Wait until the background thread is ready for the backtrace.
  std::vector<debug_ipc::StackFrame> stack;
  {
    std::unique_lock<std::mutex> lock(data.mutex);
    if (!data.thread_ready)
      data.thread_ready_cv.wait(lock, [&data]() { return data.thread_ready; });

    // Thread query functions require it to be suspended.
    zx::suspend_token suspend = SyncSuspendThread(data.thread);

    // Get the registers for the unwinder.
    zx_thread_state_general_regs regs;
    zx_status_t status = data.thread.read_state(ZX_THREAD_STATE_GENERAL_REGS,
                                                &regs, sizeof(regs));
    ASSERT_EQ(ZX_OK, status);

    // The debug addr is necessary to find the unwind information.
    uintptr_t debug_addr = 0;
    status = zx::process::self()->get_property(ZX_PROP_PROCESS_DEBUG_ADDR,
                                               &debug_addr, sizeof(debug_addr));
    ASSERT_EQ(ZX_OK, status);
    ASSERT_NE(0u, debug_addr);

    // Do the unwinding.
    status = UnwindStack(*zx::process::self(), debug_addr, data.thread, regs,
                         16, &stack);
    ASSERT_EQ(ZX_OK, status);

    data.backtrace_done = true;
  }

  // Tell the background thread it can complete.
  data.backtrace_done_cv.notify_one();
  background.join();

  // Validate the stack. It's really hard to say what these values will be
  // without symbols given the few guarantees C++ can provide. But we should
  // have "several" entries and the first one should have "a bunch" of
  // registers.
  ASSERT_TRUE(stack.size() >= 2) << "Only got " << stack.size();
  EXPECT_TRUE(stack[0].ip != 0);
  EXPECT_TRUE(stack[0].regs.size() >= 8);

  // TODO: It might be nice to write the thread functions in assembly so we can
  // know what the addresses are supposed to be.
}

}  // namespace

#if !defined(__aarch64__)
// TODO(brettw) This test fails on ARM for unknown reasons. The unwinder
// reports only one stack frame. This will happen if the AOSP unwinder can't
// find the libraries, and hence the unwind information from them so it could
// be related to that.
TEST(Unwind, Android) {
  SetUnwinderType(UnwinderType::kAndroid);
  DoUnwindTest();
}
#endif

TEST(Unwind, NG) {
  SetUnwinderType(UnwinderType::kNgUnwind);
  DoUnwindTest();
}

}  // namespace debug_agent
