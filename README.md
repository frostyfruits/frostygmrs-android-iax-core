# FrostyGMRS Android IAX Core

This repository contains the core Android and native C++ IAX client pieces extracted from FrostyGMRS Mobile so they can be reused in another Android project without publishing the full app.

## What is included

This bundle includes:

- Kotlin engine code for connect, disconnect, RX audio playback, TX audio capture, and PTT handling
- JNI bridge code between Kotlin and native C++
- Native IAX session, frame, auth, builder, and UDP transport code
- CMake build file for the native library

Included files:

- `RealIaxEngine.kt`
- `NativeIaxBridge.kt`
- `NativeIaxBridge.cpp`
- `IaxNativeClient.cpp`
- `IaxNativeClient.h`
- `iax_session.cpp`
- `iax_session.h`
- `iax_builder.cpp`
- `iax_builder.h`
- `iax_auth.cpp`
- `iax_auth.h`
- `iax_udp_transport.cpp`
- `iax_udp_transport.h`
- `iax_frame.h`
- `libiax2_adapter.cpp`
- `libiax2_adapter.h`
- `CMakeLists.txt`

## Current behavior

This code is currently set up around a phone-mode style server flow that was used in FrostyGMRS Mobile.

PTT behavior is:

- PTT down sends DTMF `*99`
- TX audio starts after a short delay
- PTT up sends DTMF `#`

If your server does not use that behavior, you will need to adjust the native PTT flow in `IaxNativeClient.cpp`.

## What was cleaned before publishing

This bundle was cleaned for GitHub sharing by removing or reducing logs that exposed runtime connection values such as:

- host
- port
- username
- remote node
- auth response details
- JNI object pointer details

The core logic and Frosty naming were left intact so the code can still be dropped into another project with minimal rework.

## What is not included

This repository does not include the rest of the FrostyGMRS Mobile app, such as:

- node browser UI
- app screens and navigation
- login storage
- app-specific service wrappers outside this core set
- private server configuration

## Integration notes

At a high level, another Android project needs to:

1. Include the Kotlin files in the app source tree.
2. Include the native C++ files in the app native source tree.
3. Build the native library with CMake.
4. Load the native library from Kotlin.
5. Request microphone permission.
6. Feed microphone PCM audio into the TX path while PTT is held.
7. Read RX PCM audio from the native side and play it through `AudioTrack`.

## Android notes

This code assumes an Android project that already has:

- NDK support enabled
- CMake configured in Gradle
- microphone permission handling
- an audio path for `AudioRecord` and `AudioTrack`

## Security note

Do not hardcode your real server values, passwords, tokens, or private endpoints in source before publishing your own project.

## License

This repository is provided under the MIT License. See `LICENSE`.

## Third-party code and attribution

This bundle includes files that may contain protocol or utility logic originally adapted from other sources before extraction into this repository. If you know any specific file was copied or adapted from another project, add the required attribution and license notice before publishing.
