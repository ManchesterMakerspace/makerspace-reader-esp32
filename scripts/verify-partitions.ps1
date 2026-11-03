param([Parameter(Mandatory)][string]$Binary,[Parameter(Mandatory)]$Profile)
$bytes=[IO.File]::ReadAllBytes($Binary)
$actual=@{}
for($i=0;$i+32 -le $bytes.Length;$i+=32) {
 if([BitConverter]::ToUInt16($bytes,$i) -ne 0x50aa) {break}
 $label=[Text.Encoding]::ASCII.GetString($bytes,$i+12,16).TrimEnd([char]0)
 if($actual.ContainsKey($label)) {throw "Duplicate partition $label"}
 $actual[$label]=@([int]$bytes[$i+2],[int]$bytes[$i+3],[BitConverter]::ToUInt32($bytes,$i+4),[BitConverter]::ToUInt32($bytes,$i+8),[BitConverter]::ToUInt32($bytes,$i+28))
}
$expected=@{
 nvs=@(1,2,0x9000,0x5000,0)
 otadata=@(1,0,0xe000,0x2000,0)
 app0=@(0,16,0x10000,$Profile.SlotBytes,0)
 app1=@(0,17,(0x10000+$Profile.SlotBytes),$Profile.SlotBytes,0)
 spiffs=@(1,130,$Profile.CacheStart,$Profile.CacheBytes,0)
 nvs_keys=@(1,4,$Profile.KeysOffset,0x1000,1)
}
if($actual.Count -ne $expected.Count) {throw 'Unexpected partition count (including possible TinyUF2 additions)'}
foreach($name in $expected.Keys) {
 if(!$actual.ContainsKey($name)) {throw "Missing partition $name"}
 for($i=0;$i -lt 5;$i++) {if($actual[$name][$i] -ne $expected[$name][$i]) {throw "Incorrect $name partition field $i"}}
}
