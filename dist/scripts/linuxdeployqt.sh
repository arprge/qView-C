#!/usr/bin/env bash
set -e

wget -L -O qt5ct.tar.bz2 'https://sourceforge.net/projects/qt5ct/files/latest/download'
tar xf qt5ct.tar.bz2
rm -f qt5ct.tar.bz2
pushd qt5ct-*/
if [ -f CMakeLists.txt ]; then
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    sudo cmake --install build
else
    qmake
    make -j"$(nproc)"
    sudo make install
fi
popd

if [ -n "$1" ]; then
    VERSION="$1"
else
    VERSION=$(grep -m1 '^CMAKE_PROJECT_VERSION:' build/CMakeCache.txt | cut -d= -f2)
fi

# echo VERSION was set to $VERSION
# if [[ $2 == *"-extra-plugins"* ]]; then
#         PLUGINS=$2
# fi

wget -c -nv "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage"
chmod a+x linuxdeployqt-continuous-x86_64.AppImage

mkdir -p bin/appdir/usr
DESTDIR="$PWD/bin/appdir" cmake --install build --prefix /usr
cp dist/linux/hicolor/scalable/apps/com.interversehq.qView.svg bin/appdir/
cd bin
rm -f qview
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib/
export APPIMAGE_EXTRACT_AND_RUN=1
../linuxdeployqt-continuous-x86_64.AppImage appdir/usr/share/applications/com.interversehq.qView.desktop -appimage -updateinformation="gh-releases-zsync|jurplel|qView|latest|qView-*x86_64.AppImage.zsync" -extra-plugins=styles/libqt5ct-style.so,platformthemes/libqt5ct.so

if [ -n "$1" ]; then
    mv *.AppImage qView-nightly-$1-x86_64.AppImage
else
    mv *.AppImage qView-$VERSION-x86_64.AppImage
fi
rm -rf appdir
