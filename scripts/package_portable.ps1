#requires -Version 7.2
param([string]$BuildDir = 'build', [string]$OutputDir = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$version = (Get-Content (Join-Path $repo 'version.txt') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid version' }
& "$PSScriptRoot/check_release_payload.ps1" -BuildDir $build
. "$PSScriptRoot/release_payload.ps1"
$files = $PulseReleasePayload
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $file))) { throw "Missing portable dependency: $file" }
}
$dist = if ($OutputDir) { [IO.Path]::GetFullPath($OutputDir) } else { Join-Path $repo 'dist' }
$stage = Join-Path $dist ("portable-stage-" + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $stage "Pulse-$version-portable"
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $build $file) -Destination $payload }
foreach ($license in @('PDFium', 'LumaText')) {
    $source = Join-Path $build "licenses/$license"
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing licenses: $source" }
    New-Item -ItemType Directory -Path (Join-Path $payload "licenses/$license") -Force | Out-Null
    Copy-Item -Path "$source/*" -Destination (Join-Path $payload "licenses/$license") -Recurse
}
New-Item -ItemType Directory -Path (Join-Path $payload 'licenses/ib-pinyin') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'third_party/ib-pinyin-cpp/LICENSE.txt') -Destination (Join-Path $payload 'licenses/ib-pinyin')
New-Item -ItemType Directory -Path (Join-Path $payload 'licenses/md4c') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'third_party/md4c/LICENSE.md') -Destination (Join-Path $payload 'licenses/md4c')
# The portable copy keeps its data next to the program by default; the ini is
# the documented switch (docs/data-root.md) for moving it elsewhere.
@"
# Pulse data location for this copy (see docs/data-root.md).
#   data_dir=.                 index, settings, packs and logs stay in this folder
#   data_dir=D:\PulseData      any absolute path
#   data_dir=..\PulseData      relative to this folder
# Delete this file to fall back to %LOCALAPPDATA%\Pulse.
data_dir=.
"@ | Set-Content -LiteralPath (Join-Path $payload 'pulse.ini') -Encoding utf8
@"
Pulse $version — Windows 10 / 11 x64 免安装版

解压整个文件夹后运行 pulse.exe。请保留同目录的辅助程序、DLL 和 licenses 文件夹。
索引、配置、预览增强包和日志默认保存在本目录（由 pulse.ini 的 data_dir 决定），整个文件夹可整体拷贝迁移；修改 data_dir 可改为其他目录。
解压不会注册 PulseIndex 系统服务。需要全盘后台索引服务时，请使用安装版。
程序内下载安装更新会启动安装包；如需继续免安装使用，请下载新版 portable ZIP 并解压到新目录。

Extract the complete folder and run pulse.exe. Keep all helper programs, DLLs and licenses together.
Index, settings, preview packs and logs stay in this folder by default (pulse.ini, data_dir); edit data_dir to move them elsewhere, or delete the file to use %LOCALAPPDATA%\Pulse.
Extraction does not register the PulseIndex service. Use the installer for the system indexing service.
In-app updates launch an installer; download a new portable ZIP to continue using the unpacked edition.
"@ | Set-Content -LiteralPath (Join-Path $payload 'README.txt') -Encoding utf8
$archive = Join-Path $dist "Pulse-$version-portable-win-x64.zip"
Compress-Archive -LiteralPath $payload -DestinationPath $archive -Force
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($file in $files) {
        $entry = $zip.GetEntry("Pulse-$version-portable/$file")
        if (-not $entry -or $entry.Length -ne (Get-Item -LiteralPath (Join-Path $build $file)).Length) {
            throw "Final portable archive is missing or truncates: $file"
        }
    }
} finally { $zip.Dispose() }
Write-Output $archive
