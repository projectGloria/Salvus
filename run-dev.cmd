@echo off
setlocal
if not exist "%ProgramFiles%\dotnet\dotnet.exe" (
    if exist "%USERPROFILE%\.dotnet\dotnet.exe" (
        set "DOTNET_ROOT=%USERPROFILE%\.dotnet"
        set "PATH=%USERPROFILE%\.dotnet;%PATH%"
    )
)
set "EXE=%~dp0Salvus.Desktop\bin\Debug\net10.0-windows\win-x64\Salvus.Desktop.exe"
if not exist "%EXE%" (
    set "EXE=%~dp0Salvus.Desktop\bin\x64\Release\net10.0-windows\win-x64\Salvus.Desktop.exe"
)
if not exist "%EXE%" (
    echo Building Salvus.Desktop in Debug mode...
    dotnet build "%~dp0Salvus.Desktop\Salvus.Desktop.csproj" -c Debug
    set "EXE=%~dp0Salvus.Desktop\bin\Debug\net10.0-windows\win-x64\Salvus.Desktop.exe"
)
start "" "%EXE%"
endlocal
