param([Parameter(Mandatory=$true)][string]$VisualStudioRoot)
$ErrorActionPreference = 'Stop'
# Get-Item expands the wildcard to the CRT directories themselves. Get-ChildItem would
# enumerate their children, missing a normal directory containing DLL files only. The CRT family
# follows the compiler: VS 2022 ships Microsoft.VC143.CRT, VS 18 ships Microsoft.VC145.CRT, so the
# toolset that built the binaries decides which one is copied.
$Candidates = @(Get-Item "$VisualStudioRoot/VC/Redist/MSVC/*/x64/Microsoft.VC1*.CRT" -ErrorAction SilentlyContinue |
    Where-Object { $_.PSIsContainer -and (Test-Path "$($_.FullName)/vcruntime140.dll") } |
    Sort-Object { [version]$_.Parent.Parent.Name } -Descending)
if ($Candidates.Count -eq 0) { throw "MSVC x64 redistributable CRT not found under $VisualStudioRoot" }
$Candidates[0]
