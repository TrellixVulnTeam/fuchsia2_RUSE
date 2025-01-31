// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_COMPONENT_CONTEXT_H_
#define LIB_SYS_CPP_COMPONENT_CONTEXT_H_

#include <lib/sys/cpp/outgoing_directory.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

namespace sys {

namespace testing {

class ComponentContextProvider;

}

// Context information that this component received at startup.
//
// Upon creation, components are given a namespace, which is file system local
// to the component. A components namespace lets the component interact with
// other components and the system at large. One important part of this
// namespace is the directory of services, typically located at "/svc" in the
// components namespace. The |ComponentContext| provides an ergonomic interface
// to this service bundle through its |svc()| property.
//
// In addition to receiving services, components can also publish services and
// data to other components through their outgoing namespace, which is also a
// directory. The |ComponentContext| provides an ergonomic interface for
// exposing services and other file system objects through its |outgoing()|
// property.
//
// This class is thread-hostile.
//
//  # Simple usage
//
// Instances of this class should be owned and managed on the same thread.
//
// # Advanced usage
//
// You can use a background thread to service this class provided:
// async_dispatcher_t for the background thread is stopped or suspended
// prior to destroying the class object.
//
// # Example
//
// The |ComponentContext| object is typically created early in the startup
// sequence for components, typically after creating the |async::Loop| for the
// main thread.
//
// ```
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigAttachToThread);
//   auto context = sys::ComponentContext::Create();
//   my::App app(std::move(context))
//   loop.Run();
//   return 0;
// }
// ```
class ComponentContext final {
  struct MakePrivate;

 public:
  // Creates a component context.
  //
  // This constructor is rarely used directly. Instead, most clients create a
  // component context using the |Create()| static method.
  ComponentContext(MakePrivate make_private,
                   std::shared_ptr<ServiceDirectory> svc,
                   zx::channel directory_request,
                   async_dispatcher_t* dispatcher = nullptr);

  ~ComponentContext();

  // ComponentContext objects cannot be copied.
  ComponentContext(const ComponentContext&) = delete;
  ComponentContext& operator=(const ComponentContext&) = delete;

  // Creates a component context from the process startup info.
  //
  // Call this function once during process initialization to retrieve the
  // handles supplied to the component by the component manager. This function
  // consumes some of those handles, which means subsequent calls to this
  // function will not return a functional component context.
  //
  // Prefer creating the |ComponentContext| in the |main| function for a
  // component and passing the context to a class named "App" which encapsulates
  // the main logic of the program. This pattern makes testing easier because
  // tests can pass a fake |ComponentContext| from |ComponentContextProvider| to
  // the |App| class to inject dependencies.
  //
  // The returned unique_ptr is never null.
  //
  // # Example
  //
  // ```
  // int main(int argc, const char** argv) {
  //   async::Loop loop(&kAsyncLoopConfigAttachToThread);
  //   auto context = sys::ComponentContext::Create();
  //   my::App app(std::move(context))
  //   loop.Run();
  //   return 0;
  // }
  // ```
  static std::unique_ptr<ComponentContext> Create();

  // The component's incoming directory of services from its namespace.
  //
  // Use this object to connect to services offered by other components.
  //
  // The returned object is thread-safe.
  //
  // # Example
  //
  // ```
  // auto controller = context.svc()->Connect<fuchsia::foo::Controller>();
  // ```
  const std::shared_ptr<ServiceDirectory>& svc() const { return svc_; }

  // The component's outgoing directory.
  //
  // Use this object to publish services and data to the component manager and
  // other components.
  //
  // The returned object is thread-safe.
  //
  // # Example
  //
  // ```
  // class App : public fuchsia::foo::Controller {
  //  public:
  //   App(std::unique_ptr<ComponentContext> context)
  //     : context_(std::move(context) {
  //     context_.outgoing()->AddPublicService(bindings_.GetHandler(this));
  //   }
  //
  //   // fuchsia::foo::Controller implementation:
  //   [...]
  //
  //  private:
  //   fidl::BindingSet<fuchsia::foo::Controller> bindings_;
  // }
  // ```
  const std::shared_ptr<OutgoingDirectory>& outgoing() const {
    return outgoing_;
  }
  std::shared_ptr<OutgoingDirectory>& outgoing() { return outgoing_; }

 private:
  std::shared_ptr<ServiceDirectory> svc_;
  std::shared_ptr<OutgoingDirectory> outgoing_;

  // makes constructor private and only accessible by
  // |sys::testing::ComponentContextProvider|.
  struct MakePrivate {};
  friend class sys::testing::ComponentContextProvider;
};

}  // namespace sys

#endif  // LIB_SYS_CPP_COMPONENT_CONTEXT_H_
