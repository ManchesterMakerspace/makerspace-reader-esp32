function Get-ReaderFlashProfile([string]$Capacity) {
 if ($Capacity -notmatch '^([0-9]+)\s*(MB|MiB|M)$') { throw 'Flash capacity must be an integer in MB' }
 $configured = [long]$Matches[1]
 if ($configured -lt 4 -or $configured -gt 1024) { throw 'Unsupported flash capacity' }
 $size = if ($configured -ge 16) {16} elseif ($configured -ge 8) {8} else {4}
 $slot = if ($size -eq 16) {0x400000} elseif ($size -eq 8) {0x200000} else {0x180000}
 $start = 0x10000 + 2 * $slot
 $keys = if ($size -eq 16) {0xc10000} else {$size * 1MB - 0x1000}
 $cache = if ($size -eq 16) {0x400000} else {$keys - $start}
 [pscustomobject]@{ ConfiguredBytes=$configured * 1MB; ProfileMB=$size; SlotBytes=$slot; CacheStart=$start; CacheBytes=$cache; KeysOffset=$keys }
}
function Get-ReaderPartitions($Profile) {
 @(
 '# Name, Type, SubType, Offset, Size, Flags'
 'nvs,data,nvs,0x9000,0x5000,'
 'otadata,data,ota,0xe000,0x2000,'
 ('app0,app,ota_0,0x10000,0x{0:x},' -f $Profile.SlotBytes)
 ('app1,app,ota_1,0x{0:x},0x{1:x},' -f (0x10000+$Profile.SlotBytes),$Profile.SlotBytes)
 ('spiffs,data,spiffs,0x{0:x},0x{1:x},' -f $Profile.CacheStart,$Profile.CacheBytes)
 ('nvs_keys,data,nvs_keys,0x{0:x},0x1000,encrypted' -f $Profile.KeysOffset)
 ) -join "
"
}
