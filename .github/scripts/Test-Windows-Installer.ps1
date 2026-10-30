$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot/../..").Path
$Installer = (Get-Item "$Root/release/*-installer.exe" | Select-Object -First 1).FullName
$Plugin = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'obs-studio/plugins/smart-chroma-key'
$Expected = Join-Path $Root 'release/Release/smart-chroma-key'
$Report = Join-Path $Root 'build_x64'

# Preview the real wizard without installing, then verify the default install
# location, every shipped byte, the installed filter, and uninstall cleanup.
Add-Type -AssemblyName System.Drawing, UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WizardWindow {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
function Capture-Wizard($Process, $Name) {
    $Window = [System.Windows.Automation.AutomationElement]::FromHandle($Process.MainWindowHandle)
    $Children = $Window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    $Children | ForEach-Object { "$($_.Current.ControlType.ProgrammaticName): $($_.Current.Name)" } | Set-Content "$Report/$Name-ui.txt"
    $Rect = New-Object WizardWindow+Rect
    [void][WizardWindow]::GetWindowRect($Process.MainWindowHandle, [ref]$Rect)
    $Bitmap = New-Object Drawing.Bitmap ($Rect.Right - $Rect.Left), ($Rect.Bottom - $Rect.Top)
    $Graphics = [Drawing.Graphics]::FromImage($Bitmap)
    $DC = $Graphics.GetHdc()
    try { [void][WizardWindow]::PrintWindow($Process.MainWindowHandle, $DC, 2) }
    finally { $Graphics.ReleaseHdc($DC) }
    $Bitmap.Save("$Report/$Name.png", [Drawing.Imaging.ImageFormat]::Png)
    $Graphics.Dispose(); $Bitmap.Dispose()
    return $Window
}
function Click-Button($Window, $Pattern) {
    $Buttons = $Window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
        (New-Object System.Windows.Automation.PropertyCondition ([System.Windows.Automation.AutomationElement]::ControlTypeProperty), ([System.Windows.Automation.ControlType]::Button)))
    foreach ($Button in $Buttons) {
        if ($Button.Current.Name -match $Pattern -and $Button.Current.IsEnabled -and !$Button.Current.IsOffscreen) {
            $Button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
            Start-Sleep -Milliseconds 600
            return
        }
    }
    throw "Wizard button not found: $Pattern"
}
foreach ($Language in @('english', 'farsi')) {
    $Launcher = Start-Process $Installer -ArgumentList @('/SP-', "/LANG=$Language", "/LOG=`"$Report/wizard-$Language.log`"") -PassThru
    $WindowProcess = $null
    try {
        for ($i = 0; $i -lt 30; $i++) {
            Start-Sleep -Milliseconds 300
            $WindowProcess = Get-Process | Where-Object { $_.MainWindowTitle -like '*Smart Chroma Key*' -and $_.MainWindowHandle -ne 0 } | Select-Object -First 1
            if ($WindowProcess) { break }
        }
        if (!$WindowProcess) { throw 'Wizard window was not available' }
        [void][WizardWindow]::SetForegroundWindow($WindowProcess.MainWindowHandle)
        Start-Sleep -Milliseconds 600
        $Window = Capture-Wizard $WindowProcess "wizard-$Language-welcome"
        if ($Language -eq 'english') {
            Click-Button $Window 'Next'
            $Window = Capture-Wizard $WindowProcess 'wizard-english-license'
            $Radios = $Window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                (New-Object System.Windows.Automation.PropertyCondition ([System.Windows.Automation.AutomationElement]::ControlTypeProperty), ([System.Windows.Automation.ControlType]::RadioButton)))
            foreach ($Radio in $Radios) {
                if ($Radio.Current.Name -match '^I accept') { $Radio.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select() }
            }
            Click-Button $Window 'Next'
            $null = Capture-Wizard $WindowProcess 'wizard-english-guide'
        }
    } finally {
        # Stop only the installer processes started by this preview.
        if ($WindowProcess -and !$WindowProcess.HasExited) { Stop-Process -Id $WindowProcess.Id -Force }
        if (!$Launcher.HasExited) { Stop-Process -Id $Launcher.Id -Force }
    }
}

if (Test-Path $Plugin) { throw 'Unexpected pre-existing plugin on clean Windows runner' }
$Process = Start-Process $Installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/LANG=english', "/LOG=`"$Report/installer-test.log`"") -Wait -PassThru
if ($Process.ExitCode -ne 0) { throw "Installer exit code: $($Process.ExitCode)" }
foreach ($File in Get-ChildItem $Expected -Recurse -File | Where-Object { $_.Extension -ne '.pdb' }) {
    $Relative = [IO.Path]::GetRelativePath($Expected, $File.FullName)
    $Installed = Join-Path $Plugin $Relative
    if (!(Test-Path $Installed) -or (Get-FileHash $Installed).Hash -ne (Get-FileHash $File.FullName).Hash) { throw "Installed file differs: $Relative" }
}
if (!(Test-Path "$Plugin/setup.ico") -or !(Test-Path "$Plugin/data/setup-guide.html")) { throw 'Branding/help not installed' }
$RuntimeBin = Join-Path $env:RUNNER_TEMP 'sck-obs-runtime/bin/64bit'
Push-Location $RuntimeBin
try {
    & ./sck-windows-test.exe "$Plugin/smart-chroma-key.dll" "$Plugin/data" full 2>&1 | Tee-Object "$Report/installed-runtime.log"
    if ($LASTEXITCODE) { throw 'Installed plugin runtime test failed' }
} finally { Pop-Location }
$Uninstaller = (Get-Item "$Plugin/unins*.exe" | Select-Object -First 1).FullName
$Process = Start-Process $Uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$Report/uninstaller-test.log`"") -Wait -PassThru
if ($Process.ExitCode -ne 0) { throw "Uninstaller exit code: $($Process.ExitCode)" }
foreach ($Relative in @('smart-chroma-key.dll', 'bin/64bit/smart-chroma-key.dll', 'data/rvm.onnx', 'data/runtime/sck-onnxruntime.dll', 'data/setup-guide.html', 'setup.ico')) {
    if (Test-Path (Join-Path $Plugin $Relative)) { throw "Uninstaller left $Relative" }
}
'PASS: English/Persian wizard preview, default ProgramData install, byte comparison, installed Classic/AI runtime, uninstall cleanup' | Set-Content "$Report/installer-verification.txt"
