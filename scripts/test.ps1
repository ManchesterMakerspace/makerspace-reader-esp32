$ErrorActionPreference = 'Stop'
$readerRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $readerRoot
try {
 New-Item -ItemType Directory -Force .test-build | Out-Null
 & g++ -std=c++11 -Wall -Wextra -Werror tests/display.cpp MakerspaceReader/Display.cpp -o .test-build/display-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display test compilation failed'}
 & ./.test-build/display-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display tests failed'}
 Copy-Item MakerspaceReader/DisplaySerLcd.cpp .test-build/DisplaySerLcd.cpp -Force
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/display-fakes -I MakerspaceReader tests/display-backend.cpp .test-build/DisplaySerLcd.cpp MakerspaceReader/Display.cpp -o .test-build/display-backend-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display backend test compilation failed'}
 & ./.test-build/display-backend-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Display backend tests failed'}
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
 & g++ -std=c++11 -Wall -Wextra -Werror -I tests/fakes tests/nfc.cpp MakerspaceReader/Pn532.cpp -o .test-build/nfc-tests.exe
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
