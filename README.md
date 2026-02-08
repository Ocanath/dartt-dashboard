# dartt-dashboard

## Building

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

## Usage
When launching the software, you will see a blank screen with a Live Expressions and Plot Settings view.

![alt text](img/image-1.png)


To begin, you can either drag and drop an .elf or a .json file. The .elf must be compiled with DWARF debugging content (on STM32, this can be done with the Debug/ build configuration, with optimize for Debug enabled or similar). 

If using a .json, it must follow the format of the .json file provided. Alternatively, a .json may be generated using the [dartt-describe](external/dartt-protocol/tools/dartt-describe.py) script, or by loading from .elf and hitting the 'Save' icon in the Live Expressions view.