# Pinned reader dependencies

ElegantOTA 4.0.0: https://github.com/ayushsharma82/ElegantOTA
ELECHOUSE PN7160 PN7161 NFC 3.1.3: https://github.com/wilson-elechouse/ELECHOUSE_PN7150_PN7160

Upstream licenses are retained in each library directory. These sources are vendored because the patches affect authentication:
- ElegantOTA defaults to async mode. Every start, chunk and completion calls the reader's authentication/CSRF/session gate. Completion requires a successfully validated final image; an empty POST cannot claim success or trigger reboot.
- PN7160 I2C reads reject short headers and short payloads. The application additionally uses bounded fresh NCI exchanges, exact response lengths and status checks.

Do not replace these copies with Library Manager updates without reviewing and reapplying the patches.
The remaining libraries are pinned by scripts/setup.ps1, including SHA-256 checks for the maintained ESP32Async release archives.

MakerspacePN7160.h is the unique sketch include for the bundled NFC driver. It prevents Arduino library resolution from selecting ElectronicCats v3.1.1, which shares the original header name but lacks the ELECHOUSE configuration methods and local patches. scripts/package-nfc-library.ps1 packages this exact directory for Arduino IDE ZIP installation.
