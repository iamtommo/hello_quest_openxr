#!/bin/bash
PACKAGE_STRUCTURE=com/makepad/hello_quest
ANDROID_HOME=~/Library/Android/sdk
NDK_HOME=~/Library/Android/sdk/ndk/21.4.7075529
JAVA_HOME=~/Library/Application\ Support/JetBrains/Toolbox/apps/AndroidStudio/ch-0/221.6008.13.2211.9514443/Android\ Studio.app/Contents/jbr/Contents/Home/
OVR_HOME=~/dev/ovr_openxr_mobile_sdk_47.0
OPENXR_HOME=~/dev/OpenXR-SDK
PATH=$PATH:$ANDROID_HOME/build-tools/33.0.1
PATH=$PATH:$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin
PATH=$JAVA_HOME/bin:$PATH

rm -rf build
mkdir -p build
pushd build > /dev/null

echo Using javac $(javac -version)
javac\
	-classpath $ANDROID_HOME/platforms/android-26/android.jar\
	-d .\
	../src/*.java

export DEX_PREOPT_DEFAULT=nostripping
d8 --lib $ANDROID_HOME/platforms/android-26/android.jar ${PACKAGE_STRUCTURE}/MainActivity.class

mkdir -p lib/arm64-v8a
pushd lib/arm64-v8a > /dev/null
cp $OVR_HOME/OpenXR/Libs/Android/arm64-v8a/Debug/libopenxr_loader.so .
$NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android26-clang\
    -march=armv8-a\
    -shared\
    -I ../../../src\
    -I $NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/include/\
    -I $OVR_HOME/OpenXR/Include\
    -I $OPENXR_HOME/include\
    -L $NDK_HOME/platforms/android-26/arch-arm64/usr/lib\
    -L.\
    -landroid\
    -llog\
    -lEGL\
    -lGLESv2\
    -lopenxr_loader\
    -o libmain.so\
   ../../../src/*.c
popd > /dev/null

aapt\
	package\
	-F hello_quest.apk\
	-I $ANDROID_HOME/platforms/android-26/android.jar\
	-M ../src/AndroidManifest.xml\
	-f
aapt add hello_quest.apk classes.dex
aapt add hello_quest.apk lib/arm64-v8a/libmain.so
aapt add hello_quest.apk lib/arm64-v8a/libopenxr_loader.so
apksigner\
	sign\
	-ks ~/.android/debug.keystore\
	--ks-key-alias androiddebugkey\
	--ks-pass pass:android\
	hello_quest.apk

popd > /dev/null