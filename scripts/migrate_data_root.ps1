#requires -Version 5.1
<#
.SYNOPSIS
  Moves an existing Pulse data directory to a new location.

.DESCRIPTION
  Copies %LOCALAPPDATA%\Pulse (or -Source) to -Target, verifies the copy by
  file count and total bytes, and renames the old directory to
  Pulse.migrated-<timestamp> so the move stays reversible. Nothing is deleted.

  Run this while Pulse is not running. Afterwards start Pulse with the new
  location in place (pulse.ini data_dir=... or PULSE_DATA_DIR) and confirm the
  window, search and previews work before deleting the backup.

.PARAMETER Target
  Absolute path of the new data root, e.g. D:\PulseData or the portable
  program folder.

.PARAMETER Source
  Defaults to %LOCALAPPDATA%\Pulse.

.PARAMETER Force
  Allows a non-empty target directory (files are then merged, target wins).

.EXAMPLE
  pwsh ./scripts/migrate_data_root.ps1 -Target D:\PulseData
#>
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$Source = (Join-Path $env:LOCALAPPDATA 'Pulse'),
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

$target = [IO.Path]::GetFullPath($Target)
if (-not [IO.Path]::IsPathRooted($target)) { throw "Target must be an absolute path: $Target" }

$running = Get-Process -Name pulse, 'Pulse.Index', 'Pulse.Preview', 'Pulse.Document', pulse_shell, pulse_elevated, pulse_integration -ErrorAction SilentlyContinue
if ($running) {
    Write-Warning ("Pulse is running ({0}). Close it first, then re-run." -f ($running.Name -join ', '))
    exit 1
}
if (-not (Test-Path -LiteralPath $Source)) { throw "Source data directory not found: $Source" }
if ((Test-Path -LiteralPath $target) -and (Get-ChildItem -LiteralPath $target -Recurse -ErrorAction SilentlyContinue)) {
    if (-not $Force) { throw "Target is not empty: $target (pass -Force to merge; target files win)" }
}

New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
New-Item -ItemType Directory -Path $target -Force | Out-Null

$sourceFiles = Get-ChildItem -LiteralPath $Source -Recurse -File -ErrorAction SilentlyContinue
$sourceBytes = ($sourceFiles | Measure-Object -Property Length -Sum).Sum
$robocopy = robocopy $Source $target /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

$targetFiles = Get-ChildItem -LiteralPath $target -Recurse -File -ErrorAction SilentlyContinue
$targetBytes = ($targetFiles | Measure-Object -Property Length -Sum).Sum
if ($targetFiles.Count -lt $sourceFiles.Count -or $targetBytes -lt $sourceBytes) {
    throw ("Copy incomplete: {0}/{1} files, {2}/{3} bytes. Old data left untouched at {4}." -f
        $targetFiles.Count, $sourceFiles.Count, $targetBytes, $sourceBytes, $Source)
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = "$Source.migrated-$stamp"
Rename-Item -LiteralPath $Source -NewName (Split-Path -Leaf $backup)
Write-Output "Moved $($sourceFiles.Count) files ($([math]::Round($sourceBytes/1MB,1)) MB) to $target"
Write-Output "Old data kept at $backup - delete it after the new location works."
