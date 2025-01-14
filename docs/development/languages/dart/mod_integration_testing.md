# Mod integration testing with Topaz

[TOC]

## Introduction

This step-by-step guide is for running integration tests using
[Flutter Driver](https://docs.flutter.io/flutter/flutter_driver/flutter_driver-library.html)
in Topaz. If you are not looking to run integration testing on your mods, or if
your mod is not written using Flutter, then you don’t need this guide.

This is different from unit testing with widgets because the expectation is that
you will be testing simulated user interaction with your mod (tapping buttons,
scrolling, etc, for example), which requires Scenic and cannot be run in QEMU.

The examples in this doc will be focused around a testing mod under
[`//topaz/examples/test/driver_example_mod`](https://fuchsia.googlesource.com/topaz/+/HEAD/examples/test/driver_example_mod).
The name is derived from how it is an example mod relating to the use of
[Flutter Driver](https://docs.flutter.io/flutter/flutter_driver/flutter_driver-library.html).
In addition, you'll see how to set up a hermetic test with Fuchsia
[component testing](/docs/development/tests/test_component.md).

The ultimate goal of this document is to make it possible for you to add
integration tests into
[`//topaz/tests`](https://fuchsia.googlesource.com/topaz/+/HEAD/tests) so that
your mod can be tested in CQ and CI.

## Setup

To start, this doc assumes you’ve already got a mod that you can run on Topaz
(see
[here](/docs/getting_started.md)
if you haven't). For simplicity, we’ll assume it is a standalone mod that
doesn’t depend on other mods (in the future this will have been tested and
verified).

Per the introduction section, we’ll be focusing on `driver_example_mod` as the
mod under test.

### Enabling Flutter Driver extensions

If you want to simulate user interaction with your mod, or capture a screenshot
of a certain state you are interested in, you will need to enable the flutter
driver extensions before you can start using the flutter driver. This is done by
adding `flutter_driver_extendable = true` to your `flutter_app` target in the
[`BUILD.gn`](https://fuchsia.googlesource.com/topaz/+/HEAD/examples/test/driver_example_mod/BUILD.gn)
for your mod:

```gn
flutter_app("driver_example_mod") {
  // ...
  flutter_driver_extendable = true
  // ...
}
```

In a debug JIT setting, this will generate a wrapper for your
[`main`](https://fuchsia.googlesource.com/topaz/+/HEAD/examples/test/driver_example_mod/lib/main.dart)
that calls
[`enableFlutterDriverExtension()`](https://docs.flutter.io/flutter/flutter_driver_extension/enableFlutterDriverExtension.html)
from
[`package:flutter_driver/driver_extension.dart`](https://docs.flutter.io/flutter/flutter_driver_extension/flutter_driver_extension-library.html).
(See also:
[`gen_debug_wrapper_main.py`](https://fuchsia.googlesource.com/topaz/+/HEAD/runtime/flutter_runner/build/gen_debug_wrapper_main.py))

## Writing your tests

Next you’ll get to the exciting part: writing the tests for your mod! These will
require use of the aforementioned
[Flutter Driver](https://docs.flutter.io/flutter/flutter_driver/flutter_driver-library.html)
library.

Tests live in a `test` subfolder of your mod and end in `_test.dart`. These
requirements are stipulated by
[`dart_fuchsia_test`](https://fuchsia.googlesource.com/topaz/+/master/runtime/dart/dart_fuchsia_test.gni),
described [later](#build_gn-target). The tests for `driver_example_mod` are in
[`driver_example_mod_test.dart`](https://fuchsia.googlesource.com/topaz/+/HEAD/examples/test/driver_example_mod/test/driver_example_mod_test.dart).

### Boilerplate

You’ll need some boilerplate to set up and tear down your code. The following
helper function starts a basemgr with dev shells, allowing you to inject your
mod as the root view. This helper will eventually be factored into a Modular
Framework service call.

```dart
import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

const _basemgrUrl = 'fuchsia-pkg://fuchsia.com/basemgr#meta/basemgr.cmx';

// Starts basemgr with dev shells. This should be called from within a
// try/finally or similar construct that closes the component controller.
Future<void> _startBasemgr(
    InterfaceRequest<ComponentController> controllerRequest,
    String rootModUrl) async {
  final context = StartupContext.fromStartupInfo();

  final launchInfo = LaunchInfo(url: _basemgrUrl, arguments: [
    '--base_shell=fuchsia-pkg://fuchsia.com/dev_base_shell#meta/dev_base_shell.cmx',
    '--session_shell=fuchsia-pkg://fuchsia.com/dev_session_shell#meta/dev_session_shell.cmx',
    '--session_shell_args=--root_module=$rootModUrl',
    '--story_shell=fuchsia-pkg://fuchsia.com/dev_story_shell#meta/dev_story_shell.cmx',
    '--test',
    '--enable_presenter',
    '--run_base_shell_with_test_runner=false'
  ]);
  await context.launcher.createComponent(launchInfo, controllerRequest);
}
```

The first four options start basemgr with dev shells, which simply start and
display the given `root_module` using the Modular framework.

The `--test` option prevents basemgr from overriding your command-line options
with the on-device base shell configuration, but then requires the next two
options to turn off test-only behavior we don't want. `--enable_presenter`
allows basemgr to be a root presenter (display) as it is in production, and
`--run_base_shell_with_test_runner=false` prevents it from needing to connect to
the `TestRunner` service (used for Modular multiprocess testing).

In your test setup, you'll need to start basemgr with your app under test and
connect to Flutter Driver.

```dart
// ...
import 'package:flutter_driver/flutter_driver.dart';
import 'package:test/test.dart';

const Pattern _isolatePattern = 'driver_example_mod';
const _testAppUrl =
    'fuchsia-pkg://fuchsia.com/driver_example_mod#meta/driver_example_mod.cmx';

// ... _startBasemgr ...

void main() {
  group('driver example tests', () {
    final controller = ComponentControllerProxy();
    FlutterDriver driver;

    setUpAll(() async {
      await _startBasemgr(controller.ctrl.request(), _testAppUrl);

      driver = await FlutterDriver.connect(
          fuchsiaModuleTarget: _isolatePattern,
          printCommunication: true,
          logCommunicationToFile: false);
    });

    tearDownAll(() async {
      await driver?.close();
      controller.ctrl.close();
    });

    // ...
  });
}
```

When a Dart app starts in debug mode, it exposes the Dart Observatory (VM
Service) on an HTTP port.
[`FlutterDriver.connect`](https://docs.flutter.io/flutter/flutter_driver/FlutterDriver/connect.html)
connects to the Dart Observatory and finds the isolate for the mod named in
`fuchsiaModuleTarget`. Behind the scenes, Flutter Driver uses
[`FuchsiaCompat`](https://github.com/flutter/flutter/blob/master/packages/flutter_driver/lib/src/common/fuchsia_compat.dart)
and
[`FuchsiaRemoteConnection`](https://github.com/flutter/flutter/blob/master/packages/fuchsia_remote_debug_protocol/lib/src/fuchsia_remote_connection.dart)
to search over all the Dart VMs running on the device to find the isolate that
matches your `fuchsiaModuleTarget`. Once this is found, Flutter Driver opens a
websocket over which to send RPCs which were registered by your mod under test
in
[`enableFlutterDriverExtension()`](https://docs.flutter.io/flutter/flutter_driver_extension/enableFlutterDriverExtension.html).

If you’d like to see an example test that pushes a few buttons, you can check
[here](https://fuchsia.googlesource.com/topaz/+/master/examples/test/driver_example_mod/test/driver_example_mod_test.dart).

### Component manifest

A
[component manifest](/docs/the-book/package_metadata.md#Component-manifest)
allows the test to run as a hermetic
[test component](/docs/development/tests/test_component.md)
under its own dedicated environment that will sandbox its services and tear
everything down on completion or failure. This is particularly important for
Flutter Driver tests and other graphical tests as only one Scenic instance may
own the display controller at a time, so any such test that does not properly
clean up can cause subsequent tests to fail.

The component manifest for our tests is
[driver_example_mod_tests.cmx](https://fuchsia.googlesource.com/topaz/+/master/examples/test/driver_example_mod/meta/driver_example_mod_tests.cmx).

```json
{
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.fonts.Provider": "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx",
                "fuchsia.sysmem.Allocator": "fuchsia-pkg://fuchsia.com/sysmem_connector#meta/sysmem_connector.cmx",
                "fuchsia.tracelink.Registry": "fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx",
                "fuchsia.ui.input.ImeService": "fuchsia-pkg://fuchsia.com/ime_service#meta/ime_service.cmx",
                "fuchsia.ui.policy.Presenter": "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx",
                "fuchsia.ui.scenic.Scenic": "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx",
                "fuchsia.vulkan.loader.Loader": "fuchsia-pkg://fuchsia.com/vulkan_loader#meta/vulkan_loader.cmx"
            },
            "system-services": [
                "fuchsia.net.SocketProvider"
            ]
        }
    },
    "program": {
        "data": "data/driver_example_mod_tests"
    },
    "sandbox": {
        "features": [
            "shell"
        ],
        "services": [
            "fuchsia.net.SocketProvider",
            "fuchsia.sys.Environment"
        ]
    }
}
```

The
[injected-services](/docs/development/tests/test_component.md#run-external-services)
entry starts the hermetic services our mod will need, mostly related to
graphics. In addition, the `fuchsia.net.SocketProvider` system service and
`shell` feature are needed to allow Flutter Driver to interact with the Dart
Observatory.

### BUILD.gn target

The test itself also needs a target in the
[`BUILD.gn`](https://fuchsia.googlesource.com/topaz/+/HEAD/examples/test/driver_example_mod/BUILD.gn).

```gn
dart_fuchsia_test("driver_example_mod_tests") {
  deps = [
    "//sdk/fidl/fuchsia.sys",
    "//third_party/dart-pkg/git/flutter/packages/flutter_driver",
    "//third_party/dart-pkg/pub/test",
    "//topaz/public/dart/fuchsia_services",
  ]

  meta = [
    {
      path = rebase_path("meta/driver_example_mod_tests.cmx")
      dest = "driver_example_mod_tests.cmx"
    },
  ]

  environments = []

  # Flutter driver is only available in debug builds.
  if (is_debug) {
    environments += [
      nuc_env,
      vim2_env,
    ]
  }
}
```

[`dart_fuchsia_test`](https://fuchsia.googlesource.com/topaz/+/master/runtime/dart/dart_fuchsia_test.gni)
defines a Dart test that runs on a Fuchsia device. It uses each file in the
`test` subfolder that ends in `_test.dart` as an entrypoint for tests. In
addition, it links the component manifest for the tests and specifies the
[environments](/docs/development/tests/environments.md)
in which to run the test in automated testing (CI/CQ). (See also the predefined
environments in
[//build/testing/environments.gni](/build/testing/environments.gni).)

### Topaz Package

Once you have this target available, you can add it to the build tree. In the
case of `driver_example_mod`, this can be done in
[`//topaz/packages/examples:tests`](https://fuchsia.googlesource.com/topaz/+/HEAD/packages/examples/BUILD.gn)
to be available in `//topaz/bundles:buildbot` and other configurations, like so:

```gn
group("tests") {
  testonly = true
  public_deps = [
    # ...
    "//topaz/examples/test/driver_example_mod",
    "//topaz/examples/test/driver_example_mod:driver_example_mod_tests",
    # ...
  ]
}
```

With that you’re ready to run your test on your device.

## Running Your Tests

To run your tests, you first need to make sure you're building a configuration
that includes your test packages. One typical way is to use the `core` product
and `//topaz/bundles:buildbot` bundle, as that is what the CI/CQ bots use.

```bash
$ fx set core.arm64 --with //topaz/bundles:buildbot
```

(If you are on an Acer or Nuc, use `x64` rather than `arm64` as the board.)

Then, build and pave or OTA as necessary.

```bash
$ fx build
$ fx serve / ota / reboot
```

The tests can then be run using

```bash
$ fx run-test driver_example_mod_tests
```
