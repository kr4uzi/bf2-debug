# Configuration Script for Battlefield 2 Python Debugger
# - compatible with custom dice-py.dlls
# - if custom dice-py.dll is detected (versions other than 2.3.4), copy headers from official python source
# - generates the .def and .lib file required for the debugger
param (
  [string]$BF2Dir
)

function Get-RegistryKey32Value {
  param (
    [string]$Path,
    [string]$Name
  )
  $res = reg query $Path /reg:32 /v $Name 2>$null
  if ($res -And $res[2] -match "^\s+$Name\s+REG_SZ\s+(.+)$") {
    return $Matches[1]
  }
}

function Get-BattlefieldDirectory {
  param (
    [string]$InitialValue,
    [string]$UserBF2Dir
  )

  $bf2Dir = $UserBF2Dir

  if (!$bf2Dir) {
    Write-Host "Checking Battlefield 2 Server Installation ..."
    $bf2Dir = Get-RegistryKey32Value -Path "HKEY_LOCAL_MACHINE\SOFTWARE\EA GAMES\Battlefield 2 Server" -Name "GAMEDIR"
  }

  if (!$bf2Dir) {
    Write-Host "Checking Battlefield 2 Client Installation ..."
    $bf2Dir = Get-RegistryKey32Value -Path "HKEY_LOCAL_MACHINE\SOFTWARE\Electronic Arts\EA Games\Battlefield 2" -Name "InstallDir"
  }

  if (!$bf2Dir) {
    $bf2Dir = $InitialValue
  }

  if (!$bf2Dir) {
    Write-Host "Unable to autodetect Battlefield 2 Installation, please select it manually"
    Add-Type -AssemblyName System.Windows.Forms
    $browser = New-Object System.Windows.Forms.FolderBrowserDialog
    $browser.ShowNewFolderButton = $false
    $browser.SelectedPath = $Env:Programfiles
    $browser.Description = "This PowerShell Script will check the python version of the dice_py.dll. Please select the Battlefield 2 install directory."
    if ($browser.ShowDialog() -ne "OK") {
      return
    }

    $bf2Dir = $browser.SelectedPath
  }

  if (Test-Path "$bf2Dir\\dice_py.dll") {
    Write-Host "> Using Battlefield 2 Directory: $bf2Dir"
    return $bf2Dir
  }

  Write-Host "Invalid Folder: dice_py.dll does not exist"
}

function Get-Battlefield2PythonVersion {
  param (
    [string]$BF2Dir
  )

  Write-Host "Detecting python version ..."
  $script = Start-Job -ScriptBlock {
    param([string]$Path)
    $dllPath = (Join-Path $Path "dice_py.dll") -replace "\\", "\\\\"
    Add-Type -TypeDefinition @"
  using System;
  using System.Runtime.InteropServices;

  public class PythonMethods {
      [DllImport("$dllPath", CallingConvention = CallingConvention.Cdecl)]
      public static extern IntPtr Py_GetVersion();
  }
"@

    return [Runtime.InteropServices.Marshal]::PtrToStringAnsi([PythonMethods]::Py_GetVersion())
   } -RunAs32 -ArgumentList $BF2Dir
  $pyVersion = $script | Wait-Job | Receive-Job
  if ($pyVersion -match '(\d+\.\d+\.\d+)') {
    Write-Host "> Detected python version $($Matches[1])"
    return $Matches[1]
  }
}

function Get-VSCommandCmd {
  if (Get-Command "VsDevCmd.bat" -ErrorAction SilentlyContinue) {
    # VsDevCmd.bat is already in the path
    return "VsDevCmd.bat"
  }

  Write-Host "Detecting Visual Studio installation ..."
  $programFilesX86 = ${env:ProgramFiles(x86)}
  if (-not $programFilesX86) {
    $programFilesX86 = $Env:ProgramFiles
  }

  $vsWherePath = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
  if (-Not (Test-Path $vsWherePath)) {
    Write-Host "vswhere.exe not found at $vsWherePath"
    return
  }

  $vsInstallations = & $vsWherePath -legacy -prerelease -format json | ConvertFrom-Json
  if (!$vsInstallations.Count) {
    Write-Host "Unable to detect Visual Studio Installation"
  }

  $visualStudioPath = $vsInstallations[0].installationPath

  $vsDevCmdPath = Join-Path $visualStudioPath "Common7\Tools\VsDevCmd.bat"
  if (!(Test-Path $vsDevCmdPath)) {
    Write-Error "Invalid or no Visual Studio installation found"
    Write-Error "Please execute this script using the Visual Studio Command Line: Tools > Command Line > Developer PowerShell"
    return
  }

  return $vsDevCmdPath
}

function Add-DicePyLibrary {
  param(
    [string]$BF2Dir
  )

  Write-Host "Creating dice_py.lib ..."
  $dllPath = Join-Path $BF2Dir "dice_py.dll" -ErrorAction Stop

  $vsDevCmd = Get-VSCommandCmd
  if (!$vsDevCmd) {
    return
  }
  
  $dumpbinOutput = cmd.exe /c "`"$vsDevCmd`" -no_logo && dumpbin.exe /EXPORTS `"$dllPath`"" | Out-String
  $defContent = @()
  $defContent += "LIBRARY dice_py"
  $defContent += "EXPORTS"
  foreach ($line in $dumpbinOutput -split "`n") {
    if ($line -match "^\s*(\d+)\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)\s*$") {
      $ordinal = $Matches[1]
      $symbol = $Matches[2]
      $defContent += "    $($symbol) @$($ordinal)"
    }
  }
  Write-Host "> Created dice_py.def"

  $defContent = $defContent -join "`n"
  Set-Content -Path "dice_py.def" -Value $defContent
  cmd.exe /c "`"$vsDevCmd`" -no_logo && lib /NOLOGO /DEF:dice_py.def /OUT:dice_py.lib /MACHINE:x86"
  Write-Host "> Created dice_py.lib"
}

function main {
  param (
    [string]$UserBF2Dir
  )

  [xml]$bf2Props = Get-Content "bf2.props" -ErrorAction Stop
  $props = $bf2Props.Project.PropertyGroup | Where-Object { $_.Label -eq "UserMacros" }
  $bf2Dir = Get-BattlefieldDirectory -InitialValue $props.BF2_DIR -UserBF2Dir $UserBF2Dir
  if (!$bf2Dir) {
    Write-Host "No Battlefield 2 Directory detected or slected. Aborting..."
    return
  }

  $props.BF2_DIR = $bf2Dir
  $pyVersion = Get-Battlefield2PythonVersion -BF2Dir $bf2Dir
  if (!$pyVersion) {
    Write-Host "Unable to detect python version, please extract the \Include folder from the official source to .\python-<bf2-python-version>"
    Write-Host "Official Python Source Files: https://www.python.org/ftp/python/"
    return
  }

  $props.BF2_PY_VERSION = $pyVersion
  $bf2Props.Save("bf2.props")
  
  Add-DicePyLibrary -BF2Dir $BF2Dir
  if (Test-Path ".\python-$pyVersion\patchlevel.h") {
    foreach ($line in Get-Content ".\python-$pyVersion\patchlevel.h") {
      if ($line -match '^\s*#\s*define\s*PY_VERSION\s+"(\d+\.\d+\.\d+)"') {
          $cfgPyVersion = $Matches[1]
          break
      }
    }

    if ($cfgPyVersion -eq $pyVersion) {
      Write-Host "Version $cfgPyVersion already configured"
      return
    }
  }

  $dlPath = "$env:TEMP\Python-$pyVersion.tgz"
  if (Test-Path $dlPath) {
    Write-Output "Using pre-downloaded source archive"
  }
  
  $dlUrl = "https://www.python.org/ftp/python/$pyVersion/Python-$pyVersion.tgz"
  if (Get-Command tar -ErrorAction SilentlyContinue) {
    Write-Output "Downloading python headers from official source for detected version: $pyVersion"
    Invoke-WebRequest "$dlUrl" -OutFile $dlPath -ErrorAction Stop

    tar -xzf $dlPath -C $env:TEMP
    if (Test-Path -Path ".\python-$pyVersion") {
      Remove-Item -Path ".\python-$pyVersion" -Recurse
    }

    $unzipDir = Join-Path $env:TEMP "Python-$pyVersion"
    Move-Item -Path "$unzipDir\Include" -Destination ".\python-$pyVersion"
    Move-Item -Path "$unzipDir\PC\pyconfig.h" -Destination ".\python-$pyVersion\pyconfig.h"
    # Remove-Item is unable to remove certain the files in Mac/IDE, only rmdir seems to work
    cmd.exe /c "rmdir /s /q $unzipDir"
    Remove-Item $dlPath
  } else {
    Remove-Item $dlPath
    Write-Error "No tool to unzip .tgz files is installed, please manually extract \Include to .\python-$pyVersion from:"
    Write-Error $dlUrl
  }
}

main -UserBF2Dir $BF2Dir
if ($Host.Name -eq 'ConsoleHost') {
  Write-Host "Press any key to continue..."
  $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyUp") > $null
}