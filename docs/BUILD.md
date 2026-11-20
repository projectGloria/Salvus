# Building Salvus

## Prerequisites

- **OS:** Windows 10 or Windows 11 (x64)
- **C++ Toolchain:** Visual Studio 2022 (or Build Tools 2022) with **Desktop development with C++** (MSVC v143, Windows 10/11 SDK, CMake 3.24+)
- **.NET SDK:** .NET 10 SDK (`10.0.x`)

## 1. Build the C++20 Recovery Engine (`Salvus.Engine`)

From a Developer PowerShell for VS 2022 (or with `cmake` in `PATH`):

```powershell
cmake -S Salvus.Engine -B Salvus.Engine/build -G "Visual Studio 17 2022" -A x64
cmake --build Salvus.Engine/build --config Release
```

The engine executable is produced at:
- `Salvus.Engine/build/Release/Salvus.Engine.exe` (or `Debug/Salvus.Engine.exe`)

## 2. Build the .NET 10 Desktop Solution (`Salvus.sln`)

```powershell
dotnet restore Salvus.sln
dotnet build Salvus.sln -c Release --no-restore
```

`Salvus.Desktop.csproj` automatically copies `Salvus.Engine.exe` into its output directory if a built engine binary is present in `Salvus.Engine/build/<Configuration>/`.

## 3. Verify Official Branding Assets

Both `Salvus.ico` and `Salvus.png` must exist directly in the repository root. Both `Salvus.Desktop.csproj` and `Salvus.Engine/CMakeLists.txt` fail the build explicitly if either asset is missing.
