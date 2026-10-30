$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../scripts/flash-profile.ps1')
foreach ($case in @(@('4MB',4),@('8MB',8),@('12MB',8),@('16MB',16),@('32MB',16))) {
 $p=Get-ReaderFlashProfile $case[0]
 if ($p.ProfileMB -ne $case[1]) { throw "Wrong profile for $($case[0])" }
 if ($p.KeysOffset + 4096 -gt $p.ProfileMB * 1MB) { throw 'Partition overflow' }
 if ($p.CacheStart -ne 65536 + 2*$p.SlotBytes) { throw 'Slot overlap' }
 if ($p.CacheStart + $p.CacheBytes -ne $p.KeysOffset) { throw 'Cache/key partition gap or overlap' }
}
foreach ($bad in @('','auto','3MB','2MB','4GB','4.5MB','-4MB','9999999999999999999999MB')) {
 $failed=$false
 try { Get-ReaderFlashProfile $bad | Out-Null } catch { $failed=$true }
 if (!$failed) { throw "Accepted invalid capacity: $bad" }
}
$expected=Get-Content (Join-Path $PSScriptRoot '../MakerspaceReader/partitions.csv')
$actual=(Get-ReaderPartitions (Get-ReaderFlashProfile '16MB')) -split "
"
for ($i=1; $i -lt $expected.Count; $i++) {
 if ($expected[$i].Trim() -ne $actual[$i].Trim()) { throw '16MB layout changed' }
}
Write-Output 'Flash profile selection, bounds and existing 16MB layout passed'
