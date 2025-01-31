// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/overnet/streamlinkfuzzer/cpp/fidl.h>
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/links/stream_link.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"
#include "src/connectivity/overnet/lib/routing/router.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

using namespace overnet;

namespace {

class FuzzedStreamLink final : public StreamLink {
 public:
  FuzzedStreamLink(Router* router) : StreamLink(router, NodeId(1), 64, 1) {}
  void Emit(Slice slice, Callback<Status> done) override { abort(); }
};

class StreamLinkFuzzer {
 public:
  StreamLinkFuzzer(bool log_stuff)
      : logging_(log_stuff ? new Logging(&timer_) : nullptr) {
    auto link = MakeLink<FuzzedStreamLink>(&router_);
    link_ = link.get();
    router_.RegisterLink(std::move(link));
  }

  void Run(fuchsia::overnet::streamlinkfuzzer::UntrustedInputPlan plan) {
    for (const auto& action : plan.input) {
      link_->Process(timer_.Now(), Slice::FromContainer(action));
      timer_.Step(TimeDelta::FromSeconds(1).as_us());
    }
  }

 private:
  TestTimer timer_;
  struct Logging {
    Logging(Timer* timer) : tracer(timer) {}
    TraceCout tracer;
    ScopedRenderer set_tracer{&tracer};
  };
  std::unique_ptr<Logging> logging_;
  Router router_{&timer_, NodeId(1), false};
  FuzzedStreamLink* link_;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (auto buffer =
          Decode<fuchsia::overnet::streamlinkfuzzer::UntrustedInputPlan>(
              Slice::FromCopiedBuffer(data, size));
      buffer.is_ok()) {
    StreamLinkFuzzer(false).Run(std::move(*buffer));
  }
  return 0;
}
