// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"syscall/zx"
	"testing"

	"fidl/fuchsia/hardware/ethernet"
	"fidl/fuchsia/net"
	"fidl/fuchsia/netstack"
	ethernetext "fidlext/fuchsia/hardware/ethernet"

	"netstack/fidlconv"
	"netstack/link/eth"

	"github.com/google/netstack/tcpip"
	"github.com/google/netstack/tcpip/network/arp"
	"github.com/google/netstack/tcpip/network/ipv4"
	"github.com/google/netstack/tcpip/network/ipv6"
	"github.com/google/netstack/tcpip/stack"
)

const (
	testDeviceName string        = "testdevice"
	testTopoPath   string        = "/fake/ethernet/device"
	testIpAddress  tcpip.Address = tcpip.Address("\xc0\xa8\x2a\x10")
)

func TestNicName(t *testing.T) {
	ns := newNetstack(t)

	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName}, &ethernetext.Device{
		TB:                t,
		GetInfoImpl:       func() (ethernet.Info, error) { return ethernet.Info{}, nil },
		SetClientNameImpl: func(string) (int32, error) { return 0, nil },
		GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: 1,
			}, nil
		},
		GetStatusImpl: func() (uint32, error) {
			return uint32(eth.LinkUp), nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
	})

	if err != nil {
		t.Fatal(err)
	}
	ifs.mu.Lock()
	if ifs.mu.nic.Name != testDeviceName {
		t.Errorf("ifs.mu.nic.Name = %v, want %v", ifs.mu.nic.Name, testDeviceName)
	}
	ifs.mu.Unlock()
}

func TestNicStartedByDefault(t *testing.T) {
	ns := newNetstack(t)

	startCalled := false
	eth := deviceForAddEth(ethernet.Info{}, t)
	eth.StartImpl = func() (int32, error) {
		startCalled = true
		return int32(zx.ErrOk), nil
	}

	_, err := ns.addEth(testTopoPath, netstack.InterfaceConfig{Name: testDeviceName}, &eth)
	if err != nil {
		t.Fatal(err)
	}

	if !startCalled {
		t.Errorf("expected ethernet.Device.Start to be called from addEth")
	}
}

func TestDhcpConfiguration(t *testing.T) {
	ns := newNetstack(t)

	ipAddressConfig := netstack.IpAddressConfig{}
	ipAddressConfig.SetDhcp(true)

	d := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)

	if err != nil {
		t.Fatal(err)
	}

	ifs.mu.Lock()
	if ifs.mu.dhcp.Client == nil {
		t.Errorf("no dhcp client")
	}
	if !ifs.mu.dhcp.enabled {
		t.Errorf("expected dhcp to be configured to run initially")
	}

	if !ifs.mu.dhcp.running() {
		t.Errorf("expected dhcp client to be running initially")
	}
	ifs.mu.Unlock()

	ifs.eth.Down()

	ifs.mu.Lock()
	if ifs.mu.dhcp.running() {
		t.Errorf("expected dhcp client to be stopped on eth down")
	}
	if !ifs.mu.dhcp.enabled {
		t.Errorf("expected dhcp configuration to be preserved on eth down")
	}
	ifs.mu.Unlock()

	ifs.eth.Up()

	ifs.mu.Lock()
	if !ifs.mu.dhcp.running() {
		t.Errorf("expected dhcp client to be running on eth restart")
	}
	if !ifs.mu.dhcp.enabled {
		t.Errorf("expected dhcp configuration to be preserved on eth restart")
	}
	ifs.mu.Unlock()
}

func TestStaticIPConfiguration(t *testing.T) {
	ns := newNetstack(t)

	ipAddressConfig := netstack.IpAddressConfig{}
	addr := fidlconv.ToNetIpAddress(testIpAddress)
	subnet := net.Subnet{Addr: addr, PrefixLen: 32}
	ipAddressConfig.SetStaticIp(subnet)
	d := deviceForAddEth(ethernet.Info{}, t)
	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)
	if err != nil {
		t.Fatal(err)
	}

	if ifs.mu.nic.Addr != testIpAddress {
		t.Errorf("expected static IP to be set when configured")
	}
	if ifs.mu.dhcp.enabled {
		t.Errorf("expected dhcp state to be disabled initially")
	}

	ifs.eth.Down()

	if ifs.mu.dhcp.enabled {
		t.Errorf("expected dhcp state to remain disabled after bringing interface down")
	}

	ifs.eth.Up()

	ifs.mu.Lock()
	if ifs.mu.dhcp.enabled {
		t.Errorf("expected dhcp state to remain disabled after restarting interface")
	}

	ifs.setDHCPStatusLocked(true)
	if !ifs.mu.dhcp.enabled {
		t.Errorf("expected dhcp state to become enabled after manually enabling it")
	}
	if !ifs.mu.dhcp.running() {
		t.Errorf("expected dhcp state running")
	}
	ifs.mu.Unlock()
}

func TestWLANStaticIPConfiguration(t *testing.T) {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	ns := &Netstack{
		arena: arena,
	}
	ns.mu.ifStates = make(map[tcpip.NICID]*ifState)
	ns.mu.stack = stack.New(
		[]string{
			ipv4.ProtocolName,
			ipv6.ProtocolName,
			arp.ProtocolName,
		}, nil, stack.Options{})

	ns.OnInterfacesChanged = func([]netstack.NetInterface) {}
	ipAddressConfig := netstack.IpAddressConfig{}
	addr := fidlconv.ToNetIpAddress(testIpAddress)
	subnet := net.Subnet{Addr: addr, PrefixLen: 32}
	ipAddressConfig.SetStaticIp(subnet)
	d := deviceForAddEth(ethernet.Info{Features: ethernet.InfoFeatureWlan}, t)
	ifs, err := ns.addEth("/fake/ethernet/device", netstack.InterfaceConfig{Name: testDeviceName, IpAddressConfig: ipAddressConfig}, &d)
	if err != nil {
		t.Fatal(err)
	}

	if ifs.mu.nic.Addr != testIpAddress {
		t.Errorf("expected static IP to be set when configured")
	}
}

func newNetstack(t *testing.T) *Netstack {
	arena, err := eth.NewArena()
	if err != nil {
		t.Fatal(err)
	}
	ns := &Netstack{
		arena: arena,
	}
	ns.mu.ifStates = make(map[tcpip.NICID]*ifState)
	ns.mu.stack = stack.New(
		[]string{
			ipv4.ProtocolName,
			ipv6.ProtocolName,
			arp.ProtocolName,
		}, nil, stack.Options{})

	ns.OnInterfacesChanged = func([]netstack.NetInterface) {}
	return ns
}

// Returns an ethernetext.Device struct that implements
// ethernet.Device and can be started and stopped.
//
// Reports the passed in ethernet.Info when Device#GetInfo is called.
func deviceForAddEth(info ethernet.Info, t *testing.T) ethernetext.Device {
	return ethernetext.Device{
		TB: t,
		GetInfoImpl: func() (ethernet.Info, error) {
			return info, nil
		},
		SetClientNameImpl: func(string) (int32, error) { return 0, nil },
		GetStatusImpl: func() (uint32, error) {
			return uint32(eth.LinkUp), nil
		},
		GetFifosImpl: func() (int32, *ethernet.Fifos, error) {
			return int32(zx.ErrOk), &ethernet.Fifos{
				TxDepth: 1,
			}, nil
		},
		SetIoBufferImpl: func(zx.VMO) (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StartImpl: func() (int32, error) {
			return int32(zx.ErrOk), nil
		},
		StopImpl: func() error {
			return nil
		},
	}
}
