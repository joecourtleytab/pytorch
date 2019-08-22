#!/usr/bin/env bash
set -eux -o pipefail

# sh ./build_android_gradle_dockerpart.sh

echo "build_android_gradle.sh"
echo "$(pwd)"
echo "HOME:$(~/.)"

export ANDROID_HOME=/opt/android/sdk
alias sdkmanager=/opt/android/sdk/tools/bin/sdkmanager

export GRADLE_VERSION=5.1.1
export GRADLE_HOME=/opt/gradle/gradle-$GRADLE_VERSION
alias gradle=$GRADLE_HOME/bin/gradle
PYTORCH_ANDROID_SRC_MAIN_DIR=~/workspace/android/pytorch_android/src/main

JNI_LIBS_DIR=${PYTORCH_ANDROID_SRC_MAIN_DIR}/jniLibs
mkdir -p $JNI_LIBS_DIR
JNI_LIBS_DIR_x86=${JNI_LIBS_DIR}/x86
mkdir -p $JNI_LIBS_DIR_x86
JNI_INCLUDE_DIR_x86=${JNI_INCLUDE_DIR}/x86
echo "ANDROID_GRADLE_BUILD_ONLY_X86:${ANDROID_GRADLE_BUILD_ONLY_X86}"

if [ -z "${ANDROID_GRADLE_BUILD_ONLY_X86}" ]; then
    BUILD_ANDROID_INCLUDE_DIR_x86=~/workspace/build_android_install_x86_32/install/include
    BUILD_ANDROID_LIB_DIR_x86=~/workspace/build_android_install_x86_32/install/lib

    BUILD_ANDROID_INCLUDE_DIR_x86_64=~/workspace/build_android_install_x86_64/install/include
    BUILD_ANDROID_LIB_DIR_x86_64=~/workspace/build_android_install_x86_64/install/lib

    BUILD_ANDROID_INCLUDE_DIR_arm_v7a=~/workspace/build_android_install_arm_v7a/install/include
    BUILD_ANDROID_LIB_DIR_arm_v7a=~/workspace/build_android_install_arm_v7a/install/lib

    BUILD_ANDROID_INCLUDE_DIR_arm_v8a=~/workspace/build_android_install_arm_v8a/install/include
    BUILD_ANDROID_LIB_DIR_arm_v8a=~/workspace/build_android_install_arm_v8a/install/lib

    JNI_LIBS_DIR_x86_64=${JNI_LIBS_DIR}/x86_64
    mkdir -p $JNI_LIBS_DIR_x86_64
    JNI_LIBS_DIR_arm_v7a=${JNI_LIBS_DIR}/armeabi-v7a
    mkdir -p $JNI_LIBS_DIR_arm_v7a
    JNI_LIBS_DIR_arm_v8a=${JNI_LIBS_DIR}/arm64-v8a
    mkdir -p $JNI_LIBS_DIR_arm_v8a

    JNI_INCLUDE_DIR=${PYTORCH_ANDROID_SRC_MAIN_DIR}/cpp/libtorch_include
    mkdir -p $JNI_INCLUDE_DIR

    JNI_INCLUDE_DIR_x86_64=${JNI_INCLUDE_DIR}/x86_64
    JNI_INCLUDE_DIR_arm_v7a=${JNI_INCLUDE_DIR}/armeabi-v7a
    JNI_INCLUDE_DIR_arm_v8a=${JNI_INCLUDE_DIR}/arm64-v8a

    ln -s ${BUILD_ANDROID_INCLUDE_DIR_x86_64} ${JNI_INCLUDE_DIR_x86_64}
    ln -s ${BUILD_ANDROID_INCLUDE_DIR_arm_v7a} ${JNI_INCLUDE_DIR_arm_v7a}
    ln -s ${BUILD_ANDROID_INCLUDE_DIR_arm_v8a} ${JNI_INCLUDE_DIR_arm_v8a}

    ln -s ${BUILD_ANDROID_LIB_DIR_x86_64}/libc10.so ${JNI_LIBS_DIR_x86_64}/libc10.so
    ln -s ${BUILD_ANDROID_LIB_DIR_x86_64}/libtorch.so ${JNI_LIBS_DIR_x86_64}/libtorch.so

    ln -s ${BUILD_ANDROID_LIB_DIR_arm_v7a}/libc10.so ${JNI_LIBS_DIR_arm_v7a}/libc10.so
    ln -s ${BUILD_ANDROID_LIB_DIR_arm_v7a}/libtorch.so ${JNI_LIBS_DIR_arm_v7a}/libtorch.so

    ln -s ${BUILD_ANDROID_LIB_DIR_arm_v8a}/libc10.so ${JNI_LIBS_DIR_arm_v8a}/libc10.so
    ln -s ${BUILD_ANDROID_LIB_DIR_arm_v8a}/libtorch.so ${JNI_LIBS_DIR_arm_v8a}/libtorch.so
else
    #x86 only
    BUILD_ANDROID_INCLUDE_DIR_x86=~/workspace/build_android/install/include
    BUILD_ANDROID_LIB_DIR_x86=~/workspace/build_android/install/lib
fi

ln -s ${BUILD_ANDROID_INCLUDE_DIR_x86} ${JNI_INCLUDE_DIR_x86}
ln -s ${BUILD_ANDROID_LIB_DIR_x86}/libc10.so ${JNI_LIBS_DIR_x86}/libc10.so
ln -s ${BUILD_ANDROID_LIB_DIR_x86}/libtorch.so ${JNI_LIBS_DIR_x86}/libtorch.so

echo "ANDROID_HOME:${ANDROID_HOME}"
echo "ANDROID_NDK_HOME:${ANDROID_NDK_HOME}"

export GRADLE_LOCAL_PROPERTIES=~/workspace/android/local.properties
rm -f $GRADLE_LOCAL_PROPERTIES
echo "sdk.dir=/opt/android/sdk" >> $GRADLE_LOCAL_PROPERTIES
echo "ndk.dir=/opt/ndk" >> $GRADLE_LOCAL_PROPERTIES

if [ -z "${ANDROID_GRADLE_BUILD_ONLY_X86}" ]; then
    gradle -p ~/workspace/android/ assembleRelease
else
    gradle -PABI_FILTERS=x86 -p ~/workspace/android/ assembleRelease
fi


find . -type f -name *aar | xargs ls -lah
