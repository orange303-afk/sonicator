$ErrorActionPreference = 'Stop'
$Root = Join-Path ([IO.Path]::GetTempPath()) ('sonicator-crt-test-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path "$Root/VC/Redist/MSVC/14.40.1/x64/Microsoft.VC143.CRT" -Force | Out-Null
try {
    [IO.File]::WriteAllText("$Root/VC/Redist/MSVC/14.40.1/x64/Microsoft.VC143.CRT/vcruntime140.dll", 'fixture')
    $Result = & "$PSScriptRoot/../windows/find-crt.ps1" -VisualStudioRoot $Root
    if ($Result.FullName -ne (Get-Item "$Root/VC/Redist/MSVC/14.40.1/x64/Microsoft.VC143.CRT").FullName) {
        throw 'Did not return the actual CRT directory containing only DLL files'
    }
    $Failed = $false
    try { & "$PSScriptRoot/../windows/find-crt.ps1" -VisualStudioRoot "$Root/missing" | Out-Null }
    catch { $Failed = $true }
    if (!$Failed) { throw 'Missing CRT must fail' }
    Write-Host 'PASS: CRT directory discovery and missing-input rejection'
} finally {
    Remove-Item $Root -Recurse -Force
}
