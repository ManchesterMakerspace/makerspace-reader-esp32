$ErrorActionPreference='Stop'
$cases=@(
 @{ok=$true; flags=@('-DARDUINO=1','-DARDUINO_ESP32S3_DEV','-DCONFIG_IDF_TARGET_ESP32S3','-DBOARD_HAS_PSRAM')},
 @{ok=$true; flags=@('-DARDUINO=1','-DARDUINO_ESP32_POE','-DCONFIG_IDF_TARGET_ESP32')},
 @{ok=$true; flags=@('-DARDUINO=1','-DARDUINO_ADAFRUIT_QTPY_ESP32S3_N4R2','-DCONFIG_IDF_TARGET_ESP32S3','-DBOARD_HAS_PSRAM')},
 @{ok=$false; flags=@('-DARDUINO_ADAFRUIT_QTPY_ESP32S3_N4R2','-DREADER_BOARD=1')},
 @{ok=$false; flags=@('-DREADER_BOARD=9')},
 @{ok=$false; flags=@('-DREADER_BOARD=2','-DREADER_PROFILE_MB=8')},
 @{ok=$false; flags=@('-DARDUINO=1')},
 @{ok=$false; flags=@('-DARDUINO=1','-DREADER_BOARD=2','-DCONFIG_IDF_TARGET_ESP32','-DBOARD_HAS_PSRAM')},
 @{ok=$false; flags=@('-DARDUINO=1','-DREADER_BOARD=3','-DCONFIG_IDF_TARGET_ESP32','-DBOARD_HAS_PSRAM')},
 @{ok=$false; flags=@('-DARDUINO=1','-DREADER_BOARD=3','-DCONFIG_IDF_TARGET_ESP32S3')}
)
foreach($case in $cases) {
 & g++ -std=c++11 -I tests/board-fakes @($case.flags) tests/board-config.cpp -o .test-build/board-selection.exe *> .test-build/board-selection.log
 if(($LASTEXITCODE -eq 0) -ne $case.ok) {Get-Content .test-build/board-selection.log; throw 'Unexpected board selection result'}
}
# A Wi-Fi-only adapter must fail compilation when credentials are absent.
& g++ -std=c++11 -DREADER_BOARD=3 -DTEST_ETHERNET_ONLY -I tests/connectivity-fakes -I MakerspaceReader tests/connectivity-service.cpp -o .test-build/board-selection.exe *> .test-build/board-selection.log
if($LASTEXITCODE -eq 0 -or !(Select-String -Quiet 'Wi-Fi-only reader requires' .test-build/board-selection.log)) {throw 'QT Py credential validation missing'}
Write-Output 'Native board selection, incompatible targets/memory and missing Wi-Fi credentials passed'
