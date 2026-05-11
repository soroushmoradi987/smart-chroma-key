[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [switch] $CreateInstaller
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductDisplayName = if ( $BuildSpec.displayName ) { $BuildSpec.displayName } else { $BuildSpec.name }
    $ProductVersion = $BuildSpec.version
    $ProductAuthor = $BuildSpec.author
    $ProductWebsite = $BuildSpec.website

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $OutputInstallerName = "${ProductName}-${ProductVersion}-windows-${Target}-installer"
    $ReleaseDirectory = Join-Path $ProjectRoot "release/${Configuration}"
    $InstalledPluginDirectory = Join-Path $ReleaseDirectory $ProductName
    $PluginBinaryPath = Join-Path $InstalledPluginDirectory "bin/64bit/${ProductName}.dll"
    $PluginDataDirectory = Join-Path $InstalledPluginDirectory 'data'
    $PackageRoot = Join-Path $ProjectRoot 'release/package'
    $InstallerStageDirectory = Join-Path $PackageRoot "windows-${Target}/installer"
    $InstallerSourceDirectory = Join-Path $InstallerStageDirectory 'plugin'
    $InstallerScriptTemplate = Join-Path $ScriptHome 'resources/Installer-Windows.iss.in'
    $InstallerScriptPath = Join-Path $InstallerStageDirectory 'Installer-Windows.iss'

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
            "${ProjectRoot}/release/${ProductName}-*-windows-*.exe"
        )
    }

    Remove-Item @RemoveArgs

    if ( ! ( Test-Path $InstalledPluginDirectory ) ) {
        throw "Installed plugin directory not found at ${InstalledPluginDirectory}. Run the Windows build and install steps first."
    }

    if ( ! ( Test-Path $PluginBinaryPath ) ) {
        throw "Plugin DLL not found at ${PluginBinaryPath}. Run the Windows build and install steps first."
    }

    if ( ! ( Test-Path $PluginDataDirectory ) ) {
        throw "Plugin data directory not found at ${PluginDataDirectory}."
    }

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path $ReleaseDirectory -Exclude "${OutputName}*.*")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    Log-Group

    if ( ! $CreateInstaller ) {
        return
    }

    $IsccCommand = Get-Command -Name ISCC.exe, ISCC -ErrorAction 'SilentlyContinue' |
        Select-Object -First 1

    if ( $null -eq $IsccCommand ) {
        throw 'Inno Setup compiler (ISCC.exe) was not found on PATH. Install Inno Setup or use Build-Installer-Windows.ps1 -InstallDependencies.'
    }

    Remove-Item -Path $InstallerStageDirectory -Recurse -Force -ErrorAction 'SilentlyContinue'
    New-Item -ItemType Directory -Force -Path (Join-Path $InstallerSourceDirectory 'bin/64bit') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $InstallerSourceDirectory 'data') | Out-Null

    Copy-Item -Path $PluginBinaryPath -Destination (Join-Path $InstallerSourceDirectory 'bin/64bit') -Force
    Copy-Item -Path (Join-Path $PluginDataDirectory '*') -Destination (Join-Path $InstallerSourceDirectory 'data') -Recurse -Force

    $InstallerScript = Get-Content -Path $InstallerScriptTemplate -Raw
    $InstallerVariables = @{
        '@APP_ID@' = "${ProductName}-obs-plugin"
        '@PRODUCT_NAME@' = $ProductName
        '@PRODUCT_DISPLAY_NAME@' = $ProductDisplayName
        '@PRODUCT_VERSION@' = $ProductVersion
        '@PRODUCT_PUBLISHER@' = $ProductAuthor
        '@PRODUCT_URL@' = $ProductWebsite
        '@LICENSE_FILE@' = (Join-Path $ProjectRoot 'LICENSE')
        '@OUTPUT_DIR@' = (Join-Path $ProjectRoot 'release')
        '@OUTPUT_BASENAME@' = $OutputInstallerName
        '@STAGING_DIR@' = $InstallerStageDirectory
    }

    foreach ( $Entry in $InstallerVariables.GetEnumerator() ) {
        $InstallerScript = $InstallerScript.Replace($Entry.Key, $Entry.Value.Replace('"', '""'))
    }

    Set-Content -Path $InstallerScriptPath -Value $InstallerScript -NoNewline

    Log-Group "Building ${ProductName} installer..."
    Invoke-External $IsccCommand.Source '/Qp' $InstallerScriptPath
    Log-Group
}

Package
