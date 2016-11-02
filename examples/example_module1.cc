// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include <mojo/system/main.h>

#include "apps/document_store/interfaces/document.mojom.h"
#include "apps/modular/document_editor/document_editor.h"
#include "apps/modular/mojo/single_service_view_app.h"
#include "apps/modular/services/story/story_runner.mojom.h"
#include "apps/mozart/lib/skia/skia_vmo_surface.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/bindings/interface_handle.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/environment/logging.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

namespace {

constexpr uint32_t kContentImageResourceId = 1;
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr int kTickRotationDegrees = 45;
constexpr int kValueHandoffDuration = 3;

// Subjects
constexpr char kDocId[] =
    "http://google.com/id/dc7cade7-7be0-4e23-924d-df67e15adae5";

// Property labels
constexpr char kCounterLabel[] = "http://schema.domokit.org/counter";
constexpr char kSenderLabel[] = "http://schema.org/sender";

using document_store::Document;
using document_store::Property;
using document_store::Value;

using mojo::ApplicationConnector;
using mojo::Array;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::Map;
using mojo::StrongBinding;
using mojo::String;
using mojo::StructPtr;

using modular::DocumentEditor;
using modular::Link;
using modular::LinkChanged;
using modular::Module;
using modular::MojoDocMap;
using modular::Session;
using modular::operator<<;

// Module implementation that acts as a leaf module. It implements
// both Module and the LinkChanged observer of its own Link.
class Module1Impl : public mozart::BaseView, public Module, public LinkChanged {
 public:
  explicit Module1Impl(InterfaceHandle<ApplicationConnector> app_connector,
                       InterfaceRequest<Module> module_request,
                       InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(app_connector),
                 std::move(view_owner_request),
                 "Module1Impl"),
        module_binding_(this, std::move(module_request)),
        watcher_binding_(this),
        tick_(0) {
    FTL_LOG(INFO) << "Module1Impl";
  }

  ~Module1Impl() override { FTL_LOG(INFO) << "~Module1Impl"; }

  void Initialize(InterfaceHandle<Session> session,
                  InterfaceHandle<Link> link) override {
    session_.Bind(std::move(session));
    link_.Bind(std::move(link));

    InterfaceHandle<LinkChanged> watcher;
    watcher_binding_.Bind(&watcher);
    link_->Watch(std::move(watcher));
  }

  // See comments on Module2Impl in example-module2.cc.
  void Notify(MojoDocMap docs) override {
    FTL_LOG(INFO) << "Module1Impl::Notify() " << (int64_t)this << docs;
    docs_ = std::move(docs);

    if (!editor_.Edit(kDocId, &docs_))
      return;

    Value* sender = editor_.GetValue(kSenderLabel);
    Value* value = editor_.GetValue(kCounterLabel);
    FTL_DCHECK(value != nullptr);

    tick_++;
    int counter = value->get_int_value();
    if (counter > 10) {
      // For the last iteration, Module2 removes the sender.
      FTL_DCHECK(sender == nullptr);
      session_->Done();
    } else {
      FTL_DCHECK(sender != nullptr);
      handoff_time_ = ftl::TimePoint::Now() +
                      ftl::TimeDelta::FromSeconds(kValueHandoffDuration);
      Invalidate();
    }
  }

 private:
  // Copied from
  // https://fuchsia.googlesource.com/mozart/+/master/examples/spinning_square/spinning_square.cc
  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());
    auto update = mozart::SceneUpdate::New();
    const mojo::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mojo::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;
      mozart::ImagePtr image;
      sk_sp<SkSurface> surface = mozart::MakeSkSurface(size, &image);
      FTL_CHECK(surface);
      DrawContent(surface->getCanvas(), size);
      auto content_resource = mozart::Resource::New();
      content_resource->set_image(mozart::ImageResource::New());
      content_resource->get_image()->image = std::move(image);
      update->resources.insert(kContentImageResourceId,
                               std::move(content_resource));
      auto root_node = mozart::Node::New();
      root_node->op = mozart::NodeOp::New();
      root_node->op->set_image(mozart::ImageNodeOp::New());
      root_node->op->get_image()->content_rect = bounds.Clone();
      root_node->op->get_image()->image_resource_id = kContentImageResourceId;
      update->nodes.insert(kRootNodeId, std::move(root_node));
    } else {
      auto root_node = mozart::Node::New();
      update->nodes.insert(kRootNodeId, std::move(root_node));
    }
    scene()->Update(std::move(update));
    scene()->Publish(CreateSceneMetadata());

    if (ftl::TimePoint::Now() >= handoff_time_) {
      Value* sender = editor_.GetValue(kSenderLabel);
      Value* value = editor_.GetValue(kCounterLabel);
      FTL_DCHECK(value != nullptr);

      int counter = value->get_int_value();
      value->set_int_value(counter + 1);
      sender->set_string_value("Module1Impl");

      editor_.Keep(&docs_);
      link_->SetAllDocuments(docs_.Clone());
    } else {
      Invalidate();
    }
  }

  void DrawContent(SkCanvas* const canvas, const mojo::Size& size) {
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    canvas->rotate(SkIntToScalar(kTickRotationDegrees * tick_));
    SkPaint paint;
    paint.setColor(SK_ColorGREEN);
    paint.setAntiAlias(true);
    float d = std::min(size.width, size.height) / 4;
    canvas->drawRect(SkRect::MakeLTRB(-d, -d, d, d), paint);
    canvas->flush();
  }

  StrongBinding<Module> module_binding_;
  StrongBinding<LinkChanged> watcher_binding_;

  InterfacePtr<Session> session_;
  InterfacePtr<Link> link_;

  // Used by |OnDraw()| to decide whether enough time has passed, so that the
  // value can be sent back and a new frame drawn.
  ftl::TimePoint handoff_time_;
  MojoDocMap docs_;
  DocumentEditor editor_;

  // This is a counter that is incremented when a new value is received and used
  // to rotate a square.
  int tick_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Module1Impl);
};
}
// namespace

MojoResult MojoMain(MojoHandle request) {
  FTL_LOG(INFO) << "module1 main";
  modular::SingleServiceViewApp<modular::Module, Module1Impl> app;
  return mojo::RunApplication(request, &app);
}
