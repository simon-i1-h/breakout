# ブロック崩しをC言語とSDL2ライブラリで実装してPCとブラウザで動かす

## ビルド (Ubuntu)

最初に:
```
$sudo apt install cmake
```

### PCターゲット

```
$ sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev build-essential cmake
$ mkdir build && cd build
$ cmake ..
$ cmake --build .
```

### ブラウザターゲット

1. [Install emscripten](https://emscripten.org/docs/getting_started/downloads.html).

2.
```
$ mkdir embuild && cd embuild
$ emcmake cmake ..
$ cmake --build .
```
