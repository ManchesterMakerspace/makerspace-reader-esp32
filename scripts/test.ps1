$ErrorActionPreference = 'Stop'
$readerRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $readerRoot
try {
 & (Join-Path $readerRoot 'tests/flash-profile.ps1')
 New-Item -ItemType Directory -Force .test-build | Out-Null
 & g++ -std=c++11 -Wall -Wextra -Werror tests/reservations.cpp MakerspaceReader/Reservation.cpp -o .test-build/reservation-tests.exe
 if ($LASTEXITCODE -ne 0) { throw 'Reservation test compilation failed' }
 & ./.test-build/reservation-tests.exe
 if ($LASTEXITCODE -ne 0) { throw 'Reservation tests failed' }
 & g++ -std=c++11 -Wall -Wextra -Werror -DREADER_ENABLE_RESERVATIONS=0 tests/reservations-disabled.cpp MakerspaceReader/Reservation.cpp MakerspaceReader/Audio.cpp MakerspaceReader/Display.cpp -o .test-build/reservations-disabled.exe
 if ($LASTEXITCODE -ne 0) { throw 'Reservation compile-out failed' }
 & ./.test-build/reservations-disabled.exe
 if ($LASTEXITCODE -ne 0) { throw 'Reservation compile-out regression failed' }
 & g++ -std=c++11 -Wall -Wextra -Werror tests/connectivity.cpp -o .test-build/connectivity-tests.exe
 if ($LASTEXITCODE -ne 0) { throw 'Connectivity test compilation failed' }
 & ./.test-build/connectivity-tests.exe
 if ($LASTEXITCODE -ne 0) { throw 'Connectivity tests failed' }
 Copy-Item MakerspaceReader/Connectivity.cpp .test-build/Connectivity.cpp -Force
 foreach ($wifiOption in @('-DTEST_WIFI', '-DTEST_ETHERNET_ONLY')) {
  & g++ -std=c++11 -Wall -Wextra -Werror -DREADER_PROFILE_MB=4 $wifiOption -I tests/connectivity-fakes -I MakerspaceReader tests/connectivity-service.cpp -o .test-build/connectivity-service.exe
  if ($LASTEXITCODE -ne 0) { throw 'Connectivity service compilation failed' }
  & ./.test-build/connectivity-service.exe
  if ($LASTEXITCODE -ne 0) { throw 'Connectivity service tests failed' }
 }
 foreach ($cacheSource in @('CachePolicy','CacheRefresh','CacheSnapshot')) {
  Copy-Item "MakerspaceReader/$cacheSource.cpp" ".test-build/$cacheSource.cpp" -Force
 }
 foreach ($profileMB in @(4,8,16)) {
  & g++ -std=c++11 -Wall -Wextra -Werror "-DREADER_PROFILE_MB=$profileMB" -I tests/cache-fakes -I MakerspaceReader -I .arduino/libraries/ArduinoJson/src tests/cache-policy.cpp .test-build/CachePolicy.cpp .test-build/CacheRefresh.cpp .test-build/CacheSnapshot.cpp -o .test-build/cache-policy.exe
  if ($LASTEXITCODE -ne 0) { throw 'Cache policy compilation failed' }
  & ./.test-build/cache-policy.exe
  if ($LASTEXITCODE -ne 0) { throw 'Cache policy tests failed' }
  & g++ -std=c++11 "-DREADER_PROFILE_MB=$profileMB" -I .arduino/libraries/ArduinoJson/src tests/cache-size.cpp -o .test-build/cache-profile-size.exe
  if ($LASTEXITCODE -ne 0) { throw 'Profile size compilation failed' }
  & ./.test-build/cache-profile-size.exe
  if ($LASTEXITCODE -ne 0) { throw 'Profile size tests failed' }
 }
 & g++ -std=c++11 -Wall -Wextra -Werror tests/audio.cpp MakerspaceReader/Audio.cpp -o .test-build/audio-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Audio test compilation failed'}
 & ./.test-build/audio-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Audio tests failed'}
 Copy-Item MakerspaceReader/AudioQwiic.cpp .test-build/AudioQwiic.cpp -Force
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/audio-fakes -I MakerspaceReader tests/audio-backend.cpp .test-build/AudioQwiic.cpp -o .test-build/audio-backend-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Audio backend test compilation failed'}
 & ./.test-build/audio-backend-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Audio backend tests failed'}
 Copy-Item MakerspaceReader/AudioService.cpp .test-build/AudioService.cpp -Force
 Copy-Item MakerspaceReader/ReservationService.cpp .test-build/ReservationService.cpp -Force
 Copy-Item MakerspaceReader/ReservationService.h .test-build/ReservationService.h -Force
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/reservation-fakes -I MakerspaceReader -I .arduino/libraries/ArduinoJson/src tests/reservation-service.cpp MakerspaceReader/Reservation.cpp -o .test-build/reservation-service.exe
 if($LASTEXITCODE -ne 0){throw 'Reservation service compilation failed'}
 & ./.test-build/reservation-service.exe
 if($LASTEXITCODE -ne 0){throw 'Reservation service tests failed'}
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/audio-fakes -I MakerspaceReader tests/audio-service.cpp .test-build/AudioService.cpp MakerspaceReader/Audio.cpp -o .test-build/audio-service-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Audio service test compilation failed'}
 & ./.test-build/audio-service-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Audio service tests failed'}
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/audio-fakes -I MakerspaceReader tests/reservation-audio-service.cpp .test-build/AudioService.cpp MakerspaceReader/Audio.cpp -o .test-build/reservation-audio-service.exe
 if($LASTEXITCODE -ne 0){throw 'Reservation audio service compilation failed'}
 & ./.test-build/reservation-audio-service.exe
 if($LASTEXITCODE -ne 0){throw 'Reservation audio service tests failed'}
 & g++ -std=c++11 -Wall -Wextra -Werror tests/display.cpp MakerspaceReader/Display.cpp -o .test-build/display-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display test compilation failed'}
 & ./.test-build/display-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display tests failed'}
 Copy-Item MakerspaceReader/DisplaySerLcd.cpp .test-build/DisplaySerLcd.cpp -Force
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/display-fakes -I MakerspaceReader tests/display-backend.cpp .test-build/DisplaySerLcd.cpp MakerspaceReader/Display.cpp -o .test-build/display-backend-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display backend test compilation failed'}
 & ./.test-build/display-backend-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display backend tests failed'}
 & g++ -std=c++11 -Wall -Wextra -Werror tests/tool-session.cpp -o .test-build/tool-session-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Tool session test compilation failed'}
 & ./.test-build/tool-session-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Tool session tests failed'}
 Copy-Item MakerspaceReader/EventSpool.cpp .test-build/EventSpool.cpp -Force
 Copy-Item MakerspaceReader/ToolSessionService.cpp .test-build/ToolSessionService.cpp -Force
 foreach ($reservations in @(0,1)) {
  & g++ -std=c++11 -Wall -Wextra -Werror "-DREADER_ENABLE_RESERVATIONS=$reservations" -I tests/session-fakes -I MakerspaceReader -I .arduino/libraries/ArduinoJson/src tests/session-service.cpp MakerspaceReader/Display.cpp -o .test-build/session-service-tests.exe
  if($LASTEXITCODE -ne 0){throw 'Session service test compilation failed'}
  & ./.test-build/session-service-tests.exe
  if($LASTEXITCODE -ne 0){throw 'Session service tests failed'}
 }
 # Compile the production relay implementation with hardware/state test doubles.
 Copy-Item MakerspaceReader/Relay.cpp .test-build/Relay.cpp -Force
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/relay-fakes tests/relay.cpp .test-build/Relay.cpp -o .test-build/relay-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Relay test compilation failed'}
 & ./.test-build/relay-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Relay tests failed'}
 & g++ -std=c++11 -Wall -Wextra -Werror tests/policy.cpp -o .test-build/policy-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Policy test compilation failed'}
 & ./.test-build/policy-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Policy tests failed'}
 Copy-Item tests/fakes/wire.h .test-build/Wire.h -Force
 & g++ -std=c++11 -Wall -Wextra -Werror -I .test-build -I tests/fakes tests/nfc.cpp MakerspaceReader/Pn532.cpp -o .test-build/nfc-tests.exe
 if($LASTEXITCODE -ne 0){throw 'NFC test compilation failed'}
 & ./.test-build/nfc-tests.exe
 if($LASTEXITCODE -ne 0){throw 'NFC tests failed'}
 & g++ -std=c++11 -Wall -Wextra -Werror tests/cache-key.cpp -o .test-build/cache-key-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Cache-key test compilation failed'}
 & ./.test-build/cache-key-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Cache-key tests failed'}
 & g++ -std=c++11 -I .arduino/libraries/ArduinoJson/src tests/cache-size.cpp -o .test-build/cache-size-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Cache test compilation failed'}
 & ./.test-build/cache-size-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Cache size tests failed'}
} finally { Pop-Location }
