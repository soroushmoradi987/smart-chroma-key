$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path "$PSScriptRoot/../..").Path
$Runtime = Join-Path $env:RUNNER_TEMP 'sck-obs-runtime'
New-Item -ItemType Directory -Force $Runtime | Out-Null
$Archive = Join-Path $Runtime 'obs.zip'
Invoke-WebRequest 'https://github.com/obsproject/obs-studio/releases/download/31.1.1/OBS-Studio-31.1.1-Windows-x64.zip' -OutFile $Archive
if ((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '9d8dceb77acd8af04af23f877061f63c9bef78ca73d2093d0ccba1bb9104173f') { throw 'OBS checksum mismatch' }
Expand-Archive $Archive -DestinationPath $Runtime -Force
$Bin = Join-Path $Runtime 'bin/64bit'
Copy-Item "$Root/build_x64/Release/sck-*-test.exe" $Bin
$Plugin = Join-Path $Root 'release/Release/smart-chroma-key'
$Modern = Join-Path $Plugin 'smart-chroma-key.dll'
$Legacy = Join-Path $Plugin 'bin/64bit/smart-chroma-key.dll'
$Data = Join-Path $Plugin 'data'
foreach ($File in @($Modern, $Legacy, "$Data/smart-chroma-key.effect", "$Data/rvm-refine.effect", "$Data/locale/en-US.ini", "$Data/rvm.onnx", "$Data/runtime/sck-onnxruntime.dll")) {
    if (!(Test-Path $File)) { throw "Missing package file: $File" }
}
if ((Get-FileHash $Modern).Hash -ne (Get-FileHash $Legacy).Hash) { throw 'Modern/legacy DLL mismatch' }
$BadShader = Join-Path $Runtime 'original-broken.effect'
(Get-Content "$Data/smart-chroma-key.effect" -Raw).Replace('color_axis', 'line') | Set-Content $BadShader
Push-Location $Bin
try {
    & ./sck-windows-test.exe $Modern $BadShader shader-must-fail 2>&1 | Tee-Object "$Root/build_x64/shader-before.log"
    if ($LASTEXITCODE) { throw 'Original shader regression was not reproduced' }
    & ./sck-pipeline-test.exe libobs-d3d11.dll $Modern $Data 2>&1 | Tee-Object "$Root/build_x64/classic-pipeline.log"
    if ($LASTEXITCODE) { throw 'Classic Direct3D pipeline failed' }
    & ./sck-windows-test.exe $Modern $Data full 2>&1 | Tee-Object "$Root/build_x64/windows-ai.log"
    if ($LASTEXITCODE) { throw 'Windows controls/AI runtime failed' }
    # Loading the legacy DLL is independently checked without the AI payload.
    $WithoutAI = Join-Path $Runtime 'classic-data'
    Copy-Item $Data $WithoutAI -Recurse
    Remove-Item "$WithoutAI/rvm.onnx", "$WithoutAI/runtime" -Recurse
    & ./sck-windows-test.exe $Legacy $WithoutAI classic-only 2>&1 | Tee-Object "$Root/build_x64/classic-no-ai.log"
    if ($LASTEXITCODE) { throw 'Classic must work independently of the AI runtime' }
} finally { Pop-Location }
