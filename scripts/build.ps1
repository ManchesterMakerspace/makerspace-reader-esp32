param(
 [string]$Fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,DebugLevel=info',
 [string[]]$BuildProperties = @(),
 [switch]$DescribeOnly
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'flash-profile.ps1')
$readerRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $readerRoot
try {
 $extra = @(); foreach ($p in $BuildProperties) { $extra += @('--build-property', $p) }
 $resolved = & arduino-cli --config-file arduino-cli.yaml compile --fqbn $Fqbn @extra --show-properties=expanded MakerspaceReader
 if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve board properties' }
 $flash = @($resolved | Where-Object { $_ -match '^build.flash_size=' })
 if ($flash.Count -ne 1) { throw 'Missing or ambiguous build.flash_size' }
 $profile = Get-ReaderFlashProfile ($flash[0].Substring('build.flash_size='.Length).Trim())
 if ($DescribeOnly) { $profile; return }
 $output = Join-Path $readerRoot ('build/profile-' + $profile.ProfileMB)
 $source = Join-Path $output 'source/MakerspaceReader'
 New-Item -ItemType Directory -Force $source | Out-Null
 # Synchronize only this generated sketch; remove stale generated source files.
 $expected = @(Get-ChildItem MakerspaceReader -File | ForEach-Object Name)
 Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -notin $expected } | Remove-Item -Force
 Copy-Item MakerspaceReader/* $source -Recurse -Force
 [IO.File]::WriteAllText((Join-Path $source 'partitions.csv'), (Get-ReaderPartitions $profile))
 $revision = (& git describe --always --dirty).Trim()
 if ($LASTEXITCODE -ne 0) { throw 'Cannot determine firmware revision' }
 $metadata = '#pragma once' + "
" + '#define READER_BUILD_ID "' + $revision + '-flash' + $profile.ProfileMB + '"' + "
" + '// Generated ' + [DateTime]::UtcNow.ToString('O') + "
"
 [IO.File]::WriteAllText((Join-Path $source 'BuildMetadata.h'), $metadata)
 $flags = '-DREADER_CONFIGURED_FLASH_BYTES=' + $profile.ConfiguredBytes + 'ULL -DREADER_PROFILE_MB=' + $profile.ProfileMB
 $options = Join-Path $source 'build_opt.h'
 [IO.File]::AppendAllText($options, "
" + $flags + "
")
 $compiled = Join-Path $output 'compiled'
 & arduino-cli --config-file arduino-cli.yaml compile --jobs 4 --fqbn $Fqbn @extra --build-property ('build.flash_size=' + $profile.ProfileMB + 'MB') --build-property ('upload.maximum_size=' + $profile.SlotBytes) --build-path $compiled $source
 if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed' }
 $binary = Get-Item (Join-Path $compiled 'MakerspaceReader.ino.bin')
 if ($binary.Length -gt [math]::Floor($profile.SlotBytes * 0.9)) { throw 'Firmware must leave at least 10% OTA slot headroom' }
 $profile | ConvertTo-Json | Set-Content (Join-Path $output 'profile.json')
} finally { Pop-Location }
