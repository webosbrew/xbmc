![Kodi Logo](resources/banner_slim.png)

# webOS build guide
This guide has been tested with an adapted buildroot configuration where the Linux Kernel was changed to 5.4.96 and glibc was set to 2.31, this was done so to match the same configuration found on webOS 7 devices. Instructions for doing so is included in this guide. The host OS used for compilation was Ubuntu 22.10 (64-bit). Other combinations may work but we provide no assurances that other combinations will build correctly and run identically to Team Kodi releases. It is meant to cross-compile Kodi for webOS 7+ (LG 2022 models) using **[Kodi's unified depends build system](../tools/depends/README.md)**. Please read it in full before you proceed to familiarize yourself with the build procedure. Note that you do not need to "root" your TV to install Kodi.

## Table of Contents
1. **[Document conventions](#1-document-conventions)**
2. **[Prerequisites](#2-prerequisites)**
3. **[Configure the tool chain](#3-Configure-the-tool-chain)**  
  3.1. **[3.1. Downgrade flatc binary](#31-Downgrade-flatc-binary)**
4. **[Configure ares-cli tools](#4-Configure-ares-cli-tools)**
5. **[Get the source code](#5-get-the-source-code)**
6. **[Configure and build tools and dependencies](#6-configure-and-build-tools-and-dependencies)**  
  6.1. **[Advanced Configure Options](#61-Advanced-Configure-Options)**
7. **[Generate Kodi Build files](#7-Generate-Kodi-Build-files)**  
  7.1. **[Generate Project Files](#71-Generate-Project-Files)**  
  7.2. **[Add Binary Addons to Project](#72-Add-Binary-Addons-to-Project)**
8. **[Build](#8-build)**  
  8.1. **[Build kodi binary](#81-Build-kodi-binary)**
9. **[Packaging kodi to distribute as an IPK](#9-Packaging-kodi-to-distribute-as-an-IPK)**  
  9.1. **[Add missing libraries to your IPK](#91-Add-missing-libraries-to-your-IPK)**  
  9.2. **[Create the IPK](#92-Create-the-IPK)**  
10. **[Install](#10-Install)**  
  10.1. **[Using ares-cli to install](#101-Using-ares-cli-to-install)**  
11. **[Debugging ](#11-Debugging)**
12. **[Uninstall](#12-Uninstall)**  
  12.1. **[Using ares-cli to uninstall](#121-Using-ares-cli-to-uninstall)**

## 1. Document conventions
This guide assumes you are using `terminal`, also known as `console`, `command-line` or simply `cli`. Commands need to be run at the terminal, one at a time and in the provided order.

This is a comment that provides context:
```
this is a command
this is another command
and yet another one
```

**Example:** Clone Kodi's current master branch:
```
git clone https://github.com/xbmc/xbmc kodi
```

Commands that contain strings enclosed in angle brackets denote something you need to change to suit your needs.
```
git clone -b <branch-name> https://github.com/xbmc/xbmc kodi
```

**Example:** Clone Kodi's current Krypton branch:
```
git clone -b Krypton https://github.com/xbmc/xbmc kodi
```

Several different strategies are used to draw your attention to certain pieces of information. In order of how critical the information is, these items are marked as a note, tip, or warning. For example:

**NOTE:** Linux is user friendly... It's just very particular about who its friends are.
**TIP:** Algorithm is what developers call code they do not want to explain.
**WARNING:** Developers don't change light bulbs. It's a hardware problem.

**[back to top](#table-of-contents)** | **[back to section top](#1-document-conventions)**

## 2. Prerequisites
* **[LG developer account](https://webostv.developer.lge.com/develop/getting-started/preparing-lg-account)**. This is required to access developer mode on the webOS device.
* **[Actvate developer mode on your webOS device]**. Follow the instructions as per **[Installing Developer Mode app page](https://webostv.developer.lge.com/develop/getting-started/developer-mode-app#installing-developer-mode-app)**.
* **[Download ares CLI tools](https://webostv.developer.lge.com/develop/tools/cli-installation)**. These are required to package the kodi binary into a IPK format that webOS understands.
* **[Download and setup a compatible toolchain to compile kodi]**. Suitable toolchains can be found on the LG OpenSource website: **https://opensource.lge.com/**. You will need to enter the model number of your TV. Another toolchain which we actually used in this guide is **[buildroot-nc4](https://github.com/openlgtv/buildroot-nc4)** which may be more suitable as it comes with newer tools such as GCC 12.2.0.

* Device with **webOS 7.0 or newer**.

Team Kodi CI infrastructure is limited, and therefore we only have the single combination tested. Newer webOS versions may work, however the team does not actively test/use these versions, so use with caution. Earlier versions may work, however we dont actively support them, so use with caution.

**[back to top](#table-of-contents)**

## 3. Configure the tool chain

Assuming you are using builroot-nc4 there will be some changes that need to be made. The default buildroot-nc4 is setup to build with very old devices, bumping the glibc version and kernel is recommended as so:

Edit configs/webos_tv_defconfig
```
change BR2_DEFAULT_KERNEL_VERSION to a more recent version that matches your device e.g. 5.4.96
```

Edit package/glibc/glibc.mk
```
change GLIBC_VERSION to one that matches the version on your device, for example 2.31
```

You will also need to add a sha256 hash for whatever glibc version you end up using:
```
sha256  bcc5496be806fba7c0122201b2e246b02170dfd791ad6ac90bf3a52e9b2e27d4  glibc-2.31.tar.gz
```

After we have finished setting up the tool chain we now need to compile it:

```
make webos_tv_defconfig
make -j$(getconf _NPROCESSORS_ONLN)
```

After compilation, you will need to add the toolchain to your PATH so that it can be found:
```
export SDK_PATH="/home/developer/buildroot-nc4/output/host"
export "PATH=$PATH:$SDK_PATH/bin:$SDK_PATH/sbin"
```

### 3.1. Downgrade flatc binary

The version of FlatBuffers in Kodi is currently locked to v2.0.0, the flatc binary installed onto your Linux system is likely
to be much newer and therefore incompatible. You can get a version compatible here:

```
https://github.com/google/flatbuffers/releases/download/v2.0.0/Linux.flatc.binary.clang++-9.zip
```

Place flatc somewhere in your path, for example `/usr/local`


## 4. Configure ares-cli tools

You need to add your webOS device so that it can be used later on in this guide. This step is only required once:

```
ares-setup-device -s
```

Note that the username is prisoner and the password is blank.

You will need to note the name of your device for later e.g. [LG]_webOS_TV_OLED65C24LA

## 5. Get the source code
Change to your `home` directory:
```
cd $HOME
```

Clone Kodi's current master branch:
```
git clone https://github.com/xbmc/xbmc kodi
```

**[back to top](#table-of-contents)**

## 6. Configure and build tools and dependencies
Kodi should be built as a 32bit program for webOS. webOS currently operates with a 32-bit OS but the kernel is 64-bit. The dependencies are built in `$HOME/kodi/tools/depends` and installed into `/media/developer/apps/usr/palm/applications/org.xbmc.kodi/xbmc-deps`.
**NOTE:** `--with-platform` is mandatory for all webOS platforms

Configure build:
```
cd $HOME/kodi/tools/depends
./bootstrap
./configure --prefix=/media/developer/apps/usr/palm/applications/org.xbmc.kodi/xbmc-deps --host=arm-linux \
            --with-rendersystem=gles --enable-debug=no \
            --with-target-cflags='-march=armv7-a -mfloat-abi=softfp -mfpu=neon' \
            --with-target-cxxflags='-static-libstdc++' \
            --with-platform=webos
```

Build tools and dependencies:
```
make -j$(getconf _NPROCESSORS_ONLN)
```

**TIP:** By adding `-j<number>` to the make command, you can choose how many concurrent jobs will be used and expedite the build process. It is recommended to use `-j$(getconf _NPROCESSORS_ONLN)` to compile on all available processor cores. The build machine can also be configured to do this automatically by adding `export MAKEFLAGS="-j$(getconf _NPROCESSORS_ONLN)"` to your shell config (e.g. `~/.bashrc`).

**WARNING:** Look for the `Dependencies built successfully.` success message. If in doubt run a single threaded `make` command until the message appears. If the single make fails, clean the specific library by issuing `make -C target/<name_of_failed_lib> distclean` and run `make`again.

**NOTE:** You may want to modify arch, float-abi or fpu to obtain the best performance out of the target CPU, however the defaults are recommended for now until you have a working build:
```
./configure --with-target-cflags='-march=armv7-a -mfloat-abi=softfp -mfpu=neon'
```

### 6.1. Advanced Configure Options


**All platforms:**

```
--prefix=<path>
```
  specify path on target device. By default we will likely be packaging as org.xbmc.kodi and it will installed to /media/developer/apps/usr/palm/applications/{org.xbmc.kodi}. All developer apps will get installed to this base path plus {your-app-name}

```
--enable-debug=<yes:no>
```
  enable debugging information (default is yes)

```
--disable-ccache
```
  disable ccache

```
--with-tarballs=<path>
```
  path where tarballs will be saved [prefix/xbmc-tarballs]

```
--with-cpu=<cpu>
```
  optional. specify target cpu. guessed if not specified

```
--with-linker=<linker>
```
  specify linker to use. (default is ld)

```
--with-platform=<platform>
```
  target platform

```
--enable-gplv3=<yes:no>
```
  enable gplv3 components. (default is yes)

```
--with-target-cflags=<cflags>
```
  C compiler flags (target)

```
--with-target-cxxflags=<cxxflags>
```
  C++ compiler flags (target)

```
--with-target-ldflags=<ldflags>
```
  linker flags. Use e.g. for -l<lib> (target)

```
--with-ffmpeg-options
```
  FFmpeg configure options, e.g. --enable-vaapi (not relevant however to webOS)

**[back to top](#table-of-contents)**

## 7. Generate Kodi Build files
Before you can build Kodi, the build files have to be generated with CMake. CMake is built as part of the dependencies and doesn't have to be installed separately. A toolchain file is also generated and is used to configure CMake.
Default behaviour will not build binary addons. To add addons to your build go to **[Add Binary Addons to Project](#52-(Optional)-Add-Binary-Addons-to-Project)**

## 7.1. Generate Project Files

Before you can build Kodi, the project has to be generated with CMake. CMake is built as part of the dependencies and doesn't have to be installed separately. A toolchain file is also generated and is used to configure CMake.

Create an out-of-source build directory:
```
mkdir /media/developer/apps/usr/palm/applications/org.xbmc.kodi
```

Generate project for webOS:
```
make -C tools/depends/target/cmakebuildsys BUILD_DIR=/media/developer/apps/usr/palm/applications/org.xbmc.kodi \
	CMAKE_EXTRA_ARGUMENTS="-DCORE_PLATFORM_NAME=webos"
```

**TIP:** BUILD_DIR can be omitted, and project will be created in $HOME/kodi/build
Change all relevant paths onwards if omitted.

Additional cmake arguments can be supplied via the CMAKE_EXTRA_ARGUMENTS command line variable

An example of extra arguments to remove dependencies dbus, CEC and pipewire from the build:
````
make -C tools/depends/target/cmakebuildsys BUILD_DIR=/media/developer/apps/usr/palm/applications/org.xbmc.kodi \
	CMAKE_EXTRA_ARGUMENTS="-DCORE_PLATFORM_NAME=webos -DENABLE_DBUS=OFF -DENABLE_CEC=OFF -DENABLE_PIPEWIRE=OFF -DHAVE_LINUX_UDMABUF=OFF"
````

## 7.2. Add Binary Addons to Project

You can find a complete list of available binary add-ons **[here](https://github.com/xbmc/repo-binary-addons)**.

Binary addons are optional.

To build all add-ons:
```
make -j$(getconf _NPROCESSORS_ONLN) -C tools/depends/target/binary-addons PREFIX=/media/developer/apps/usr/palm/applications/org.xbmc.kodi/xbmc-deps
```

Build specific add-ons:
```
make -j$(getconf _NPROCESSORS_ONLN) -C tools/depends/target/binary-addons PREFIX=/media/developer/apps/usr/palm/applications/org.xbmc.kodi/xbmc-deps ADDONS="audioencoder.flac pvr.vdr.vnsi audiodecoder.snesapu"
```

Build a specific group of add-ons:
```
make -j$(getconf _NPROCESSORS_ONLN) -C tools/depends/target/binary-addons PREFIX=/media/developer/apps/usr/palm/applications/org.xbmc.kodi/xbmc-deps ADDONS="pvr.*"
```

To build specific addons or help with regular expression usage for ADDONS_TO_BUILD, view ADDONS_TO_BUILD section located at [Kodi add-ons CMake based buildsystem](../cmake/addons/README.md)

**TIP:** Binary add-ons added to the generated project can be built independently of the Kodi app by selecting the scheme/target `binary-addons` in the project.

**[back to top](#table-of-contents)** | **[back to section top](#5-Generate-Kodi-Build-files)**

## 8. Build

### 8.1. Build kodi binary

In '/media/developer/apps/usr/palm/applications/org.xbmc.kodi`

```
make -j$(getconf _NPROCESSORS_ONLN)
```

This will create a `kodi-webos` file located in `/media/developer/apps/usr/palm/applications/org.xbmc.kodi`. This App needs to be packaged into a IPK which we will go over later.

**WARNING:** Building for simulator is NOT supported.

**[back to top](#table-of-contents)** | **[back to section top](#6-Build)**

## 9 Packaging kodi to distribute as an IPK

## 9.1 Create the IPK

CMake generates a target called `ipk` which will package Kodi ready for distribution.

Create package:
```
cd $HOME/kodi/tools/webOS/packaging
make ipk
```

Generated `ipk` file will be inside `/media/developer/apps/usr/palm/applications`. The filename may differ from this guide which is due to the version of Kodi which at time of writing will create a `ipk` called org.xbmc.kodi_20.90.101_all.ipk.

You can also create a minimal package without any libs:
```
make compact-ipk
```

## 10. Install

We use the same ares-cli tools to install.

## 10.1. Using ares-cli to install

```
ares-install ./org.xbmc.kodi_20.90.101_all.ipk --d <your tv>
e.g. [LG]_webOS_TV_OLED65C24LA
```

## 11. Debugging

You can connect over ssh directly to your webOS device to debug any issues:

```
ssh -oHostKeyAlgorithms=+ssh-rsa -oPubkeyAcceptedAlgorithms=+ssh-rsa -i ~/.ssh/\<your tv>-p 9922 prisoner@<ip of tv> bash -i
```

You will be put into /media/developer by default.
cd `/media/developer/apps/usr/palm/applications/org.xbmc.kodi/.kodi/temp` to find kodi.log

You can also edit `kodi.sh` and place strace just before launching kodi to debug any missing libraries:

```
strace -t -o /media/developer/apps/usr/palm/applications/org.xbmc.kodi/trace.temp ./kodi-webos --debug
```

## 12. Uninstall

We use the same ares-cli tools to uninstall.

## 12.1. Using ares-cli to uninstall

```
ares-install --remove org.xbmc.kodi --d <your tv>
e.g. [LG]_webOS_TV_OLED65C24LA
```

**[back to top](#table-of-contents)**

