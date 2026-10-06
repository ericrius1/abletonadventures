# Adventure Pack installer for Windows.
#
# Double-click "Install Adventure Pack.bat" in the unzipped folder, or run this in PowerShell.
# Straight from the internet (downloads the latest release; run PowerShell as Administrator):
#   irm https://github.com/ericrius1/abletonadventures/releases/latest/download/install-windows.ps1 | iex

$ErrorActionPreference = 'Stop'
$repo = 'ericrius1/abletonadventures'

# Copying into Program Files needs admin rights: relaunch elevated if necessary.
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

$src = $null
if ($PSScriptRoot -and (Test-Path (Join-Path $PSScriptRoot 'VST3'))) { $src = $PSScriptRoot }

if (-not $isAdmin) {
    if ($src) {
        Write-Host 'Asking for administrator rights to install into Program Files...'
        Start-Process powershell -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
        exit
    }
    Write-Warning 'Please run PowerShell as Administrator (right-click > Run as administrator) and try again.'
    return
}

if (-not $src) {
    Write-Host 'Downloading the latest Adventure Pack release...'
    $tmp = Join-Path $env:TEMP ('AdventurePack-' + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $tmp | Out-Null
    $zip = Join-Path $tmp 'pack.zip'
    Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/$repo/releases/latest/download/AdventurePack-Windows.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    $src = Join-Path $tmp 'AdventurePack-Windows'
}

$dest = Join-Path $env:CommonProgramFiles 'VST3\Adventure Audio'
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host "Installing VST3 plugins into $dest" -ForegroundColor Magenta
Get-ChildItem -Path (Join-Path $src 'VST3') -Filter '*.vst3' | ForEach-Object {
    $target = Join-Path $dest $_.Name
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    Copy-Item -Recurse -Force $_.FullName $target
    Get-ChildItem -Recurse -Path $target | Unblock-File -ErrorAction SilentlyContinue
    Write-Host "  + $($_.Name)"
}

Write-Host ''
Write-Host 'All done!' -ForegroundColor Green
Write-Host 'In Ableton Live: Options > Preferences > Plug-Ins > turn on "Use VST3 Plug-in System Folders", then Rescan.'
Write-Host 'Everything shows up under Plug-Ins > "Adventure Audio".'
if ($PSScriptRoot) { Read-Host 'Press Enter to close' | Out-Null }
