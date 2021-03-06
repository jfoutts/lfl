http://lucidfusionlabs.com/svn/lfl/README.txt
=============================================

OVERVIEW
--------

The "lfapp" API primarly consists of the: Application, Window, and Scene
classes, plus the 7 Modules: Audio, Video, Input, Assets, Network, Camera,
and CUDA.

The key implementation files are 
[lfapp/lfapp.h](http://lucidfusionlabs.com/websvn/filedetails.php?repname=lfl&path=%2Flfapp%2Flfapp.h) and
[lfapp/lfapp.cpp](http://lucidfusionlabs.com/websvn/filedetails.php?repname=lfl&path=%2Flfapp%2Flfapp.cpp).

Projects include:

- term:         LTerminal, a modern terminal
- editor:       LEditor, a text editor and IDE
- browser:      LBrowser, a HTML4/CSS2 web browser with V8 javascript
- image:        LImage, an image and 3D-model manipulation utility
- fs:           Fusion Server, a speech and image recognition server
- fv:           Fusion Viewer, a speech and image recognition client
- market:       Financial data visualization and automated trading code
- spaceball:    Spaceball Future, a multiplayer 3d game
- cb:           Crystal Bawl, a geopacket visualization screensaver
- chess:        LChess, a magic bitboard chess engine and FICS client
- quake:        LQuake, a quake clone
- senators:     IRC bots with NLP capabilties

The following build procedures apply to any app cloned from lfl/new_app_template.
See lfl/new_app_template/README to quick start your next app.


BUILDING
--------

svn co http://lucidfusionlabs.com/svn/lfl

LFL builds easily for Windows, Linux, Mac OSX, iPhone and Android.

* Replace "LTerminal" and "lterm" with "YourPackage" and "YourApp" to
build other apps.


BUILDING Windows
----------------

* use CMake 3.0.2

        [select c:\lfl for source and binaries]
        [Configure]
        [uncheck USE_MSVC_RUNTIME_LIBRARY_DLL]
        [Generate]

        start Visual Studio Command Prompt
        cd lfl\imports\judy\src
        build.bat

* use Visual Studio C++ 2013 Express
* Tools > Options > Text Editor > All Languages > Tabs > Insert Spaces

        c:\lfl\Project.sln
        [Build LTerminal]

        cd c:\lfl\term
        copy ..\debug\*.dll debug
        copy ..\lfapp\*.glsl assets
        copy ..\imports\berkelium\w32\bin\* Debug
        copy ..\imports\ffmpeg\w32\dll\*.dll Debug [overwrite:All]
        [Run]

        [Right click] term.nsi > Compile NSIS Script

* Windows installer lterminst.exe results


BUILDING Linux
--------------

* if yasm < 1 install http://www.tortall.net/projects/yasm/releases/yasm-1.2.0.tar.gz

        cd lfl
        ./imports/build.sh
        cmake .

        cd term
        make -j4
        ./pkg/lin.sh
        export LD_LIBRARY_PATH=./LTerminal
        ./LTerminal/lterm

        tar cvfz LTerminal.tgz LTerminal

* Linux package LTerminal.tgz results


BUILDING Mac
------------

* http://www.cmake.org/files/v3.0/cmake-3.0.2-Darwin64-universal.dmg
* Minimum of XCode 6 required

        cd lfl
        ./imports/build.sh
        cmake .

        cd term
        make -j4
        ./pkg/macprep.sh
        ./LTerminal.app/Contents/MacOS/lterm

        ./pkg/macpkg.sh

* OSX installer LTerminal.dmg results
* For C++ Interpreter setup ~/cling following http://root.cern.ch/drupal/content/cling-build-instructions
* For V8 Javascript setup ~/v8 following https://developers.google.com/v8/build then:

        sudo port install yasm
        export CXX="clang++ -std=c++11 -stdlib=libc++"
        export CC=clang
        export CPP="clang -E"
        export LINK="clang++ -std=c++11 -stdlib=libc++"
        export CXX_host=clang++
        export CC_host=clang
        export CPP_host="clang -E"
        export LINK_host=clang++
        export GYP_DEFINES="clang=1 mac_deployment_target=10.8"
        make native; cp -R out ~/v8; cp -R include ~/v8


BUILDING iPhone Device
----------------------

        cd lfl
        cmake -D LFL_IPHONE=1 .

        cd term
        make -j4
        cp -R assets term-iphone
        cp lfapp/*.glsl term-iphone/assets
        ./pkg/iphoneprep.sh
        ./pkg/iphonepkg.sh

        open term-iphone/term-iphone.xcodeproj

        [Change configuration to Device]
        [Build and run]
        cp lterm term-iphone/build/Debug-iphoneos/term-iphone.app/term-iphone
        cp skorp ~/Library//Developer/Xcode/DerivedData/skorp-iphone-cwokylhxlztdqwhdhxqzpqiemvoz/Build/Products/Debug-iphoneos/skorp-iphone.app/skorp-iphone
        [Build and run]

* iPhone Installer iLTerminal.ipa results


BUILDING iPhone Simulator
-------------------------

        cd lfl
        cmake -D LFL_IPHONESIM=1 .

        cd term
        make -j4
        cp -R assets term-iphone
        ./pkg/iphoneprep.sh
        ./pkg/iphonepkg.sh

        open term-iphone/term-iphone.xcodeproj
        [Change configuration to Simulator]
        [Build and run]
        cp lterm term-iphone/build/Debug-iphonesimulator/term-iphone.app/term-iphone
        [Build and run]

* iPhone Installer iLTerminal.ipa results


BUILDING Android
----------------

* http://www.oracle.com/technetwork/java/javase/downloads/java-se-jdk-7-download-432154.html
* http://www.eclipse.org/downloads/packages/eclipse-classic-37/indigor

* Android SDK http://developer.android.com/sdk/index.html
* Android NDK http://developer.android.com/sdk/ndk/index.html
* ADT Plugin http://developer.android.com/sdk/eclipse-adt.html#installing

        $HOME/android-sdk-linux_x86/tools/android
        [Install Platform android-13 + Google Play Services]
        [New Virtual Device]

        $HOME/android-ndk-r8b/build/tools/make-standalone-toolchain.sh \
        --platform=android-8 --install-dir=$HOME/android-toolchain

        cd lfl
        ** Modify ANDROIDROOT in CMakeLists.txt
        cmake -D LFL_ANDROID=1 .

        cd term
        make -j4
        cd term-android/jni
        ../../pkg/androidprebuild.sh
        rm ~/lfl-android/lfl/lfapp/lfjava/lfjava
        cd src && ln -s ~/android-ndk-r9/sources/cxx-stl/gnu-libstdc++ && cd ..
        vim lfapp/Android.mk # Change to: libskorp_lfapp.a
        ndk-build

        cd lfl/term/term-android/assets
        cp -R ../../assets .
        cp ../../lfapp/*.glsl assets

        cd lfl/term/term-android
        vi local.properties
        ant debug

        Eclipse > [Eclipse|Window] > Preferences > Android > SDK Location
        Eclipse > File > New > Other > Android Project > From Existing Source > term-android (Name: LTerminal, Target 2.2)
        LTerminal > Refresh
        LTerminal > Debug as Android Application

* Android Installer bin/LTerminal.apk results

* Setup eclipse_keystore

        LTerminal > Android Tools > Export Signed Application Package

* Signed Android Installer results


