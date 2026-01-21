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
