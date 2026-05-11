[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'Release',
    [switch] $InstallDependencies
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw 'A 64-bit system is required to build the installer.'
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The Windows installer build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Ensure-InnoSetup {
    $IsccCommand = Get-Command -Name ISCC.exe, ISCC -ErrorAction 'SilentlyContinue' | Select-Object -First 1

    if ( $null -ne $IsccCommand ) {
        return
    }

    if ( ! $InstallDependencies ) {
        throw 'Inno Setup compiler (ISCC.exe) was not found on PATH. Re-run with -InstallDependencies, or install Inno Setup manually.'
    }

    $Winget = Get-Command -Name winget -ErrorAction 'SilentlyContinue'
    $Choco = Get-Command -Name choco -ErrorAction 'SilentlyContinue'

    if ( $null -ne $Winget ) {
        & $Winget.Source install --id JRSoftware.InnoSetup --exact --accept-source-agreements --accept-package-agreements --silent
        if ( $LASTEXITCODE -ne 0 ) {
            throw 'winget failed to install Inno Setup.'
        }
    } elseif ( $null -ne $Choco ) {
        & $Choco.Source install innosetup --no-progress -y
        if ( $LASTEXITCODE -ne 0 ) {
            throw 'choco failed to install Inno Setup.'
        }
    } else {
        throw 'Inno Setup compiler (ISCC.exe) was not found and neither winget nor choco is available for automatic installation.'
    }
}

function Build-Installer {
    trap {
        Write-Error $_
        exit 2
    }

    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    Push-Location $ProjectRoot
    try {
        Ensure-InnoSetup

        $BuildArgs = @{
            Target = $Target
            Configuration = $Configuration
        }

        & (Join-Path $PSScriptRoot 'Build-Windows.ps1') @BuildArgs
        if ( $LASTEXITCODE -ne 0 ) {
            throw 'Build-Windows.ps1 failed.'
        }

        $PackageArgs = @{
            Target = $Target
            Configuration = $Configuration
            CreateInstaller = $true
        }

        & (Join-Path $PSScriptRoot 'Package-Windows.ps1') @PackageArgs
        if ( $LASTEXITCODE -ne 0 ) {
            throw 'Package-Windows.ps1 failed.'
        }
    }
    finally {
        Pop-Location
    }
}

Build-Installer