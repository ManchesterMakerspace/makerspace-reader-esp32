$ErrorActionPreference = 'Stop'
$readerRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $readerRoot
try {
 & arduino-cli --config-file arduino-cli.yaml compile --jobs 4 --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom' --build-property upload.maximum_size=4194304 --build-path build MakerspaceReader
 if($LASTEXITCODE -ne 0){throw 'Firmware compilation failed'}
} finally { Pop-Location }
