# Destructive to THIS product's installation: run only on a disposable GitHub runner.
param([Parameter(Mandatory=$true)][string]$Msi,
      [Parameter(Mandatory=$true)][string]$UpgradeMsi,
      [Parameter(Mandatory=$true)][string]$Pluginval,
      [Parameter(Mandatory=$true)][string]$LogDir)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'Installer smoke test requires a disposable GitHub Actions runner' }
$Msi = (Resolve-Path $Msi).Path
$UpgradeMsi = (Resolve-Path $UpgradeMsi).Path
New-Item -ItemType Directory -Force $LogDir | Out-Null
$LogDir = (Resolve-Path $LogDir).Path
$App = "$env:ProgramFiles/SONICATOR/SONICATOR.exe"
$Plugin = "$env:CommonProgramFiles/VST3/SONICATOR.vst3"
$Library = "$env:APPDATA/Sonicator"
$Counter = 0
function Invoke-Msi([string[]]$Arguments) {
    $script:Counter++
    $Log = "$LogDir/msi-$script:Counter.log"
    $Process = Start-Process msiexec.exe -ArgumentList ($Arguments + @('/qn', '/norestart', '/l*v', "`"$Log`"")) -PassThru -Wait
    if ($Process.ExitCode -notin @(0, 3010)) {
        if (Test-Path $Log) { Get-Content $Log -Tail 60 | Write-Host }
        throw "msiexec failed $($Process.ExitCode); see $Log"
    }
}
function Assert-Features([bool]$Application, [bool]$Vst3) {
    if ((Test-Path $App) -ne $Application) { throw "Application selection mismatch ($Application)" }
    if ((Test-Path "$Plugin/Contents/x86_64-win/SONICATOR.vst3") -ne $Vst3) { throw "VST3 selection mismatch ($Vst3)" }
    if ($Application -and !(Test-Path "$(Split-Path $App)/onnxruntime.dll")) { throw 'Missing installed app runtime' }
    if ($Vst3 -and !(Test-Path "$Plugin/Contents/x86_64-win/onnxruntime.dll")) { throw 'Missing installed plugin runtime' }
}
$ActiveMsi = $Msi
try {
    Invoke-Msi -Arguments @('/i', "`"$Msi`"")
    Assert-Features $true $true
    # This smoke test is about installation, not answering the first-launch consent dialog.
    # Windows WM_CLOSE is correctly ignored while another modal component is active.
    # Seed a fresh disposable profile with sharing OFF, never an owner's existing settings.
    if (Test-Path "$Library/settings.json") { throw 'Expected a fresh disposable application profile' }
    New-Item -ItemType Directory -Force $Library | Out-Null
    $ConsentVersion = [int][regex]::Match((Get-Content "$PSScriptRoot/../../core/settings/AppSettings.h" -Raw),
        'currentContributionConsentVersion\s*=\s*(\d+)').Groups[1].Value
    if ($ConsentVersion -le 0) { throw 'Cannot determine consent version for fixture profile' }
    $Settings = @{ contributionConsentVersion = $ConsentVersion; contributeTrainingData = $false } | ConvertTo-Json
    [IO.File]::WriteAllText("$Library/settings.json", $Settings)
    # A real window proves exe and dependency loading; this is not audio-device/DAW acceptance.
    $Process = Start-Process $App -PassThru
    $Ready = $false
    for ($Attempt = 0; $Attempt -lt 30; $Attempt++) {
        Start-Sleep -Milliseconds 500
        $Process.Refresh()
        if ($Process.HasExited) { throw "Installed app exited early: $($Process.ExitCode)" }
        if ($Process.MainWindowHandle -ne 0) { $Ready = $true; break }
    }
    if (!$Ready) { $Process.Kill(); throw 'Installed app did not create its window' }
    $null = $Process.CloseMainWindow()
    if (!$Process.WaitForExit(15000)) { $Process.Kill(); throw 'Installed app did not shut down cleanly' }
    if ($Process.ExitCode -ne 0) { throw "Installed app exit: $($Process.ExitCode)" }
    New-Item -ItemType Directory -Force "$Library/exports" | Out-Null
    [IO.File]::WriteAllText("$Library/exports/installer-test.wav", 'project media sentinel')
    $Saved = @{}
    foreach ($File in @("$Library/sonicator.sqlite", "$Library/settings.json", "$Library/exports/installer-test.wav")) {
        if (!(Test-Path $File)) { throw "Application did not create expected file: $File" }
        $Saved[$File] = (Get-FileHash $File -Algorithm SHA256).Hash
    }
    function Assert-UserData {
        foreach ($File in $Saved.Keys) {
            if (!(Test-Path $File) -or (Get-FileHash $File -Algorithm SHA256).Hash -ne $Saved[$File]) {
                throw "Installer changed user data: $File"
            }
        }
    }
    # The host is outside the plugin directory; no build path or CWD can rescue DLL loading.
    # Start-Process reports the raw process exit code and stderr, unlike $LASTEXITCODE.
    function Invoke-Pluginval([string]$PluginPath, [string]$Label) {
        $Slug = $Label -replace '[^A-Za-z0-9]+', '-'
        $Out = "$LogDir/pluginval-$Slug.out.txt"
        $Err = "$LogDir/pluginval-$Slug.err.txt"
        $Process = Start-Process $Pluginval -NoNewWindow -Wait -PassThru `
            -WorkingDirectory $env:RUNNER_TEMP `
            -RedirectStandardOutput $Out -RedirectStandardError $Err `
            -ArgumentList @('--strictness-level', '10', '--skip-gui-tests',
                            '--output-dir', "`"$LogDir`"", '--validate', "`"$PluginPath`"")
        Get-Content $Out -Tail 4 | Write-Host
        if ((Test-Path $Err) -and (Get-Item $Err).Length -gt 0) { Get-Content $Err -Tail 20 | Write-Host }
        Write-Host "pluginval [$Label] exit code: $($Process.ExitCode)"
        return $Process.ExitCode
    }
    $InstalledCode = Invoke-Pluginval $Plugin 'installed VST3'
    $BuiltCode = Invoke-Pluginval (Resolve-Path 'build/plugin/SonicatorPlugin_artefacts/Release/VST3/SONICATOR.vst3').Path 'build-tree VST3'
    & $Pluginval --version | Write-Host
    Get-ChildItem "$env:LOCALAPPDATA/CrashDumps" -ErrorAction SilentlyContinue |
        Select-Object Name, Length, LastWriteTime | Out-String | Write-Host
    Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = (Get-Date).AddMinutes(-20) } -ErrorAction SilentlyContinue |
        Where-Object { $_.ProviderName -match 'Application Error|Windows Error Reporting|Application Hang' } |
        Select-Object ProviderName, TimeCreated, Message | Out-String | Write-Host
    if ($InstalledCode -ne 0 -or $BuiltCode -ne 0) {
        throw "Installed VST3 validation exit $InstalledCode; build-tree VST3 exit $BuiltCode"
    }
    Invoke-Msi -Arguments @('/fvomus', "`"$Msi`"")
    Assert-Features $true $true
    Assert-UserData
    Invoke-Msi -Arguments @('/i', "`"$UpgradeMsi`"")
    $ActiveMsi = $UpgradeMsi
    Assert-Features $true $true
    Assert-UserData
    Invoke-Msi -Arguments @('/x', "`"$UpgradeMsi`"")
    Assert-Features $false $false
    Assert-UserData
    foreach ($Feature in @('AppFeature', 'Vst3Feature')) {
        $ActiveMsi = $Msi
        Invoke-Msi -Arguments @('/i', "`"$Msi`"", "ADDLOCAL=$Feature")
        Assert-Features ($Feature -eq 'AppFeature') ($Feature -eq 'Vst3Feature')
        Assert-UserData
        Invoke-Msi -Arguments @('/x', "`"$Msi`"")
        Assert-Features $false $false
        Assert-UserData
    }
    Write-Host 'PASS: installed window, pluginval, repair, major upgrade, feature selections, uninstall and user-data preservation'
} finally {
    # Only our MSI's registered product; never recursively delete Program Files or user media.
    $null = Start-Process msiexec.exe -ArgumentList @('/x', "`"$ActiveMsi`"", '/qn', '/norestart') -PassThru -Wait
}
