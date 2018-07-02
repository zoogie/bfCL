# bfCL
This is an experimental port of [TWLbf](https://github.com/Jimmy-Z/TWLbf/) to OpenCL.

## Compile
### Windows
Note: If you really want to use Visual Studio 2017, you're going to probably have to change the Makefile a bit and compile [mbedtls](https://github.com/ARMmbed/mbedtls/) from source.
#### Requirements for compiling with MSYS2
* **A 64-bit computer**
* [MSYS2](http://www.msys2.org/) (the x86_64 executable; **read its instructions on installing and setting up**)
* An `OpenCL.dll` or `OpenCL.lib` 64-bit library

Note: `OpenCL.dll` can be found inside of your `C:\Windows\System32\` folder, but you may have to install your graphics card's drivers from your graphics card's vendor if it's not there.
You can alternatively install [Intel's OpenCL SDK](https://software.intel.com/intel-opencl/), but this requires you to agree to their TOS and takes up more space on your computer.
#### Instructions for compiling with MSYS2
1. Close any open instances of MSYS2 (if applicable), then launch the `MSYS2 MinGW 64-bit` shortcut from the Windows Start Menu.
1. In the MSYS2 bash shell that appears, execute `pacman -Syu --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-mbedtls git` to download and install required packages.
1. If you're going to use the `OpenCL.dll` 64-bit library from your `C:\Windows\System32\` folder (in contrast to installing Intel's OpenCL SDK), copy it into your `msys64/mingw64/lib/` folder (your `msys64` folder is by default installed onto the root of your "C:" drive during the installation of MSYS2). Additionally, if you're going to use `OpenCL.dll`, in MSYS2, execute `git clone https://github.com/KhronosGroup/OpenCL-Headers.git && mv OpenCL-Headers/CL /mingw64/include/` to download and move the required OpenCL C headers folder.
1. In MSYS2, execute `git clone https://github.com/zoogie/bfCL.git && cd bfCL` to download bfCL and change your current directory to it.
1. In MSYS2, execute `mingw32-make` to compile bfCL (**OpenCL and mbedcrypto will both be dynamically linked!** Refer to the Makefile if you want to statically link mbedcrypto instead).
### Linux
#### Requirements for compiling on all Linux distros
* **A 64-bit computer**
#### Instructions for compiling on Debian-based Linux distros
Note: the **concept** is still applicable for all other Linux distros; e.g., some packages may have different names.
1. Open up the "Terminal" application.
1. In "Terminal", execute `sudo apt-get update && sudo apt-get install gcc git libmbedtls-dev make ocl-icd-opencl-dev` to download and install required packages (note that the "ocl-icd-opencl-dev" package includes both the OpenCL C headers and the OpenCL ICD Loader library).
1. After all of the packages have finished installing, in "Terminal", execute `git clone https://github.com/zoogie/bfCL.git && cd bfCL` to download bfCL and change your current directory to it.
1. In "Terminal", execute `make` to compile bfCL (**OpenCL and mbedcrypto will both be dynamically linked!** Refer to the Makefile if you want to statically link mbedcrypto instead).
### macOS
#### Requirements for compiling on macOS
* **An Intel-based 64-bit computer**
* [Homebrew](https://brew.sh/) (**Read its instructions on installing**; installing Homebrew also installs Xcode command-line tools, which is also needed)
#### Instructions for compiling on macOS
1. Open up the "Terminal" application through Launchpad.
1. In "Terminal", execute `brew update && brew install git mbedtls` to download and install required packages.
1. In "Terminal", execute `git clone https://github.com/zoogie/bfCL.git && cd bfCL` to download bfCL and change your current directory to it.
1. In "Terminal", execute `make` to compile bfCL (**OpenCL and mbedcrypto will both be dynamically linked!** Refer to the Makefile if you want to statically link mbedcrypto instead).

## License
AES and SHA1 code from [mbed TLS](https://github.com/ARMmbed/mbedtls/) which is Apache 2.0 license,
so I guess this project becomes Apache 2.0 licensed automatically?
or only related files are Apache 2.0? I'm not sure.
