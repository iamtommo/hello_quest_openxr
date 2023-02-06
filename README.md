# Hello Quest OpenXR

## Forked from [makepad/hello_quest](https://github.com/makepad/hello_quest)
Adapted for OpenXR runtime

![image info](./image.png)

This is a minimal example project for Linux that renders a colored cube to the
Meta Quest 2, and requires neither Android Studio nor Gradle to build.

Instead of using Android Studio or Gradle, the project contains a build script
that uses the build tools included with the Android SDK directly.

The project source code is based on the `VrCubeWorld_NativeActivity` sample from
the Oculus Quest SDK, but heavily modified to only include the absolute minimum
functionality required to render a single cube. In particular, I've removed
support for:

* Multithreading
* Multiviews
* Multisampling
* Clamp to border textures
* Instancing

The resulting code is less than 1000 lines, and should serve as a useful
starting point for those wanting to get started with native development on the
Quest

Instead of linking separately against the `android_native_app_glue` library
(which provides the bindings between a `NativeActivity` in Java and our C code),
I've added the headers and source code for this library to the project directly.

## Prerequisites

No environment variables are required, simply change the paths in `build.sh`

* Android SDK (`ANDROID_HOME`)
* Android NDK (`NDK_HOME`)
* Java Runtime Environment (JRE) (`JAVA_HOME`)
* Meta OpenXR SDK (`OVR_HOME`)
* OpenXR SDK (`OPENXR_HOME`)

## Usage

I've created several shell scripts that allow you to build, install, start, and
stop the application on the Quest.

All these scripts assume that the Android SDK platform tools are in your `PATH`.
To add the Android SDK platform tools to your path, run:

export PATH=$ANDROID_HOME/platform-tools:$PATH

To build the application, run:

```build.sh```

The subsequent steps require that the Quest is connected to your machine over
USB.

To install the application, run:

```install.sh```

To start the application, run:

```./start.sh```

To stop the application, run:

```./stop.sh```

All-in-one build & run:
```./run.sh```

To tail logs, run:
```./logs.sh```
