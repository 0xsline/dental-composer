# 在 Windows 上构建 Win7 兼容版本（Qt 5.15.2 + MSVC2019 x64）
# 需求：Windows 10/11 + Visual Studio 2019 或 2022（含 MSVC v142 工具集）+ Python 3
# 产物：build-win\Release\dental-composer.exe（windeployqt 后整目录可分发）

$ErrorActionPreference = "Stop"
$QtVersion = "5.15.2"
$Arch = "win64_msvc2019_64"
$QtRoot = Join-Path $env:USERPROFILE "Qt\$QtVersion\$Arch"

if (-not (Test-Path $QtRoot)) {
    Write-Host "==> 下载 Qt $QtVersion ($Arch)"
    pip install -q aqtinstall
    python -m aqt install-qt windows desktop $QtVersion $Arch --outputdir (Join-Path $env:USERPROFILE "Qt")
}

$env:Path = "$QtRoot\bin;$env:Path"
$buildDir = "build-win"

# 用 VS2022 + v142 工具集（与 Qt 官方 msvc2019 预编译包一致的 ABI，可跑 Win7）
cmake -S . -B $buildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_GENERATOR_TOOLSET=v142 `
    "-DCMAKE_PREFIX_PATH=$QtRoot"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "==> windeployqt 收集依赖"
& "$QtRoot\bin\windeployqt.exe" --release --no-translations "$buildDir\Release\dental-composer.exe"

Write-Host "==> 完成：$buildDir\Release\"
Write-Host "    打包安装器：makensis /DAPP_DIR=..\build-win\Release scripts\installer.nsi"
Write-Host "    或直接把 Release 目录拷到 Win7 64 位机器运行（自带 VC 运行库 DLL）。"
