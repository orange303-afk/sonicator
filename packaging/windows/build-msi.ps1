param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [string]$VersionOverride = ''
)
$ErrorActionPreference = 'Stop'
$Repo = (Resolve-Path "$PSScriptRoot/../..").Path
$BuildDir = (Resolve-Path $BuildDir).Path
$Version = [regex]::Match((Get-Content "$Repo/CMakeLists.txt" -Raw), 'project\(Sonicator VERSION ([0-9.]+)').Groups[1].Value
if ($VersionOverride) { $Version = $VersionOverride }
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid MSI version' }
$Exe = "$BuildDir/Sonicator_artefacts/Release/SONICATOR.exe"
$Plugin = "$BuildDir/plugin/SonicatorPlugin_artefacts/Release/VST3/SONICATOR.vst3"
foreach ($Path in @($Exe, "$BuildDir/Sonicator_artefacts/Release/onnxruntime.dll",
                    "$Plugin/Contents/x86_64-win/SONICATOR.vst3",
                    "$Plugin/Contents/x86_64-win/onnxruntime.dll", "$Plugin/Contents/Resources/moduleinfo.json")) {
    if (!(Test-Path $Path -PathType Leaf)) { throw "Missing Release artifact: $Path" }
}
function Check-NativeExit([string]$Operation) {
    if ($LASTEXITCODE -ne 0) { throw "$Operation failed: exit $LASTEXITCODE" }
}
$Stage = Join-Path ([IO.Path]::GetTempPath()) ('sonicator-msi-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path "$Stage/app", "$Stage/vst3" | Out-Null
try {
    Copy-Item $Exe, "$BuildDir/Sonicator_artefacts/Release/onnxruntime.dll" "$Stage/app/"
    Copy-Item "$Plugin/*" "$Stage/vst3/" -Recurse
    # ONNX's prebuilt Windows runtime uses the MSVC runtime. Ship Microsoft redistributable
    # DLLs app-locally in BOTH loading directories, without a system-wide redist custom action.
    $VsWhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $Vs = & $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Check-NativeExit 'vswhere'
    $Crt = & "$PSScriptRoot/find-crt.ps1" -VisualStudioRoot $Vs
    foreach ($Destination in @("$Stage/app", "$Stage/vst3/Contents/x86_64-win")) {
        Copy-Item "$($Crt.FullName)/*.dll" $Destination
    }
    python "$Repo/packaging/collect-licenses.py" $BuildDir "$Stage/app/Licenses"
    Check-NativeExit 'app licences'
    python "$Repo/packaging/collect-licenses.py" $BuildDir "$Stage/vst3/Contents/Resources/Licenses"
    Check-NativeExit 'plugin licences'
    python "$PSScriptRoot/generate-payload.py" $Stage "$Stage/Payload.wxs"
    Check-NativeExit 'payload generation'
    $License = Get-Content "$Repo/LICENSE" -Raw
    $License = $License.Replace('\', '\\').Replace('{', '\{').Replace('}', '\}').Replace("`r", '').Replace("`n", '\par ')
    [IO.File]::WriteAllText("$Stage/License.rtf", '{\rtf1\ansi\deff0 {\fonttbl {\f0 Arial;}}\f0\fs18 ' + $License + '}', [Text.Encoding]::ASCII)
    $Tools = "$BuildDir/installer-tools"
    if (!(Test-Path "$Tools/wix.exe")) {
        dotnet tool install wix --version 5.0.2 --tool-path $Tools
        Check-NativeExit 'WiX install'
    }
    & "$Tools/wix.exe" extension add -g WixToolset.UI.wixext/5.0.2
    Check-NativeExit 'WiX UI extension'
    New-Item -ItemType Directory -Force $OutputDir | Out-Null
    $OutputDir = (Resolve-Path $OutputDir).Path
    $Msi = "$OutputDir/SONICATOR-$Version-Windows-x64.msi"
    if (Test-Path $Msi) { throw "Refusing to overwrite $Msi" }
    # Debug symbols stay in staging: the release artifact ships the MSI and checksum only.
    & "$Tools/wix.exe" build -arch x64 -ext WixToolset.UI.wixext/5.0.2 -culture en-US `
        -d "Version=$Version" -d "LicenseRtf=$Stage/License.rtf" `
        -pdb "$Stage/SONICATOR.wixpdb" `
        -o $Msi "$PSScriptRoot/Sonicator.wxs" "$Stage/Payload.wxs"
    Check-NativeExit 'MSI build'
    $Hash = (Get-FileHash $Msi -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$Msi.sha256", "$Hash  $([IO.Path]::GetFileName($Msi))`n")
    Write-Host "Created unsigned installer: $Msi"
} finally {
    Remove-Item $Stage -Recurse -Force
}
