param([string]$ArduinoData = (Join-Path $env:LOCALAPPDATA 'Arduino15'))
$ErrorActionPreference = 'Stop'
$readerRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sketchbook = Join-Path $readerRoot '.arduino'
New-Item -ItemType Directory -Force (Join-Path $sketchbook 'libraries') | Out-Null
$config = Join-Path $readerRoot 'arduino-cli.yaml'
[IO.File]::WriteAllText($config, "directories:`n  data: " + $ArduinoData.Replace('\','/') + "`n  user: " + $sketchbook.Replace('\','/') + "`n")
function Arduino([string[]]$Arguments) {
 & arduino-cli --config-file $config @Arguments
 if($LASTEXITCODE -ne 0){throw 'Arduino dependency setup failed'}
}
Arduino @('core','update-index','--additional-urls','https://espressif.github.io/arduino-esp32/package_esp32_index.json')
Arduino @('core','install','esp32:esp32@3.3.12','--additional-urls','https://espressif.github.io/arduino-esp32/package_esp32_index.json')
Arduino @('lib','update-index')
Arduino @('lib','install','ArduinoJson@7.4.3','Adafruit BusIO@1.17.4','RTClib@2.1.4','SparkFun SerLCD Arduino Library@1.0.9','SparkFun Qwiic Buzzer Library@1.1.0','SparkFun Toolkit@1.2.0')
$downloads = Join-Path $sketchbook 'downloads'
New-Item -ItemType Directory -Force $downloads | Out-Null
$packages = @(
 @{Name='web';Url='https://github.com/ESP32Async/ESPAsyncWebServer/archive/refs/tags/v3.12.1.zip';Hash='27AE1A1DF7A0BCB8BF0238DAECA363893357A7CF4732B44FDBC7CE652CFF421E'},
 @{Name='tcp';Url='https://github.com/ESP32Async/AsyncTCP/archive/refs/tags/v3.5.0.zip';Hash='F3D013F58677175E8ADFF86FBFA8AD9352ADD42409930B04C430681CB2F075A9'}
)
foreach($package in $packages){
 $zip = Join-Path $downloads ($package.Name+'.zip')
 if(!(Test-Path -LiteralPath $zip)){Invoke-WebRequest $package.Url -OutFile $zip}
 if((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $package.Hash){throw "Dependency hash mismatch: $($package.Name)"}
 Expand-Archive -LiteralPath $zip -DestinationPath (Join-Path $sketchbook 'libraries') -Force
}
foreach($library in @('ElegantOTA','ELECHOUSE_PN7160_PN7161_NFC')){
 Copy-Item -LiteralPath (Join-Path $readerRoot "vendor/libraries/$library") -Destination (Join-Path $sketchbook 'libraries') -Recurse -Force
}
$secrets = Join-Path $readerRoot 'MakerspaceReader/secrets.h'
if(!(Test-Path -LiteralPath $secrets)){Copy-Item -LiteralPath (Join-Path $readerRoot 'MakerspaceReader/secrets.example.h') -Destination $secrets}
Write-Output "Dependencies ready. Configure secrets.h. Arduino IDE sketchbook: $sketchbook"
