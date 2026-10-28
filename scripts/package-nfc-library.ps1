$ErrorActionPreference = 'Stop'
$readerRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$outputDir = Join-Path $readerRoot '.arduino/downloads'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$archive = Join-Path $outputDir 'MakerspacePN7160.zip'
Compress-Archive -LiteralPath (Join-Path $readerRoot 'vendor/libraries/ELECHOUSE_PN7160_PN7161_NFC') -DestinationPath $archive -Force
Write-Output "Arduino IDE: Sketch > Include Library > Add .ZIP Library > $archive"
