#! /bin/sh

export EMSDK_ROOT="$HOME/emsdk"

rm -rf license
mkdir -p license
cp "$EMSDK_ROOT/upstream/emscripten/LICENSE" license/emscripten_LICENSE
cp "$EMSDK_ROOT/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" license/musl_COPYRIGHT
cp "$EMSDK_ROOT/upstream/emscripten/cache/ports/freetype.zip" license
cp "$EMSDK_ROOT/upstream/emscripten/cache/ports/libpng.zip" license
cp "$EMSDK_ROOT/upstream/emscripten/cache/ports/sdl2.zip" license
cp "$EMSDK_ROOT/upstream/emscripten/cache/ports/sdl2_image.zip" license
cp "$EMSDK_ROOT/upstream/emscripten/cache/ports/sdl2_ttf.zip" license
cp "$EMSDK_ROOT/upstream/emscripten/cache/ports/zlib.zip" license
cp COPYRIGHT.txt license
cp LICENSE_MIT.txt license
cp LICENSE_CCBY.txt license
cp notofonts_COPYRIGHT_NOTICE.txt license
cp notofonts_LICENSE_OFL.txt license
