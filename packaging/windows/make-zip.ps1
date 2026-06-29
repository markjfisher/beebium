# Package the built Windows servers into a self-contained, distributable ZIP.
#
# Run after building the beebium-servers target (Release) with the
# x64-windows-static-md triplet. It installs the relocatable tree to a staging
# prefix and zips bin/ + share/ with their contents at the archive root, so the
# Scoop manifest's "bin\<server>.exe" paths resolve and the servers find their
# extensions (bin\extensions) and ROMs/presets (..\share\beebium) relative to
# their own location. The import .lib files under lib/ are build-time only and
# are deliberately omitted.
#
# Usage, from a VS Developer PowerShell at the repo root:
#   .\packaging\windows\make-zip.ps1 -BuildDir build-win-static -Version 0.1.0
param(
  [string]$BuildDir = "build-win-static",
  [string]$Version  = "0.1.0",
  [string]$StageDir = "$env:TEMP\beebium-win-stage",
  [string]$OutDir   = "."
)
$ErrorActionPreference = "Stop"

Remove-Item -Recurse -Force $StageDir -ErrorAction SilentlyContinue
cmake --install $BuildDir --config Release --prefix $StageDir

$zip = Join-Path $OutDir "beebium-server-$Version-windows-x64.zip"
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $StageDir "bin"), (Join-Path $StageDir "share") `
                 -DestinationPath $zip -CompressionLevel Optimal

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
Write-Host "Created $zip"
Write-Host "SHA256 $hash"
