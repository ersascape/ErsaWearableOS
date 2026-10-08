# Building and registering apps

EWP apps are C++ components compiled into the firmware image. The firmware
starts one app at a time through `ApplicationManager`; it does not load native
app binaries at runtime.

## App structure

Keep app-specific behavior in `src/apps/`. Existing apps commonly split their
screen logic into a small module and adapt it to `ersa::app::Application` in
`src/apps/apps_registry.cpp`.

Implement the application contract from
[`include/ersa/app/application.h`](https://github.com/ersascape/ErsaWearableOS/blob/master/include/ersa/app/application.h):

- `getId()` returns a stable unique ID used by navigation.
- `getTitle()` returns the display name.
- `render()` draws through the `hal::IDisplay` interface.
- `onEvent()` handles button and system events.
- `onEnter()` and `onExit()` acquire and release app-scoped state.
- `tick()` is for short, non-blocking work; do not wait on network or BLE I/O
  from the UI loop.
- `getPartialBounds()` declares the area to refresh for app updates.

Apps should use framework services and HAL contracts. Do not include Arduino,
ESP-IDF, or concrete peripheral-driver headers in app code. For network work,
use the existing service flow and return promptly so input, BLE events, and
clock updates continue to run.

## Registering an app

1. Add the app module under `src/apps/`, with a header and implementation when
   the app has reusable screen logic.
2. Include its header in `src/apps/apps_registry.cpp`.
3. Add an `Application` adapter class there. Give it a unique ID and title,
   forward rendering and input to the app module, and handle lifecycle or
   periodic updates as needed.
4. Create a static app instance beside the other registered app instances.
5. Add `manager.registerApp(&myApp);` in `registerAllApps()` in that same file.
   The UI calls `registerAllApps()` during startup.
6. To make the app visible in the drawer, add a drawer item in
   `src/apps/app_drawer.h` and `src/apps/app_drawer.cpp`, then handle that item
   in the `DrawerApp` event switch in `src/apps/apps_registry.cpp`.
7. Build and run the checks below. Confirm drawer navigation, back navigation,
   button behavior, rendering bounds, and wake/sleep behavior on the watch.

`src/CMakeLists.txt` discovers project `.cpp` and `.c` files recursively, so a
new source file under `src/` is included in the firmware build automatically.
App registration is still explicit; adding a source file alone does not make an
app navigable.

## Build and check

From the repository root:

```sh
make test       # host-side platform and service tests
make firmware   # ESP32-C3 firmware build
make docs       # wiki and generated C++ API reference
```

The firmware output is `.pio/build/ErsaWearable/firmware.bin`. The same build
can be run with `./scripts/pio.sh run`. PlatformIO uses the pinned project
environment in `platformio.ini` and downloads its framework and libraries when
needed.

## App store and installable apps

There is no app store or runtime app installer today. App code is linked into
the firmware, and updates replace one of the two OTA firmware slots. The
current image already uses about 95% of one slot, and the partition table has
no app-data filesystem. There is no dynamic linker or stable binary ABI for
third-party native apps.

A hosted app catalog could list firmware releases, source projects, or
compatible app bundles for developers to build themselves. Installing native
apps independently would require a different runtime or operating system,
storage reserved for app packages, a stable application ABI, and a security
model. Those are substantial platform changes; the current flash layout does
not have room for a conventional app store.

## SDK and C++ reference

The generated [C++ API reference](https://pkgs-wearables.ersa.dev/wiki/api/)
documents project headers and implementation, including HALs, services, app
contracts, and board support. Third-party SDKs such as Arduino and ESP-IDF are
downloaded by PlatformIO at their pinned versions; their own documentation is
the authoritative reference for those APIs. The app contract deliberately
keeps vendor SDK headers out of application code.
