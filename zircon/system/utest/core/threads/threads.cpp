// Copyright 2016 The Fuchsia Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <unistd.h>

#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

#include <runtime/thread.h>
#include <unittest/unittest.h>

#include <lib/zx/vmo.h>

#include "register-set.h"
#include "thread-functions/thread-functions.h"

static const char kThreadName[] = "test-thread";

static const unsigned kExceptionPortKey = 42u;

// We have to poll a thread's state as there is no way to wait for it to
// transition states. Wait this amount of time. Generally the thread won't
// take very long so this is a compromise between polling too frequently and
// waiting too long.
constexpr zx_duration_t THREAD_BLOCKED_WAIT_DURATION = ZX_MSEC(1);

static bool get_koid(zx_handle_t handle, zx_koid_t* koid) {
    zx_info_handle_basic_t info;
    size_t records_read;
    ASSERT_EQ(zx_object_get_info(
                  handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info),
                  &records_read, NULL),
              ZX_OK);
    ASSERT_EQ(records_read, 1u);
    *koid = info.koid;
    return true;
}

static bool check_reported_pid_and_tid(zx_handle_t thread,
                                       zx_port_packet_t* packet) {
    zx_koid_t pid;
    zx_koid_t tid;
    if (!get_koid(zx_process_self(), &pid))
        return false;
    if (!get_koid(thread, &tid))
        return false;
    EXPECT_EQ(packet->exception.pid, pid);
    EXPECT_EQ(packet->exception.tid, tid);
    return true;
}

static bool get_thread_info(zx_handle_t thread, zx_info_thread_t* info) {
    return zx_object_get_info(thread, ZX_INFO_THREAD, info, sizeof(*info), NULL, NULL) == ZX_OK;
}

// Suspend the given thread and block until it reaches the suspended state. The suspend token
// is written to the output parameter.
static bool suspend_thread_synchronous(zx_handle_t thread, zx_handle_t* suspend_token) {
    ASSERT_EQ(zx_task_suspend_token(thread, suspend_token), ZX_OK);

    zx_signals_t observed = 0u;
    ASSERT_EQ(zx_object_wait_one(thread, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, &observed), ZX_OK);

    return true;
}

// Resume the given thread and block until it reaches the running state.
static bool resume_thread_synchronous(zx_handle_t thread, zx_handle_t suspend_token) {
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

    zx_signals_t observed = 0u;
    ASSERT_EQ(zx_object_wait_one(thread, ZX_THREAD_RUNNING, ZX_TIME_INFINITE, &observed), ZX_OK);

    return true;
}

// Updates the thread state to advance over a software breakpoint instruction, assuming the
// breakpoint was just hit. This does not resume the thread, only updates its state.
static bool advance_over_breakpoint(zx_handle_t thread) {
#if defined(__aarch64__)
    // Advance 4 bytes to the next instruction after the debug break.
    zx_thread_state_general_regs regs;
    ASSERT_EQ(zx_thread_read_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
              ZX_OK);
    regs.pc += 4;
    ASSERT_EQ(zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)),
              ZX_OK);
#elif defined(__x86_64__)
// x86 sets the instruction pointer to the following instruction so needs no update.
#else
#error Not supported on this platform.
#endif
    return true;
}

// Waits for the exception type excp_type, ignoring exceptions of type ignore_type (these will
// just resume the thread), and issues errors for anything else.
static bool wait_thread_excp_type(zx_handle_t thread, zx_handle_t eport, uint32_t excp_type,
                                  uint32_t ignore_type) {
    zx_port_packet_t packet;
    while (true) {
        ASSERT_EQ(zx_port_wait(eport, ZX_TIME_INFINITE, &packet), ZX_OK);
        ASSERT_EQ(packet.key, kExceptionPortKey);
        if (packet.type != ignore_type) {
            ASSERT_EQ(packet.type, excp_type);
            break;
        } else {
            ASSERT_EQ(zx_task_resume_from_exception(thread, eport, 0), ZX_OK);
        }
    }
    return true;
}

namespace {

// Class to encapsulate the various handles and calculations required to start a thread.
//
// This is only necessary to use directly if you need to do something between creating
// and starting the thread - otherwise just use start_thread() for simplicity.
class ThreadStarter {
public:
    bool CreateThread(zxr_thread_t* thread_out, zx_handle_t* thread_h,
                      bool start_suspended = false) {
        // TODO: Don't leak these when the thread dies.
        // If the thread should start suspended, give it a 0-size VMO for a stack so
        // that it will crash if it gets to userspace.
        ASSERT_EQ(zx::vmo::create(start_suspended ? 0 : kStackSize,
                                  ZX_VMO_RESIZABLE, &stack_handle_), ZX_OK);
        ASSERT_NE(stack_handle_.get(), ZX_HANDLE_INVALID);

        ASSERT_EQ(zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                              0, stack_handle_.get(), 0, kStackSize, &stack_),
                  ZX_OK);

        ASSERT_EQ(zxr_thread_create(zx_process_self(), "test_thread", false,
                                    thread_out),
                  ZX_OK);
        thread_ = thread_out;

        if (thread_h) {
            ASSERT_EQ(zx_handle_duplicate(zxr_thread_get_handle(thread_out), ZX_RIGHT_SAME_RIGHTS,
                                          thread_h),
                      ZX_OK);
        }

        return true;
    }

    bool GrowStackVmo() {
        ASSERT_EQ(stack_handle_.set_size(kStackSize), ZX_OK);
        return true;
    }

    bool StartThread(zxr_thread_entry_t entry, void* arg) {
        return zxr_thread_start(thread_, stack_, kStackSize, entry, arg) == ZX_OK;
    }

private:
    static constexpr size_t kStackSize = 256u << 10;

    zx::vmo stack_handle_;

    uintptr_t stack_ = 0u;
    zxr_thread_t* thread_ = nullptr;
};

} // namespace

static bool start_thread(zxr_thread_entry_t entry, void* arg,
                         zxr_thread_t* thread_out, zx_handle_t* thread_h) {
    ThreadStarter starter;
    return starter.CreateThread(thread_out, thread_h) && starter.StartThread(entry, arg);
}

static bool start_and_kill_thread(zxr_thread_entry_t entry, void* arg) {
    zxr_thread_t thread;
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(entry, arg, &thread, &thread_h));
    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL),
              ZX_OK);
    zxr_thread_destroy(&thread);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
    return true;
}

static bool set_debugger_exception_port(zx_handle_t* eport_out) {
    ASSERT_EQ(zx_port_create(0, eport_out), ZX_OK);
    zx_handle_t self = zx_process_self();
    ASSERT_EQ(zx_task_bind_exception_port(self, *eport_out, kExceptionPortKey,
                                          ZX_EXCEPTION_PORT_DEBUGGER),
              ZX_OK);
    return true;
}

static void clear_debugger_exception_port() {
    zx_handle_t self = zx_process_self();
    zx_task_bind_exception_port(self, ZX_HANDLE_INVALID, kExceptionPortKey,
                                ZX_EXCEPTION_PORT_DEBUGGER);
}

// Wait for |thread| to enter blocked state |reason|.
// We wait forever and let Unittest's watchdog handle errors.

static bool wait_thread_blocked(zx_handle_t thread, uint32_t reason) {
    while (true) {
        zx_info_thread_t info;
        ASSERT_TRUE(get_thread_info(thread, &info));
        if (info.state == reason)
            break;
        zx_nanosleep(zx_deadline_after(THREAD_BLOCKED_WAIT_DURATION));
    }
    return true;
}

static bool TestBasics() {
    BEGIN_TEST;
    zxr_thread_t thread;
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)zx_deadline_after(ZX_MSEC(100)),
                             &thread, &thread_h));
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
    END_TEST;
}

static bool TestInvalidRights() {
    BEGIN_TEST;
    zxr_thread_t thread;
    zx_handle_t ro_process_h;

    ASSERT_EQ(zx_handle_duplicate(zx_process_self(), ZX_RIGHT_DESTROY, &ro_process_h), ZX_OK);
    ASSERT_EQ(zxr_thread_create(ro_process_h, "test_thread", false, &thread),
              ZX_ERR_ACCESS_DENIED);

    ASSERT_EQ(zx_handle_close(ro_process_h), ZX_OK);
    END_TEST;
}

static bool TestDetach() {
    BEGIN_TEST;
    zxr_thread_t thread;
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0, &event), ZX_OK);

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_wait_detach_fn, &event, &thread, &thread_h));
    // We're not detached yet
    ASSERT_FALSE(zxr_thread_detached(&thread));

    ASSERT_EQ(zxr_thread_detach(&thread), ZX_OK);
    ASSERT_TRUE(zxr_thread_detached(&thread));

    // Tell thread to exit
    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);

    // Wait for thread to exit
    ASSERT_EQ(zx_object_wait_one(thread_h,
                                 ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

static bool TestLongNameSucceeds() {
    BEGIN_TEST;
    // Creating a thread with a super long name should succeed.
    static const char long_name[] =
        "0123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789";
    ASSERT_GT(strlen(long_name), (size_t)ZX_MAX_NAME_LEN - 1,
              "too short to truncate");

    zxr_thread_t thread;
    ASSERT_EQ(zxr_thread_create(zx_process_self(), long_name, false, &thread),
              ZX_OK);
    zxr_thread_destroy(&thread);
    END_TEST;
}

// zx_thread_start() is not supposed to be usable for creating a
// process's first thread.  That's what zx_process_start() is for.
// Check that zx_thread_start() returns an error in this case.
static bool TestThreadStartOnInitialThread() {
    BEGIN_TEST;

    static const char kProcessName[] = "test-proc-thread1";
    zx_handle_t process;
    zx_handle_t vmar;
    zx_handle_t thread;
    ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar),
              ZX_OK);
    ASSERT_EQ(zx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread),
              ZX_OK);
    ASSERT_EQ(zx_thread_start(thread, 1, 1, 1, 1), ZX_ERR_BAD_STATE);

    ASSERT_EQ(zx_handle_close(thread), ZX_OK);
    ASSERT_EQ(zx_handle_close(vmar), ZX_OK);
    ASSERT_EQ(zx_handle_close(process), ZX_OK);

    END_TEST;
}

// Test that we don't get an assertion failure (and kernel panic) if we
// pass a zero instruction pointer when starting a thread (in this case via
// zx_process_start()).
static bool TestThreadStartWithZeroInstructionPointer() {
    BEGIN_TEST;

    static const char kProcessName[] = "test-proc-thread2";
    zx_handle_t process;
    zx_handle_t vmar;
    zx_handle_t thread;
    ASSERT_EQ(zx_process_create(zx_job_default(), kProcessName, sizeof(kProcessName) - 1,
                                0, &process, &vmar),
              ZX_OK);
    ASSERT_EQ(zx_thread_create(process, kThreadName, sizeof(kThreadName) - 1,
                               0, &thread),
              ZX_OK);

    REGISTER_CRASH(process);
    ASSERT_EQ(zx_process_start(process, thread, 0, 0, thread, 0), ZX_OK);

    zx_signals_t signals;
    EXPECT_EQ(zx_object_wait_one(
                  process, ZX_TASK_TERMINATED, ZX_TIME_INFINITE, &signals),
              ZX_OK);
    signals &= ZX_TASK_TERMINATED;
    EXPECT_EQ(signals, ZX_TASK_TERMINATED);

    ASSERT_EQ(zx_handle_close(process), ZX_OK);
    ASSERT_EQ(zx_handle_close(vmar), ZX_OK);

    END_TEST;
}

static bool TestKillBusyThread() {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(threads_test_busy_fn, NULL));

    END_TEST;
}

static bool TestKillSleepThread() {
    BEGIN_TEST;

    ASSERT_TRUE(start_and_kill_thread(threads_test_infinite_sleep_fn, NULL));

    END_TEST;
}

static bool TestKillWaitThread() {
    BEGIN_TEST;

    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
    ASSERT_TRUE(start_and_kill_thread(threads_test_infinite_wait_fn, &event));
    ASSERT_EQ(zx_handle_close(event), ZX_OK);

    END_TEST;
}

static bool TestNonstartedThread() {
    BEGIN_TEST;

    // Perform apis against non started threads (in the INITIAL STATE).
    zx_handle_t thread;

    ASSERT_EQ(zx_thread_create(zx_process_self(), "thread", 5, 0, &thread), ZX_OK);
    ASSERT_EQ(zx_task_kill(thread), ZX_OK);
    ASSERT_EQ(zx_task_kill(thread), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread), ZX_OK);

    END_TEST;
}

// Arguments for self_killing_fn().
struct self_killing_thread_args {
    zxr_thread_t thread; // Used for the thread to kill itself.
    uint32_t test_value; // Used for testing what the thread does.
};

__NO_SAFESTACK static void self_killing_fn(void* arg) {
    self_killing_thread_args* args = static_cast<self_killing_thread_args*>(arg);
    // Kill the current thread.
    zx_task_kill(zxr_thread_get_handle(&args->thread));
    // We should not reach here -- the syscall should not have returned.
    args->test_value = 999;
    zx_thread_exit();
}

// This tests that the zx_task_kill() syscall does not return when a thread
// uses it to kill itself.
static bool TestThreadKillsItself() {
    BEGIN_TEST;

    self_killing_thread_args args;
    args.test_value = 111;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(self_killing_fn, &args, &args.thread, &thread_handle));
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL),
              ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_handle), ZX_OK);
    // Check that the thread did not continue execution and modify test_value.
    ASSERT_EQ(args.test_value, 111u);
    // We have to destroy the thread afterwards to clean up its internal
    // handle, since it did not properly exit.
    zxr_thread_destroy(&args.thread);

    END_TEST;
}

static bool TestInfoTaskStatsFails() {
    BEGIN_TEST;
    // Spin up a thread.
    zxr_thread_t thread;
    zx_handle_t thandle;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)zx_deadline_after(ZX_MSEC(100)), &thread,
                             &thandle));
    ASSERT_EQ(zx_object_wait_one(thandle,
                                 ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    // Ensure that task_stats doesn't work on it.
    zx_info_task_stats_t info;
    EXPECT_NE(zx_object_get_info(thandle, ZX_INFO_TASK_STATS,
                                 &info, sizeof(info), NULL, NULL),
              ZX_OK,
              "Just added thread support to info_task_status?");
    // If so, replace this with a real test; see example in process.cpp.

    ASSERT_EQ(zx_handle_close(thandle), ZX_OK);
    END_TEST;
}

static bool TestResumeSuspended() {
    BEGIN_TEST;

    zx_handle_t event;
    zxr_thread_t thread;
    zx_handle_t thread_h;

    ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_h));

    // threads_test_wait_fn() uses zx_object_wait_one() so we watch for that.
    ASSERT_TRUE(wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

    // The thread should still be blocked on the event when it wakes up.
    // It needs to run for a bit to transition from suspended back to blocked
    // so we need to wait for it.
    ASSERT_TRUE(wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

    // Check that signaling the event while suspended results in the expected behavior.
    suspend_token = ZX_HANDLE_INVALID;
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, &suspend_token));

    // Verify thread is suspended.
    zx_info_thread_t info;
    ASSERT_TRUE(get_thread_info(thread_h, &info));
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);
    ASSERT_EQ(info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_NONE);

    // Resuming the thread should mark the thread as blocked again.
    ASSERT_TRUE(resume_thread_synchronous(thread_h, suspend_token));

    ASSERT_TRUE(wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

    // When the thread is suspended the signaling should not take effect.
    suspend_token = ZX_HANDLE_INVALID;
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, &suspend_token));
    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, zx_deadline_after(ZX_MSEC(100)), NULL), ZX_ERR_TIMED_OUT);

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL), ZX_OK);

    ASSERT_EQ(zx_object_wait_one(
                  thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    ASSERT_EQ(zx_handle_close(event), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

static bool TestSuspendSleeping() {
    BEGIN_TEST;

    const zx_time_t sleep_deadline = zx_deadline_after(ZX_MSEC(100));
    zxr_thread_t thread;

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_sleep_fn, (void*)sleep_deadline, &thread, &thread_h));

    zx_nanosleep(sleep_deadline - ZX_MSEC(50));

    // Suspend the thread.
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    zx_status_t status = zx_task_suspend_token(thread_h, &suspend_token);
    if (status != ZX_OK) {
        ASSERT_EQ(status, ZX_ERR_BAD_STATE);
        // This might happen if the thread exits before we tried suspending it
        // (due to e.g. a long context-switch away).  The system is too loaded
        // and so we might not have a chance at success here without a massive
        // sleep duration.
        zx_info_thread_t info;
        ASSERT_EQ(zx_object_get_info(thread_h, ZX_INFO_THREAD,
                                     &info, sizeof(info), NULL, NULL),
                  ZX_OK);
        ASSERT_EQ(info.state, ZX_THREAD_STATE_DEAD);
        ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
        // Early bail from the test, since we hit a possible race from an
        // overloaded machine.
        END_TEST;
    }
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, NULL), ZX_OK);
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

    // Wait for the sleep to finish
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    const zx_time_t now = zx_clock_get_monotonic();
    ASSERT_GE(now, sleep_deadline, "thread did not sleep long enough");

    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
    END_TEST;
}

static bool TestSuspendChannelCall() {
    BEGIN_TEST;

    zxr_thread_t thread;

    zx_handle_t channel;
    channel_call_suspend_test_arg thread_arg;
    ASSERT_EQ(zx_channel_create(0, &thread_arg.channel, &channel), ZX_OK);
    thread_arg.call_status = ZX_ERR_BAD_STATE;

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_channel_call_fn, &thread_arg, &thread, &thread_h));

    // Wait for the thread to send a channel call before suspending it
    ASSERT_EQ(zx_object_wait_one(channel, ZX_CHANNEL_READABLE, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    // Suspend the thread.
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, &suspend_token));

    // Read the message
    uint8_t buf[9];
    uint32_t actual_bytes;
    ASSERT_EQ(zx_channel_read(channel, 0, buf, NULL, sizeof(buf), 0, &actual_bytes, NULL),
              ZX_OK);
    ASSERT_EQ(actual_bytes, sizeof(buf));
    ASSERT_EQ(memcmp(buf + sizeof(zx_txid_t), &"abcdefghi"[sizeof(zx_txid_t)],
                     sizeof(buf) - sizeof(zx_txid_t)), 0);

    // Write a reply
    buf[8] = 'j';
    ASSERT_EQ(zx_channel_write(channel, 0, buf, sizeof(buf), NULL, 0), ZX_OK);

    // Make sure the remote channel didn't get signaled
    EXPECT_EQ(zx_object_wait_one(thread_arg.channel, ZX_CHANNEL_READABLE, 0, NULL),
              ZX_ERR_TIMED_OUT);

    // Make sure we can't read from the remote channel (the message should have
    // been reserved for the other thread, even though it is suspended).
    EXPECT_EQ(zx_channel_read(thread_arg.channel, 0, buf, NULL, sizeof(buf), 0,
                              &actual_bytes, NULL),
              ZX_ERR_SHOULD_WAIT);

    // Wake the suspended thread
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

    // Wait for the thread to finish
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);
    EXPECT_EQ(thread_arg.call_status, ZX_OK);

    ASSERT_EQ(zx_handle_close(channel), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

static bool TestSuspendPortCall() {
    BEGIN_TEST;

    zxr_thread_t thread;
    zx_handle_t port[2];
    ASSERT_EQ(zx_port_create(0, &port[0]), ZX_OK);
    ASSERT_EQ(zx_port_create(0, &port[1]), ZX_OK);

    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_port_fn, port, &thread, &thread_h));

    zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);

    zx_port_packet_t packet1 = {100ull, ZX_PKT_TYPE_USER, 0u, {}};
    zx_port_packet_t packet2 = {300ull, ZX_PKT_TYPE_USER, 0u, {}};

    ASSERT_EQ(zx_port_queue(port[0], &packet1), ZX_OK);
    ASSERT_EQ(zx_port_queue(port[0], &packet2), ZX_OK);

    zx_port_packet_t packet;
    ASSERT_EQ(zx_port_wait(port[1], zx_deadline_after(ZX_MSEC(100)), &packet), ZX_ERR_TIMED_OUT);

    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

    ASSERT_EQ(zx_port_wait(port[1], ZX_TIME_INFINITE, &packet), ZX_OK);
    EXPECT_EQ(packet.key, 105ull);

    ASSERT_EQ(zx_port_wait(port[0], ZX_TIME_INFINITE, &packet), ZX_OK);
    EXPECT_EQ(packet.key, 300ull);

    ASSERT_EQ(zx_object_wait_one(
                  thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);
    ASSERT_EQ(zx_handle_close(port[0]), ZX_OK);
    ASSERT_EQ(zx_handle_close(port[1]), ZX_OK);

    END_TEST;
}

struct TestWritingThreadArg {
    volatile int v;
};

__NO_SAFESTACK static void TestWritingThreadFn(void* arg_) {
    TestWritingThreadArg* arg = static_cast<TestWritingThreadArg*>(arg_);
    while (true) {
        arg->v = 1;
    }
    __builtin_trap();
}

static bool TestSuspendStopsThread() {
    BEGIN_TEST;

    zxr_thread_t thread;

    TestWritingThreadArg arg = {.v = 0};
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(TestWritingThreadFn, &arg, &thread, &thread_h));

    while (arg.v != 1) {
        zx_nanosleep(0);
    }

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);
    while (arg.v != 2) {
        arg.v = 2;
        // Give the thread a chance to clobber the value
        zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
    }
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    while (arg.v != 1) {
        zx_nanosleep(0);
    }

    // Clean up.
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
    // Wait for the thread termination to complete.  We should do this so
    // that any later tests which use set_debugger_exception_port() do not
    // receive an ZX_EXCP_THREAD_EXITING event.
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL),
              ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

static bool TestSuspendMultiple() {
    BEGIN_TEST;

    zx_handle_t event;
    zxr_thread_t thread;
    zx_handle_t thread_h;

    ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
    ASSERT_TRUE(start_thread(threads_test_wait_break_infinite_sleep_fn, &event, &thread,
                             &thread_h));

    // The thread will now be blocked on the event. Wake it up and catch the trap (undefined
    // exception).
    zx_handle_t exception_port = ZX_HANDLE_INVALID;
    ASSERT_TRUE(set_debugger_exception_port(&exception_port));
    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
    ASSERT_TRUE(wait_thread_excp_type(thread_h, exception_port, ZX_EXCP_SW_BREAKPOINT,
                                      ZX_EXCP_THREAD_STARTING));

    // The thread should now be blocked on a debugger exception.
    ASSERT_TRUE(wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_EXCEPTION));
    zx_info_thread_t info;
    ASSERT_TRUE(get_thread_info(thread_h, &info));
    ASSERT_EQ(info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_DEBUGGER);

    advance_over_breakpoint(thread_h);

    // Suspend twice (on top of the existing exception). Don't use the synchronous suspend since
    // suspends don't escape out of exception handling, unlike blocking
    // syscalls where suspend will escape out of them.
    zx_handle_t suspend_token1 = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token1), ZX_OK);
    zx_handle_t suspend_token2 = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token2), ZX_OK);

    // Resume one token, it should remain blocked.
    ASSERT_EQ(zx_handle_close(suspend_token1), ZX_OK);
    ASSERT_TRUE(get_thread_info(thread_h, &info));
    // Note: If this check is flaky, it's failing. It should not transition out of the blocked
    // state, but if it does so, it will do so asynchronously which might cause
    // nondeterministic failures.
    ASSERT_EQ(info.state, ZX_THREAD_STATE_BLOCKED_EXCEPTION);

    // Resume from the exception with invalid options.
    ASSERT_EQ(zx_task_resume_from_exception(thread_h, exception_port, 23), ZX_ERR_INVALID_ARGS);

    // Resume the exception. It should be SUSPENDED now that the exception is complete (one could
    // argue that it could still be BLOCKED also, but it's not in the current implementation).
    // The transition to SUSPENDED happens asynchronously unlike some of the exception states.
    ASSERT_EQ(zx_task_resume_from_exception(thread_h, exception_port, 0), ZX_OK);
    zx_signals_t observed = 0u;
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, &observed),
              ZX_OK);

    ASSERT_TRUE(get_thread_info(thread_h, &info));
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);

    // 2nd resume, should be running or sleeping after this.
    ASSERT_TRUE(resume_thread_synchronous(thread_h, suspend_token2));
    ASSERT_TRUE(get_thread_info(thread_h, &info));
    ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING || info.state == ZX_THREAD_STATE_BLOCKED_SLEEPING);

    // Clean up.
    clear_debugger_exception_port();
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

static bool TestSuspendSelf() {
    BEGIN_TEST;

    zx_handle_t suspend_token;
    EXPECT_EQ(zx_task_suspend(zx_thread_self(), &suspend_token), ZX_ERR_NOT_SUPPORTED);

    END_TEST;
}

static bool TestSuspendAfterDeath() {
    BEGIN_TEST;

    zxr_thread_t thread;
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(threads_test_infinite_sleep_fn, nullptr, &thread, &thread_h));
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    EXPECT_EQ(zx_task_suspend(thread_h, &suspend_token), ZX_ERR_BAD_STATE);
    EXPECT_EQ(suspend_token, ZX_HANDLE_INVALID);

    EXPECT_EQ(zx_handle_close(suspend_token), ZX_OK);
    EXPECT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

// This tests for a bug in which killing a suspended thread causes the
// thread to be resumed and execute more instructions in userland.
static bool TestKillSuspendedThread() {
    BEGIN_TEST;

    zxr_thread_t thread;
    TestWritingThreadArg arg = {.v = 0};
    zx_handle_t thread_h;
    ASSERT_TRUE(start_thread(TestWritingThreadFn, &arg, &thread, &thread_h));

    // Wait until the thread has started and has modified arg.v.
    while (arg.v != 1) {
        zx_nanosleep(0);
    }

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, &suspend_token));

    // Attach to debugger port so we can see ZX_EXCP_THREAD_EXITING.
    zx_handle_t eport;
    ASSERT_TRUE(set_debugger_exception_port(&eport));

    // Reset the test memory location.
    arg.v = 100;
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
    // Wait for the thread termination to complete.
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL),
              ZX_OK);
    // Check for the bug.  The thread should not have resumed execution and
    // so should not have modified arg.v.
    EXPECT_EQ(arg.v, 100);

    // Check that the thread is reported as exiting and not as resumed.
    ASSERT_TRUE(wait_thread_excp_type(thread_h, eport, ZX_EXCP_THREAD_EXITING, 0));

    // Clean up.
    clear_debugger_exception_port();
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_EQ(zx_handle_close(eport), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

// Suspend a thread before starting and make sure it starts into suspended state.
static bool TestStartSuspendedThread() {
    BEGIN_TEST;

    zxr_thread_t thread;
    zx_handle_t thread_h;
    ThreadStarter starter;
    ASSERT_TRUE(starter.CreateThread(&thread, &thread_h, true));

    // Suspend first, then start the thread.
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend(thread_h, &suspend_token), ZX_OK);

    TestWritingThreadArg arg = {.v = 0};
    ASSERT_TRUE(starter.StartThread(TestWritingThreadFn, &arg));

    // Make sure the thread goes directly to suspended state without executing at all.
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_SUSPENDED, ZX_TIME_INFINITE, NULL), ZX_OK);

    // Once we know it's suspended, give it a real stack.
    ASSERT_TRUE(starter.GrowStackVmo());

    // Make sure the thread still resumes properly.
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_RUNNING, ZX_TIME_INFINITE, NULL), ZX_OK);
    while (arg.v != 1) {
        zx_nanosleep(0);
    }

    // Clean up.
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

// Suspend and resume a thread before starting, it should start as normal.
static bool TestStartSuspendedAndResumedThread() {
    BEGIN_TEST;

    zxr_thread_t thread;
    zx_handle_t thread_h;
    ThreadStarter starter;
    ASSERT_TRUE(starter.CreateThread(&thread, &thread_h));

    // Suspend and resume.
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend(thread_h, &suspend_token), ZX_OK);
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);

    // Start the thread, it should behave normally.
    TestWritingThreadArg arg = {.v = 0};
    ASSERT_TRUE(starter.StartThread(TestWritingThreadFn, &arg));
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_RUNNING, ZX_TIME_INFINITE, NULL), ZX_OK);
    while (arg.v != 1) {
        zx_nanosleep(0);
    }

    // Clean up.
    ASSERT_EQ(zx_task_kill(thread_h), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    END_TEST;
}

static bool port_wait_for_signal(zx_handle_t port, zx_handle_t thread,
                                      zx_time_t deadline, zx_signals_t mask,
                                      zx_port_packet_t* packet) {
    ASSERT_EQ(zx_object_wait_async(thread, port, 0u, mask,
                                   ZX_WAIT_ASYNC_ONCE),
              ZX_OK);
    ASSERT_EQ(zx_port_wait(port, deadline, packet), ZX_OK);
    ASSERT_EQ(packet->type, ZX_PKT_TYPE_SIGNAL_ONE);
    return true;
}

// Test signal delivery of suspended threads via async wait.
static bool TestSuspendWaitAsyncSignalDeliveryWorker(void) {
    zx_handle_t event;
    zx_handle_t port;
    zxr_thread_t thread;
    zx_handle_t thread_h;
    const zx_signals_t run_susp_mask = ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED;

    ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_h));

    ASSERT_EQ(zx_port_create(0, &port), ZX_OK);

    zx_port_packet_t packet;
    // There should be a RUNNING signal packet present and not SUSPENDED.
    // This is from when the thread first started to run.
    ASSERT_TRUE(port_wait_for_signal(port, thread_h, 0u, run_susp_mask, &packet));
    ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING);

    // Make sure there are no more packets.
    // RUNNING or SUSPENDED is always asserted.
    ASSERT_EQ(zx_object_wait_async(thread_h, port, 0u,
                                   ZX_THREAD_SUSPENDED,
                                   ZX_WAIT_ASYNC_ONCE),
              ZX_OK);
    ASSERT_EQ(zx_port_wait(port, 0u, &packet), ZX_ERR_TIMED_OUT);
    ASSERT_EQ(zx_port_cancel(port, thread_h, 0u), ZX_OK);

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_TRUE(suspend_thread_synchronous(thread_h, &suspend_token));

    zx_info_thread_t info;
    ASSERT_TRUE(get_thread_info(thread_h, &info));
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);

    ASSERT_TRUE(resume_thread_synchronous(thread_h, suspend_token));
    ASSERT_TRUE(get_thread_info(thread_h, &info));
    // At this point the thread may be running or blocked waiting for an
    // event. Either one is fine. threads_test_wait_fn() uses
    // zx_object_wait_one() so we watch for that.
    ASSERT_TRUE(info.state == ZX_THREAD_STATE_RUNNING ||
                    info.state == ZX_THREAD_STATE_BLOCKED_WAIT_ONE);

    // We should see just RUNNING,
    // and it should be immediately present (no deadline).
    ASSERT_TRUE(port_wait_for_signal(port, thread_h, 0u, run_susp_mask,
                                          &packet));
    ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING);

    // The thread should still be blocked on the event when it wakes up.
    ASSERT_TRUE(wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

    // Check that suspend/resume while blocked in a syscall results in
    // the expected behavior and is visible via async wait.
    suspend_token = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_task_suspend_token(thread_h, &suspend_token), ZX_OK);
    ASSERT_TRUE(port_wait_for_signal(port, thread_h,
                                     zx_deadline_after(ZX_MSEC(100)),
                                     ZX_THREAD_SUSPENDED, &packet));
    ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_SUSPENDED);

    ASSERT_TRUE(get_thread_info(thread_h, &info));
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_TRUE(port_wait_for_signal(port, thread_h,
                                     zx_deadline_after(ZX_MSEC(100)),
                                     ZX_THREAD_RUNNING, &packet));
    ASSERT_EQ(packet.signal.observed & run_susp_mask, ZX_THREAD_RUNNING);

    // Resumption from being suspended back into a blocking syscall will be
    // in the RUNNING state and then BLOCKED.
    ASSERT_TRUE(wait_thread_blocked(thread_h, ZX_THREAD_STATE_BLOCKED_WAIT_ONE));

    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE, NULL), ZX_OK);

    ASSERT_EQ(zx_object_wait_one(
                  thread_h, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL),
              ZX_OK);

    ASSERT_EQ(zx_handle_close(port), ZX_OK);
    ASSERT_EQ(zx_handle_close(event), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_h), ZX_OK);

    return true;
}

// Test signal delivery of suspended threads via single async wait.
static bool TestSuspendSingleWaitAsyncSignalDelivery() {
    BEGIN_TEST;
    EXPECT_TRUE(TestSuspendWaitAsyncSignalDeliveryWorker());
    END_TEST;
}

// Test signal delivery of suspended threads via repeating async wait.
static bool TestSuspendRepeatingWaitAsyncSignalDelivery() {
    BEGIN_TEST;
    EXPECT_TRUE(TestSuspendWaitAsyncSignalDeliveryWorker());
    END_TEST;
}

// Helper class for setting up a test for reading register state from a worker thread.
template <typename RegisterStruct>
class RegisterReadSetup {
public:
    using ThreadFunc = void (*)(RegisterStruct*);

    RegisterReadSetup() = default;
    ~RegisterReadSetup() {
        zx_handle_close(suspend_token_);
        zx_task_kill(thread_handle_);
        // Wait for the thread termination to complete.
        zx_object_wait_one(thread_handle_, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL);
        zx_handle_close(thread_handle_);
    }

    zx_handle_t thread_handle() const { return thread_handle_; }

    // Pass the thread function to run and the parameter to pass to it.
    bool Init(ThreadFunc thread_func, RegisterStruct* state) {
        BEGIN_HELPER;

        ASSERT_TRUE(start_thread((void (*)(void*))thread_func, state, &thread_, &thread_handle_));

        // Allow some time for the thread to begin execution and reach the
        // instruction that spins.
        ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(100))), ZX_OK);

        ASSERT_TRUE(Suspend());

        END_HELPER;
    }

    bool Resume() {
      return resume_thread_synchronous(thread_handle_, suspend_token_);
    }

    bool Suspend() {
      return suspend_thread_synchronous(thread_handle_, &suspend_token_);
    }


private:
    zxr_thread_t thread_;
    zx_handle_t thread_handle_ = ZX_HANDLE_INVALID;
    zx_handle_t suspend_token_ = ZX_HANDLE_INVALID;
};

// This tests the registers reported by zx_thread_read_state() for a
// suspended thread.  It starts a thread which sets all the registers to
// known test values.
static bool TestReadingGeneralRegisterState() {
    BEGIN_TEST;

    zx_thread_state_general_regs_t gen_regs_expected;
    general_regs_fill_test_values(&gen_regs_expected);
    gen_regs_expected.REG_PC = (uintptr_t)spin_with_general_regs_spin_address;

    RegisterReadSetup<zx_thread_state_general_regs_t> setup;
    ASSERT_TRUE(setup.Init(&spin_with_general_regs, &gen_regs_expected));

    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);
    ASSERT_TRUE(general_regs_expect_eq(regs, gen_regs_expected));

    END_TEST;
}

static bool TestReadingFpRegisterState() {
    BEGIN_TEST;

    zx_thread_state_fp_regs_t fp_regs_expected;
    fp_regs_fill_test_values(&fp_regs_expected);

    RegisterReadSetup<zx_thread_state_fp_regs_t> setup;
    ASSERT_TRUE(setup.Init(&spin_with_fp_regs, &fp_regs_expected));

    zx_thread_state_fp_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_FP_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);
    ASSERT_TRUE(fp_regs_expect_eq(regs, fp_regs_expected));

    END_TEST;
}

static bool TestReadingVectorRegisterState() {
    BEGIN_TEST;

    zx_thread_state_vector_regs_t vector_regs_expected;
    vector_regs_fill_test_values(&vector_regs_expected);

    RegisterReadSetup<zx_thread_state_vector_regs_t> setup;
    ASSERT_TRUE(setup.Init(&spin_with_vector_regs, &vector_regs_expected));

    zx_thread_state_vector_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);

    ASSERT_TRUE(vector_regs_expect_eq(regs, vector_regs_expected));

    END_TEST;
}

// Procedure:
//  1. Call Init() which will start a thread and suspend it.
//  2. Write the register state you want to the thread_handle().
//  3. Call DoSave with the save function and pointer. This will execute that code in the context of
//     the thread.
template <typename RegisterStruct>
class RegisterWriteSetup {
public:
    using SaveFunc = void (*)();

    RegisterWriteSetup() = default;
    ~RegisterWriteSetup() {
        zx_handle_close(thread_handle_);
    }

    zx_handle_t thread_handle() const { return thread_handle_; }

    bool Init() {
        BEGIN_HELPER;

        ASSERT_TRUE(start_thread(threads_test_busy_fn, nullptr, &thread_, &thread_handle_));
        // Allow some time for the thread to begin execution and reach the
        // instruction that spins.
        ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(100))), ZX_OK);
        ASSERT_TRUE(suspend_thread_synchronous(thread_handle_, &suspend_token_));

        END_HELPER;
    }

    // The IP and SP set in the general registers will be filled in to the optional output
    // parameters. This is for the general register test since we change those values out from
    // under it.
    bool DoSave(SaveFunc save_func, RegisterStruct* out,
                uint64_t* general_ip = nullptr, uint64_t* general_sp = nullptr) {
        BEGIN_HELPER;

        // Modify the PC to point to the routine, and the SP to point to the output struct.
        zx_thread_state_general_regs_t general_regs;
        ASSERT_EQ(zx_thread_read_state(thread_handle_, ZX_THREAD_STATE_GENERAL_REGS,
                                       &general_regs, sizeof(general_regs)),
                  ZX_OK);

        struct {
            // A small stack that is used for calling zx_thread_exit().
            char stack[1024] __ALIGNED(16);
            RegisterStruct regs_got; // STACK_PTR will point here.
        } stack;
        general_regs.REG_PC = (uintptr_t)save_func;
        general_regs.REG_STACK_PTR = (uintptr_t)(stack.stack + sizeof(stack.stack));
        ASSERT_EQ(zx_thread_write_state(thread_handle_, ZX_THREAD_STATE_GENERAL_REGS,
                                        &general_regs, sizeof(general_regs)),
                  ZX_OK);

        if (general_ip)
            *general_ip = general_regs.REG_PC;
        if (general_sp)
            *general_sp = general_regs.REG_STACK_PTR;

        // Unsuspend the thread and wait for it to finish executing, this will run the code
        // and fill the RegisterStruct we passed.
        ASSERT_EQ(zx_handle_close(suspend_token_), ZX_OK);
        suspend_token_ = ZX_HANDLE_INVALID;
        ASSERT_EQ(zx_object_wait_one(thread_handle_, ZX_THREAD_TERMINATED,
                                     ZX_TIME_INFINITE, NULL),
                  ZX_OK);

        memcpy(out, &stack.regs_got, sizeof(RegisterStruct));

        END_HELPER;
    }

private:
    zxr_thread_t thread_;
    zx_handle_t thread_handle_ = ZX_HANDLE_INVALID;
    zx_handle_t suspend_token_ = ZX_HANDLE_INVALID;
};

// This tests writing registers using zx_thread_write_state().  After
// setting registers using that syscall, it reads back the registers and
// checks their values.
static bool TestWritingGeneralRegisterState() {
    BEGIN_TEST;

    RegisterWriteSetup<zx_thread_state_general_regs_t> setup;
    ASSERT_TRUE(setup.Init());

    // Set the general registers.
    zx_thread_state_general_regs_t regs_to_set;
    general_regs_fill_test_values(&regs_to_set);
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs_to_set, sizeof(regs_to_set)),
              ZX_OK);

    zx_thread_state_general_regs_t regs;
    uint64_t ip = 0, sp = 0;
    ASSERT_TRUE(setup.DoSave(&save_general_regs_and_exit_thread, &regs, &ip, &sp));

    // Fix up the expected values with the IP/SP required for the register read.
    regs_to_set.REG_PC = ip;
    regs_to_set.REG_STACK_PTR = sp;
    EXPECT_TRUE(general_regs_expect_eq(regs_to_set, regs));

    END_TEST;
}

static bool TestWritingFpRegisterState() {
    BEGIN_TEST;

    RegisterWriteSetup<zx_thread_state_fp_regs_t> setup;
    ASSERT_TRUE(setup.Init());

    // The busyloop code executed initially by the setup class will have executed an MMX instruction
    // so that the MMX state is available to write.
    zx_thread_state_fp_regs_t regs_to_set;
    fp_regs_fill_test_values(&regs_to_set);
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_FP_REGS,
                                    &regs_to_set, sizeof(regs_to_set)),
              ZX_OK);

    zx_thread_state_fp_regs_t regs;
    ASSERT_TRUE(setup.DoSave(&save_fp_regs_and_exit_thread, &regs));
    EXPECT_TRUE(fp_regs_expect_eq(regs_to_set, regs));

    END_TEST;
}

static bool TestWritingVectorRegisterState() {
    BEGIN_TEST;

    RegisterWriteSetup<zx_thread_state_vector_regs_t> setup;
    ASSERT_TRUE(setup.Init());

    zx_thread_state_vector_regs_t regs_to_set;
    vector_regs_fill_test_values(&regs_to_set);
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_VECTOR_REGS,
                                    &regs_to_set, sizeof(regs_to_set)),
              ZX_OK);

    zx_thread_state_vector_regs_t regs;
    ASSERT_TRUE(setup.DoSave(&save_vector_regs_and_exit_thread, &regs));
    EXPECT_TRUE(vector_regs_expect_eq(regs_to_set, regs));

    END_TEST;
}

// This test starts a thread which reads and writes from TLS.
static bool TestThreadLocalRegisterState() {
    BEGIN_TEST;

    RegisterWriteSetup<struct thread_local_regs> setup;
    ASSERT_TRUE(setup.Init());

    zx_thread_state_general_regs_t regs;

#if defined(__x86_64__)
    // The thread will read these from the fs and gs base addresses
    // into the output regs struct, and then write different numbers.
    uint64_t fs_base_value = 0x1234;
    uint64_t gs_base_value = 0x5678;
    regs.fs_base = (uintptr_t)&fs_base_value;
    regs.gs_base = (uintptr_t)&gs_base_value;
#elif defined(__aarch64__)
    uint64_t tpidr_value = 0x1234;
    regs.tpidr = (uintptr_t)&tpidr_value;
#endif

    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs, sizeof(regs)),
              ZX_OK);

    // TODO(tbodt): Remove once support for the old sizes is removed from the kernel.
    // Test that writing using the old size for the struct does not write the new members.
    // Do this by setting them to bogus values that will cause a page fault if used.
#if defined(__x86_64__)
    regs.fs_base = 0;
    regs.gs_base = 0;
#elif defined(__aarch64__)
    regs.tpidr = 0;
#endif
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs, sizeof(__old_zx_thread_state_general_regs_t)),
              ZX_OK);
    // Test that reading using the old size for the struct does not read the new members.
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(__old_zx_thread_state_general_regs_t)),
              ZX_OK);
#if defined(__x86_64__)
    ASSERT_EQ(regs.fs_base, 0);
    ASSERT_EQ(regs.gs_base, 0);
#elif defined(__aarch64__)
    ASSERT_EQ(regs.tpidr, 0);
#endif

    struct thread_local_regs tls_regs;
    ASSERT_TRUE(setup.DoSave(&save_thread_local_regs_and_exit_thread, &tls_regs));

#if defined(__x86_64__)
    EXPECT_EQ(tls_regs.fs_base_value, 0x1234);
    EXPECT_EQ(tls_regs.gs_base_value, 0x5678);
    EXPECT_EQ(fs_base_value, 0x12345678);
    EXPECT_EQ(gs_base_value, 0x7890abcd);
#elif defined(__aarch64__)
    EXPECT_EQ(tls_regs.tpidr_value, 0x1234);
    EXPECT_EQ(tpidr_value, 0x12345678);
#endif

    END_TEST;
}

#if defined(__x86_64__)

#include <cpuid.h>

// This is based on code from kernel/ which isn't usable by code in system/.
enum { X86_CPUID_ADDR_WIDTH = 0x80000008 };

static uint32_t x86_linear_address_width() {
    uint32_t eax, ebx, ecx, edx;
    __cpuid(X86_CPUID_ADDR_WIDTH, eax, ebx, ecx, edx);
    return (eax >> 8) & 0xff;
}

#endif

// Test that zx_thread_write_state() does not allow setting RIP to a
// non-canonical address for a thread that was suspended inside a syscall,
// because if the kernel returns to that address using SYSRET, that can
// cause a fault in kernel mode that is exploitable.  See
// sysret_problem.md.
static bool TestNoncanonicalRipAddress() {
    BEGIN_TEST;

#if defined(__x86_64__)
    zx_handle_t event;
    ASSERT_EQ(zx_event_create(0, &event), ZX_OK);
    zxr_thread_t thread;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(threads_test_wait_fn, &event, &thread, &thread_handle));

    // Allow some time for the thread to begin execution and block inside
    // the syscall.
    ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(100))), ZX_OK);

    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_TRUE(suspend_thread_synchronous(thread_handle, &suspend_token));

    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);

    // Example addresses to test.
    uintptr_t noncanonical_addr =
        ((uintptr_t)1) << (x86_linear_address_width() - 1);
    uintptr_t canonical_addr = noncanonical_addr - 1;
    uint64_t kKernelAddr = 0xffff800000000000;

    zx_thread_state_general_regs_t regs_modified = regs;

    // This RIP address must be disallowed.
    regs_modified.rip = noncanonical_addr;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs_modified, sizeof(regs_modified)),
              ZX_ERR_INVALID_ARGS);

    regs_modified.rip = canonical_addr;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs_modified, sizeof(regs_modified)),
              ZX_OK);

    // This RIP address does not need to be disallowed, but it is currently
    // disallowed because this simplifies the check and it's not useful to
    // allow this address.
    regs_modified.rip = kKernelAddr;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs_modified, sizeof(regs_modified)),
              ZX_ERR_INVALID_ARGS);

    // Clean up: Restore the original register state.
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs, sizeof(regs)),
              ZX_OK);
    // Allow the child thread to resume and exit.
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    ASSERT_EQ(zx_object_signal(event, 0, ZX_USER_SIGNAL_0), ZX_OK);
    // Wait for the child thread to signal that it has continued.
    ASSERT_EQ(zx_object_wait_one(event, ZX_USER_SIGNAL_1, ZX_TIME_INFINITE,
                                 NULL),
              ZX_OK);
    // Wait for the child thread to exit.
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE,
                                 NULL),
              ZX_OK);
    ASSERT_EQ(zx_handle_close(event), ZX_OK);
    ASSERT_EQ(zx_handle_close(thread_handle), ZX_OK);
#endif

    END_TEST;
}

// Test that, on ARM64, userland cannot use zx_thread_write_state() to
// modify flag bits such as I and F (bits 7 and 6), which are the IRQ and
// FIQ interrupt disable flags.  We don't want userland to be able to set
// those flags to 1, since that would disable interrupts.  Also, userland
// should not be able to read these bits.
static bool TestWritingArmFlagsRegister() {
    BEGIN_TEST;

#if defined(__aarch64__)
    TestWritingThreadArg arg = {.v = 0};
    zxr_thread_t thread;
    zx_handle_t thread_handle;
    ASSERT_TRUE(start_thread(TestWritingThreadFn, &arg, &thread,
                             &thread_handle));
    // Wait for the thread to start executing and enter its main loop.
    while (arg.v != 1) {
        ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_USEC(1))), ZX_OK);
    }
    zx_handle_t suspend_token = ZX_HANDLE_INVALID;
    ASSERT_TRUE(suspend_thread_synchronous(thread_handle, &suspend_token));

    zx_thread_state_general_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);

    // Check that zx_thread_read_state() does not report any more flag bits
    // than are readable via userland instructions.
    const uint64_t kUserVisibleFlags = 0xf0000000;
    EXPECT_EQ(regs.cpsr & ~kUserVisibleFlags, 0u);

    // Try setting more flag bits.
    uint64_t original_cpsr = regs.cpsr;
    regs.cpsr |= ~kUserVisibleFlags;
    ASSERT_EQ(zx_thread_write_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                    &regs, sizeof(regs)),
              ZX_OK);

    // Firstly, if we read back the register flag, the extra flag bits
    // should have been ignored and should not be reported as set.
    ASSERT_EQ(zx_thread_read_state(thread_handle, ZX_THREAD_STATE_GENERAL_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);
    EXPECT_EQ(regs.cpsr, original_cpsr);

    // Secondly, if we resume the thread, we should be able to kill it.  If
    // zx_thread_write_state() set the interrupt disable flags, then if the
    // thread gets scheduled, it will never get interrupted and we will not
    // be able to kill and join the thread.
    arg.v = 0;
    ASSERT_EQ(zx_handle_close(suspend_token), ZX_OK);
    // Wait until the thread has actually resumed execution.
    while (arg.v != 1) {
        ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_USEC(1))), ZX_OK);
    }
    ASSERT_EQ(zx_task_kill(thread_handle), ZX_OK);
    ASSERT_EQ(zx_object_wait_one(thread_handle, ZX_THREAD_TERMINATED,
                                 ZX_TIME_INFINITE, NULL),
              ZX_OK);

// Clean up.
#endif

    END_TEST;
}

static bool TestWriteReadDebugRegisterState() {
    BEGIN_TEST;
#if defined(__x86_64__)
    zx_thread_state_debug_regs_t debug_regs_to_write;
    zx_thread_state_debug_regs_t debug_regs_expected;
    debug_regs_fill_test_values(&debug_regs_to_write, &debug_regs_expected);

    // Because setting debug state is priviledged, we need to do it through syscalls:
    // 1. Start the thread into a routine that simply spins idly.
    // 2. Suspend it.
    // 3. Write the expected debug state through a syscall.
    // 4. Resume the thread.
    // 5. Suspend it again.
    // 6. Read the state and compare it.

    RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
    ASSERT_TRUE(setup.Init(&spin_with_debug_regs, &debug_regs_to_write));

    // Write the test values to the debug registers.
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs_to_write, sizeof(debug_regs_to_write)),
              ZX_OK);

    // Resume and re-suspend the thread.
    ASSERT_TRUE(setup.Resume());
    // Allow some time for the thread to execute again and spin for a bit.
    ASSERT_EQ(zx_nanosleep(zx_deadline_after(ZX_MSEC(100))), ZX_OK);
    ASSERT_TRUE(setup.Suspend());

    // Get the current debug state of the suspended thread.
    zx_thread_state_debug_regs_t regs;
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);

    ASSERT_TRUE(debug_regs_expect_eq(__FILE__, __LINE__, regs, debug_regs_expected));

#elif defined(__aarch64__)
    // We get how many breakpoints we have.
    zx_thread_state_debug_regs_t actual_regs = {};
    RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
    ASSERT_TRUE(setup.Init(&spin_with_debug_regs, &actual_regs));

    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
              &actual_regs, sizeof(actual_regs)), ZX_OK);

    // Arm ensures at least 2 breakpoints.
    ASSERT_GE(actual_regs.hw_bps_count, 2u);
    ASSERT_LE(actual_regs.hw_bps_count, 16u);

    // TODO(donosoc): Once the context switch state tracking is done, add the resume-suspect test
    //                to ensure that it's keeping the state correctly. This is what is done in the
    //                x86 portion of this test.

    zx_thread_state_debug_regs_t regs = {};
    regs.hw_bps_count = actual_regs.hw_bps_count;

    // We use the address of a function we know is in userspace.
    uint64_t base = reinterpret_cast<uint64_t>(TestWriteReadDebugRegisterState);

    // install the registers. We only test two breakpoints because those are the only ones we know
    // for sure will be there.
    regs.hw_bps[0].dbgbvr = 0x0;   // 0 is valid.
    regs.hw_bps[0].dbgbcr = 0x0;
    regs.hw_bps[1].dbgbvr = base;
    regs.hw_bps[1].dbgbcr = 0x0;

    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                   &regs, sizeof(regs)),
              ZX_OK);

    ASSERT_EQ(regs.hw_bps[0].dbgbvr, 0x0);
    ASSERT_EQ(regs.hw_bps[1].dbgbvr, base);

#endif
    END_TEST;
}

// All writeable bits as 0.
#define DR6_ZERO_MASK (0xffff0ff0ul)
#define DR7_ZERO_MASK (0x700ul)

static bool TestDebugRegistersValidation() {
    BEGIN_TEST;
#if defined(__x86_64__)
    zx_thread_state_debug_regs_t debug_regs = {};
    RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
    ASSERT_TRUE(setup.Init(&spin_with_debug_regs, &debug_regs));

    // Writing all 0s should work and should mask values.
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_OK);

    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_OK);

    for (size_t i = 0; i < 4; i++)
        ASSERT_EQ(debug_regs.dr[i], 0);
    ASSERT_EQ(debug_regs.dr6, DR6_ZERO_MASK);
    ASSERT_EQ(debug_regs.dr7, DR7_ZERO_MASK);

    // Writing an invalid address should fail.
    debug_regs = {};
    debug_regs.dr[1] = 0x1000;
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_ERR_INVALID_ARGS);

    // Writing an kernel address should fail.
    debug_regs = {};
    debug_regs.dr[2] = 0xffff00000000;
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_ERR_INVALID_ARGS);

    // Invalid values should be masked out.
    debug_regs = {};
    debug_regs.dr6 = ~DR6_ZERO_MASK;
    // We avoid the General Detection flag, which would make us throw an exception on next write.
    debug_regs.dr7 = ~DR7_ZERO_MASK;
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_OK);

    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_OK);

    for (size_t i = 0; i < 4; i++)
        ASSERT_EQ(debug_regs.dr[i], 0);
    // DR6: Should not have been written.
    ASSERT_EQ(debug_regs.dr6, DR6_ZERO_MASK);
    ASSERT_EQ(debug_regs.dr7, 0xffff07ff);
#elif defined(__aarch64__)
    zx_thread_state_debug_regs_t debug_regs = {};
    zx_thread_state_debug_regs_t actual_regs = {};
    RegisterReadSetup<zx_thread_state_debug_regs_t> setup;
    ASSERT_TRUE(setup.Init(&spin_with_debug_regs, &actual_regs));

    // We read the initial state to know how many HW breakpoints we have.
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                   &actual_regs, sizeof(actual_regs)),
              ZX_OK);

    // Wrong amount of HW breakpoints should fail.
    debug_regs.hw_bps_count = actual_regs.hw_bps_count;
    debug_regs.hw_bps_count++;
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_ERR_INVALID_ARGS, "Wrong amount of bps should fail");

    // Writing a kernel address should fail.
    debug_regs.hw_bps_count = actual_regs.hw_bps_count;
    debug_regs.hw_bps[0].dbgbvr = (uint64_t)-1;
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_ERR_INVALID_ARGS, "Kernel address should fail");

    // Validation should mask unwanted values from the control register.
    // Only bit 0 is unset. This means the breakpoint is disabled.
    debug_regs.hw_bps[0].dbgbcr = 0xfffffffe;
    debug_regs.hw_bps[0].dbgbvr = 0; // 0 is a valid value.

    debug_regs.hw_bps[1].dbgbcr = 0x1; // Only the enabled value is set.
    // We use the address of a function we know is in userspace.
    debug_regs.hw_bps[1].dbgbvr = reinterpret_cast<uint64_t>(TestDebugRegistersValidation);
    ASSERT_EQ(zx_thread_write_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                    &debug_regs, sizeof(debug_regs)),
              ZX_OK, "Validation should correctly mask invalid values");


    // Re-read the state and verify.
    ASSERT_EQ(zx_thread_read_state(setup.thread_handle(), ZX_THREAD_STATE_DEBUG_REGS,
                                   &actual_regs, sizeof(actual_regs)),
              ZX_OK);

    EXPECT_EQ(actual_regs.hw_bps_count, debug_regs.hw_bps_count);
    EXPECT_EQ(actual_regs.hw_bps[0].dbgbcr, 0x000001e4);
    EXPECT_EQ(actual_regs.hw_bps[0].dbgbvr, 0);
    EXPECT_EQ(actual_regs.hw_bps[1].dbgbcr, 0x000001e5);
    EXPECT_EQ(actual_regs.hw_bps[1].dbgbvr, debug_regs.hw_bps[1].dbgbvr);
#endif
    END_TEST;
}

BEGIN_TEST_CASE(threads_tests)
RUN_TEST(TestBasics)
RUN_TEST(TestInvalidRights)
RUN_TEST(TestDetach)
RUN_TEST(TestLongNameSucceeds)
RUN_TEST(TestThreadStartOnInitialThread)
RUN_TEST_ENABLE_CRASH_HANDLER(TestThreadStartWithZeroInstructionPointer)
RUN_TEST(TestKillBusyThread)
RUN_TEST(TestKillSleepThread)
RUN_TEST(TestKillWaitThread)
RUN_TEST(TestNonstartedThread)
RUN_TEST(TestThreadKillsItself)
RUN_TEST(TestInfoTaskStatsFails)
RUN_TEST(TestResumeSuspended)
RUN_TEST(TestSuspendSleeping)
RUN_TEST(TestSuspendChannelCall)
RUN_TEST(TestSuspendPortCall)
RUN_TEST(TestSuspendStopsThread)
RUN_TEST(TestSuspendMultiple)
RUN_TEST(TestSuspendSelf)
RUN_TEST(TestSuspendAfterDeath)
RUN_TEST(TestKillSuspendedThread)
RUN_TEST(TestStartSuspendedThread)
RUN_TEST(TestStartSuspendedAndResumedThread)
RUN_TEST(TestSuspendSingleWaitAsyncSignalDelivery)
RUN_TEST(TestSuspendRepeatingWaitAsyncSignalDelivery)
RUN_TEST(TestReadingGeneralRegisterState)

// Test disabled, see ZX-2508.
// RUN_TEST(TestReadingFpRegisterState)
// RUN_TEST(TestReadingVectorRegisterState)

RUN_TEST(TestWritingGeneralRegisterState)

// Test disabled, see ZX-2508.
// RUN_TEST(TestWritingFpRegisterState)
// RUN_TEST(TestWritingVectorRegisterState)

RUN_TEST(TestThreadLocalRegisterState)
RUN_TEST(TestNoncanonicalRipAddress)
RUN_TEST(TestWritingArmFlagsRegister)

// Test disabled, see ZX-2508.
// RUN_TEST(TestWriteReadDebugRegisterState);
// RUN_TEST(TestDebugRegistersValidation);

END_TEST_CASE(threads_tests)
