# ImmersiveHUD.dll

## Requirements
* [CMake](https://cmake.org/)
	* Add this to your `PATH`
* [PowerShell](https://github.com/PowerShell/PowerShell/releases/latest)
* [Vcpkg](https://github.com/microsoft/vcpkg)
	* Add the environment variable `VCPKG_ROOT` with the value as the path to the folder containing vcpkg
* [Visual Studio Community 2022 or 2026](https://visualstudio.microsoft.com/)
	* Desktop development with C++
* [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG/tree/ng)
	* Included as a git submodule in `extern/CommonLibSSE-NG` (`ng` branch)

## Register Visual Studio as a Generator
* Open `x64 Native Tools Command Prompt`
* Run `cmake`
* Close the cmd window

## Building
A single DLL is built that supports Skyrim SE (1.5.97) and AE (1.6.x and 1.7.x); the runtime is detected at load time.
```
git clone --recurse-submodules https://github.com/Fuzzlesz/ImmersiveHUD-SKSE.git
cd ImmersiveHUD-SKSE
# or, in an existing clone:
git submodule update --init --recursive
```

### Visual Studio 2022
```
cmake --preset vs2022-windows-vcpkg
cmake --build build --config Release
```
### Visual Studio 2026
```
cmake --preset vs2026-windows-vcpkg
cmake --build build --config Release
```
## License
[MIT](LICENSE)
