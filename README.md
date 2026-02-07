# VkLayer_vsync

A Vulkan layer for overwriting the vsync behavior of Vulkan apps.


## How to build

```shell
git submodule update --init --recursive

# Linux
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j $(nproc)

# Windows
cmake -B build -S .
cmake --build build --config Release -j 8
```


## Usage on Linux

Vulkan changed how layer configuration works for loaders built against the 1.3.234 Vulkan headers or later.
For more details, please refer to
https://github.com/KhronosGroup/Vulkan-Utility-Libraries/blob/main/docs/layer_configuration.md.
If using a recent Vulkan loader, the environment variables below can be used to enable the layer.

```shell
export VK_ADD_LAYER_PATH=$(pwd)/build
export VK_LOADER_LAYERS_ENABLE=*vsync
```

For older Vulkan loaders, please use the deprecated `VK_INSTANCE_LAYERS` variable.

```shell
export VK_ADD_LAYER_PATH=$(pwd)/build
export VK_INSTANCE_LAYERS=VK_LAYER_CHRISMILE_vsync
```

By default, the layer will disable vsync. The behavior can be overwritten, e.g., with the environment variable
`VK_VSYNC`. A few examples of usage of the environment variable are given below.

```shell
export VK_VSYNC=off           # vsync off; alternative values: 0, immediate
export VK_VSYNC=on            # vsync on; alternative values: 1, fifo
export VK_VSYNC=passthrough   # do not touch app vsync settings
```

If the layer should be installed globally, please add the following arguments to `cmake -B build -S .`.
- Linux (user global): `-DCMAKE_INSTALL_PREFIX="$HOME/.local/share/vulkan/implicit_layer.d"`

Independent of the operating system, `--target install` needs to be added to `cmake --build build` in this case as well.


## Usage on Windows

The usage on Windows is the same as on Linux, but depending on the used terminal (cmd.exe/PowerShell/MSYS2).
Examples are given below.

### MSYS2 (bash)

```shell
export VK_ADD_LAYER_PATH=<...>
export VK_LOADER_LAYERS_ENABLE=*vsync
export VK_VSYNC=off
```

### cmd.exe

```shell
set VK_ADD_LAYER_PATH=<...>
set VK_LOADER_LAYERS_ENABLE=*vsync
set VK_VSYNC=off
```

### PowerShell

```shell
$env:VK_ADD_LAYER_PATH = "<...>"
$env:VK_LOADER_LAYERS_ENABLE = "*vsync"
$env:VK_VSYNC = "off"
```
