param(
 [string]$Fqbn = 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,DebugLevel=info',
 [string[]]$BuildProperties = @(),
 [ValidateSet(0,1)][int]$Reservations = 1,
 [ValidateSet(0,1,2,3)][int]$Board = 0,
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
 function Property([string]$name) {
  $values=@($resolved | Where-Object { $_.StartsWith($name+'=') })
  if($values.Count -ne 1) { throw "Missing or ambiguous $name" }
  return $values[0].Substring($name.Length+1).Trim()
 }
 $variant=Property 'build.variant'
 $selected=$Board
 if(!$selected) {
  $selected=switch($variant) {
   'esp32-poe' {2}
   'adafruit_qtpy_esp32s3_n4r2' {3}
   'esp32s3' {1}
   default {throw "Unsupported board variant: $variant; select an explicit -Board override"}
  }
 }
 $mcu=Property 'build.mcu'
 $definitions=Property 'build.defines'
 if($selected -eq 2) {
  if($mcu -ne 'esp32' -or $definitions -match 'BOARD_HAS_PSRAM') {throw 'Olimex requires ESP32 without PSRAM'}
 } else {
  if($mcu -ne 'esp32s3' -or $definitions -notmatch 'BOARD_HAS_PSRAM') {throw 'S3 profiles require ESP32-S3 with PSRAM'}
  $psram=Property 'build.psram_type'
  if(($selected -eq 3 -and $psram -ne 'qspi') -or ($selected -eq 1 -and $psram -ne 'opi')) {throw 'PSRAM type does not match reader board'}
 }
 if($selected -ne 1 -and $profile.ConfiguredBytes -ne 4MB) {throw 'Only 4 MB new board variants are supported'}
 if($selected -eq 3 -and (Property 'build.partitions') -ne 'default') {throw 'QT Py requires PartitionScheme=default; TinyUF2 additions are unsupported'}
 if($selected -eq 3 -and ((Property 'upload.extra_flags') -match 'uf2|0x2d0000')) {throw 'Unexpected QT Py extra upload image'}
 if ($DescribeOnly) { $profile; return }
 if ($profile.ProfileMB -eq 4) {
  # Cross-file C++ optimization keeps Wi-Fi + reservations within the OTA budget.
  # Leave C sources alone: the core's panic-handler assembly is incompatible with LTO.
  # Use plain -flto to override the SDK link recipe's -fno-lto (-flto=auto does not).
  foreach ($setting in @(
   @('compiler.cpp.extra_flags', '-flto'),
   @('compiler.c.elf.extra_flags', '-flto -Os')
  )) {
   $prefix = $setting[0] + '='
   $existing = @($resolved | Where-Object { $_.StartsWith($prefix) })
   if ($existing.Count -ne 1) { throw "Missing or ambiguous $($setting[0])" }
   $extra += @('--build-property', ($existing[0] + ' ' + $setting[1]))
  }
 }
 $suffix = if ($Reservations -eq 0) {'-no-reservations'} else {''}
 $output = Join-Path $readerRoot ('build/board-' + $selected + '-' + $variant + '/profile-' + $profile.ProfileMB + $suffix)
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
 $flags += ' -DREADER_ENABLE_RESERVATIONS=' + $Reservations + ' -DREADER_BOARD=' + $selected
 $options = Join-Path $source 'build_opt.h'
 [IO.File]::AppendAllText($options, "
" + $flags + "
")
 $compiled = Join-Path $output 'compiled'
 & arduino-cli --config-file arduino-cli.yaml compile --jobs 4 --fqbn $Fqbn @extra --build-property ('build.flash_size=' + $profile.ProfileMB + 'MB') --build-property ('upload.maximum_size=' + $profile.SlotBytes) --build-path $compiled $source
 if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed' }
 # Confirm the compiler consumed our generated table, not the native default.
 $actual=Get-Content -Raw (Join-Path $compiled 'partitions.csv')
 if($actual.Trim() -ne (Get-ReaderPartitions $profile).Trim()) {throw 'Compiled partition layout differs from reader profile'}
 & (Join-Path $PSScriptRoot 'verify-partitions.ps1') -Binary (Join-Path $compiled 'MakerspaceReader.ino.partitions.bin') -Profile $profile
 $binary = Get-Item (Join-Path $compiled 'MakerspaceReader.ino.bin')
 $budget = [long][math]::Floor($profile.SlotBytes * 0.9)
 $headroom = 100 * ($profile.SlotBytes - $binary.Length) / $profile.SlotBytes
 Write-Output ('Application: {0:N0} bytes; OTA slot: {1:N0} bytes; headroom: {2:N2}%; 10% headroom budget: {3:N0} bytes' -f $binary.Length, $profile.SlotBytes, $headroom, $budget)
 if ($binary.Length -gt $budget) {
  throw ('Firmware must leave at least 10% OTA slot headroom: {0:N0} bytes exceeds the {1:N0}-byte budget by {2:N0} bytes (flash={3} MB, reservations={4})' -f $binary.Length, $budget, ($binary.Length - $budget), $profile.ProfileMB, $Reservations)
 }
 if ($Reservations -eq 0 -or $selected -eq 3) {
  $compilerDirectory = ($resolved | Where-Object { $_ -like 'compiler.path=*' } | Select-Object -First 1).Substring('compiler.path='.Length)
  $compilerPrefix = ($resolved | Where-Object { $_ -like 'compiler.prefix=*' } | Select-Object -First 1).Substring('compiler.prefix='.Length)
  $nmCommand = Join-Path $compilerDirectory ($compilerPrefix + 'nm')
  if (!(Test-Path -LiteralPath $nmCommand)) { $nmCommand += '.exe' }
  $symbols = & $nmCommand --demangle --defined-only (Join-Path $compiled 'MakerspaceReader.ino.elf')
  if ($LASTEXITCODE -ne 0) { throw 'Cannot verify firmware symbols' }
  if($selected -eq 3 -and ($symbols -match 'ETHClass::begin\(|SPIClass::begin\(')) {throw 'Wi-Fi-only firmware contains Ethernet/SPI initialization'}
  if ($Reservations -eq 0 -and ($symbols -match '\breservation::|audio::(?:jeopardy|donkeyKongStart|\w*Reservation|\w*Jeopardy|\w*DonkeyKongStart)|audio::Sequencer::(?:suspend|resume)')) {
   throw 'Disabled firmware contains reservation code or melody tables'
  }
 }
 $profile | ConvertTo-Json | Set-Content (Join-Path $output 'profile.json')
} finally { Pop-Location }
