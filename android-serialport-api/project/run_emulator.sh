#!/bin/sh
{
 adb wait-for-device
 adb shell chmod 666 /dev/ttyS2
} &
emulator-arm -avd Android_1.5 -qemu -serial stdio

