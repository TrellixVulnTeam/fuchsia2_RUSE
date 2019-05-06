// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/device/test_with_device.h"
#include "garnet/bin/guest/vmm/device/virtio_queue_fake.h"

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>
#include <zircon/device/ethernet.h>

#include <virtio/net.h>

static constexpr char kVirtioNetUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_net#meta/virtio_net.cmx";
static constexpr size_t kNumQueues = 2;
static constexpr uint16_t kQueueSize = 16;
static constexpr size_t kVmoSize = 1024;
static constexpr uint16_t kFakeInterfaceId = 0;

class VirtioNetTest : public TestWithDevice,
                      public fuchsia::netstack::Netstack {
 public:
  void GetPortForService(::std::string service,
                         fuchsia::netstack::Protocol protocol,
                         GetPortForServiceCallback callback) override {}

  void GetAddress(::std::string address, uint16_t port,
                  GetAddressCallback callback) override {}

  void GetInterfaces(GetInterfacesCallback callback) override {}
  void GetInterfaces2(GetInterfaces2Callback callback) override {}

  void GetRouteTable(GetRouteTableCallback callback) override {}
  void GetRouteTable2(GetRouteTable2Callback callback) override {}

  void GetStats(uint32_t nicid, GetStatsCallback callback) override {}

  void GetAggregateStats(
      ::fidl::InterfaceRequest<::fuchsia::io::Node> object) override {}

  void SetInterfaceStatus(uint32_t nicid, bool enabled) override {}

  void SetInterfaceAddress(uint32_t nicid, fuchsia::net::IpAddress addr,
                           uint8_t prefixLen,
                           SetInterfaceAddressCallback callback) override {
    fuchsia::netstack::NetErr err{
        .status = fuchsia::netstack::Status::OK,
        .message = "",
    };
    callback(err);
  }

  void RemoveInterfaceAddress(
      uint32_t nicid, fuchsia::net::IpAddress addr, uint8_t prefixLen,
      RemoveInterfaceAddressCallback callback) override {}

  void SetInterfaceMetric(uint32_t nicid, uint32_t metric,
                          SetInterfaceMetricCallback callback) override {}

  void SetDhcpClientStatus(uint32_t nicid, bool enabled,
                           SetDhcpClientStatusCallback callback) override {}

  void BridgeInterfaces(::std::vector<uint32_t> nicids,
                        BridgeInterfacesCallback callback) override {}

  void AddEthernetDevice(
      ::std::string topological_path,
      fuchsia::netstack::InterfaceConfig interfaceConfig,
      ::fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
      AddEthernetDeviceCallback callback) override {
    eth_device_ = device.Bind();
    eth_device_added_ = true;
    callback(kFakeInterfaceId);
  }

  void StartRouteTableTransaction(
      ::fidl::InterfaceRequest<fuchsia::netstack::RouteTableTransaction>
          routeTableTransaction,
      StartRouteTableTransactionCallback callback) override {}

 protected:
  VirtioNetTest()
      : rx_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        tx_queue_(phys_mem_, rx_queue_.end(), kQueueSize) {}

  void SetUp() override {
    auto env_services = CreateServices();

    // Add our fake netstack service.
    ASSERT_EQ(ZX_OK, env_services->AddService(bindings_.GetHandler(this)));

    // Launch device process.
    fuchsia::guest::device::StartInfo start_info;
    zx_status_t status = LaunchDevice(kVirtioNetUrl, tx_queue_.end(),
                                      &start_info, std::move(env_services));
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    services_->Connect(net_.NewRequest());
    net_->Start(std::move(start_info), [] {});

    // Wait for the device to call AddEthernetDevice on the netstack.
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([this] { return eth_device_added_; },
                                          zx::sec(5)));

    // Get fifos.
    eth_device_->GetFifos(
        [this](zx_status_t status,
               std::unique_ptr<fuchsia::hardware::ethernet::Fifos> fifos) {
          ASSERT_EQ(status, ZX_OK);
          rx_ = std::move(fifos->rx);
          tx_ = std::move(fifos->tx);
        });
    ASSERT_TRUE(
        RunLoopWithTimeoutOrUntil([this] { return (rx_ && tx_); }, zx::sec(5)));

    size_t vmo_size = kVmoSize;
    status = zx::vmo::create(vmo_size, ZX_VMO_NON_RESIZABLE, &vmo_);
    ASSERT_EQ(status, ZX_OK);

    zx::vmo vmo_dup;
    status = vmo_.duplicate(ZX_RIGHTS_IO | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER,
                            &vmo_dup);
    ASSERT_EQ(status, ZX_OK);

    eth_device_->SetIOBuffer(std::move(vmo_dup), [](zx_status_t status) {
      ASSERT_EQ(status, ZX_OK) << "Failed to set IO buffer";
    });

    eth_device_->Start([](zx_status_t status) { ASSERT_EQ(ZX_OK, status); });

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&rx_queue_, &tx_queue_};
    for (size_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      net_->ConfigureQueue(i, q->size(), q->desc(), q->avail(), q->used(),
                           [] {});
    }

    net_->Ready(0, []{});
  }

  fuchsia::guest::device::VirtioNetPtr net_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  ::fidl::BindingSet<fuchsia::netstack::Netstack> bindings_;
  fuchsia::hardware::ethernet::DevicePtr eth_device_;
  bool eth_device_added_ = false;

  zx::fifo rx_;
  zx::fifo tx_;
  zx::vmo vmo_;
};

TEST_F(VirtioNetTest, SendToGuest) {
  const size_t packet_size = 10;
  uint8_t* data;
  zx_status_t status = DescriptorChainBuilder(rx_queue_)
                           .AppendWritableDescriptor(
                               &data, sizeof(virtio_net_hdr_t) + packet_size)
                           .Build();
  ASSERT_EQ(status, ZX_OK);

  const uint8_t expected_packet[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  ASSERT_EQ(packet_size, sizeof(expected_packet));
  vmo_.write(static_cast<const void*>(&expected_packet), 0,
             sizeof(expected_packet));

  eth_fifo_entry_t entry{
      .offset = 0,
      .length = packet_size,
      .flags = 0,
      .cookie = 0xdeadbeef,
  };

  zx_signals_t pending = 0;
  status = tx_.wait_one(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                        zx::deadline_after(zx::sec(5)), &pending);
  ASSERT_EQ(status, ZX_OK);

  status = tx_.write(sizeof(entry), static_cast<void*>(&entry), 1, nullptr);
  ASSERT_EQ(status, ZX_OK);

  net_->NotifyQueue(0);

  RunLoopUntilIdle();

  status = tx_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                        zx::deadline_after(zx::sec(5)), &pending);
  ASSERT_EQ(status, ZX_OK);

  status = tx_.read(sizeof(entry), static_cast<void*>(&entry), 1, nullptr);
  ASSERT_EQ(status, ZX_OK);

  status = WaitOnInterrupt();
  ASSERT_EQ(status, ZX_OK);

  virtio_net_hdr_t* hdr = reinterpret_cast<virtio_net_hdr_t*>(data);
  EXPECT_EQ(hdr->num_buffers, 1);
  EXPECT_EQ(hdr->gso_type, VIRTIO_NET_HDR_GSO_NONE);
  EXPECT_EQ(hdr->flags, 0);

  data += sizeof(virtio_net_hdr_t);
  for (size_t i = 0; i != packet_size; ++i) {
    EXPECT_EQ(data[i], expected_packet[i]);
  }

  EXPECT_EQ(entry.offset, 0u);
  EXPECT_EQ(entry.length, packet_size);
  EXPECT_EQ(entry.flags, ETH_FIFO_TX_OK);
  EXPECT_EQ(entry.cookie, 0xdeadbeef);
}

TEST_F(VirtioNetTest, ReceiveFromGuest) {
  const size_t packet_size = 10;
  uint8_t data[packet_size + sizeof(virtio_net_hdr_t)];
  zx_status_t status = DescriptorChainBuilder(tx_queue_)
                           .AppendReadableDescriptor(
                               &data, sizeof(virtio_net_hdr_t) + packet_size)
                           .Build();
  ASSERT_EQ(status, ZX_OK);

  const uint8_t expected_packet[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  memcpy(reinterpret_cast<void*>(&data), expected_packet, packet_size);

  eth_fifo_entry_t entry{
      .offset = 0,
      .length = packet_size,
      .flags = 0,
      .cookie = 0xdeadbeef,
  };

  zx_signals_t pending = 0;
  status = rx_.wait_one(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED,
                        zx::deadline_after(zx::sec(5)), &pending);
  ASSERT_EQ(status, ZX_OK);

  status = rx_.write(sizeof(entry), static_cast<void*>(&entry), 1, nullptr);
  ASSERT_EQ(status, ZX_OK);

  RunLoopUntilIdle();

  net_->NotifyQueue(1);

  RunLoopUntilIdle();

  status = rx_.wait_one(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED,
                        zx::deadline_after(zx::sec(5)), &pending);
  ASSERT_EQ(status, ZX_OK);

  status = rx_.read(sizeof(entry), static_cast<void*>(&entry), 1, nullptr);
  ASSERT_EQ(status, ZX_OK);

  status = WaitOnInterrupt();
  ASSERT_EQ(status, ZX_OK);

  EXPECT_EQ(entry.offset, 0u);
  EXPECT_EQ(entry.length, packet_size);
  EXPECT_EQ(entry.flags, ETH_FIFO_RX_OK);
  EXPECT_EQ(entry.cookie, 0xdeadbeef);
}
