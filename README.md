# dartt-dashboard

## Build Dependencies

### Debian/Ubuntu
```bash
sudo apt install libgl1-mesa-dev libsdl2-dev
```

### Fedora
```bash
sudo dnf install mesa-libGL-devel SDL2-devel
```

### Arch Linux
```bash
sudo pacman -S mesa sdl2
```

## Building

```bash
git submodule update --init --recursive
mkdir build && cd build
cmake ..
make
```

## IMPORTANT NOTE FOR WINDOWS:

You MUST copy the SDL2.dll to the same directory as the compiled executable. It will otherwise fail silently (via cmd) or with an error (if launched via visual studio).

Make sure to use the same target architecture - i.e. x86 or x64. The .dlls can be found in the SDL directory