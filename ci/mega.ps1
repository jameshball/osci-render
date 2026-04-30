param(
  [Parameter(Mandatory = $true)]
  [ValidateSet('Install', 'UploadPluginvalLogs', 'UploadBinaries')]
  [string] $Task,

  [string] $Project,
  [string] $ProjectVersion,
  [string] $Variant,
  [string] $OsArtifact,
  [string] $LogDir = 'bin/pluginval-logs'
)

function Add-MegaPath {
  $megaDir = $null

  if (Test-Path "$env:LOCALAPPDATA\MEGAcmd") {
    $megaDir = "$env:LOCALAPPDATA\MEGAcmd"
  } elseif (Test-Path 'C:\Users\runneradmin\AppData\Local\MEGAcmd') {
    $megaDir = 'C:\Users\runneradmin\AppData\Local\MEGAcmd'
  } else {
    $megaLogin = Get-Command mega-login -ErrorAction SilentlyContinue
    if ($megaLogin -and $megaLogin.Source) {
      $megaDir = Split-Path $megaLogin.Source
    }
  }

  if ($megaDir -and (Test-Path $megaDir)) {
    Write-Host "MEGAcmd directory: $megaDir"
    $env:PATH = "$megaDir;$env:PATH"

    if ($env:GITHUB_PATH) {
      $megaDir | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
    }

    return $true
  }

  return $false
}

function Ensure-MegaPath {
  if (-not (Get-Command mega-login -ErrorAction SilentlyContinue)) {
    [void] (Add-MegaPath)
  }

  if (-not (Get-Command mega-login -ErrorAction SilentlyContinue)) {
    throw 'MEGAcmd is not available on PATH.'
  }
}

function Invoke-NativeChecked {
  param(
    [Parameter(Mandatory = $true)]
    [string] $FilePath,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Arguments
  )

  & $FilePath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$FilePath failed with exit code $LASTEXITCODE"
  }
}

function Require-MegaCredentials {
  if (-not $env:MEGA_USERNAME -or -not $env:MEGA_PASSWORD) {
    throw 'MEGA_USERNAME and MEGA_PASSWORD must be set.'
  }
}

function Install-MegaCmd {
  $ErrorActionPreference = 'Stop'

  if (Add-MegaPath) {
    return
  }

  for ($attempt = 1; $attempt -le 3; $attempt++) {
    Write-Host "Installing MEGAcmd with Chocolatey (attempt $attempt/3)"
    choco install megacmd -y --no-progress

    if ($LASTEXITCODE -eq 0 -and (Add-MegaPath)) {
      return
    }

    if ($attempt -lt 3) {
      Start-Sleep -Seconds (15 * $attempt)
    }
  }

  throw 'MEGAcmd installation not found after Chocolatey install attempts.'
}

function Upload-PluginvalLogs {
  $ErrorActionPreference = 'Continue'
  Require-MegaCredentials
  Ensure-MegaPath

  $dest = "/$Project/$ProjectVersion/pluginval-logs/$OsArtifact"

  mega-login $env:MEGA_USERNAME $env:MEGA_PASSWORD
  if (Test-Path $LogDir) {
    Get-ChildItem $LogDir -File -Recurse | Where-Object { $_.Length -gt 0 } | ForEach-Object {
      Write-Host "Uploading $($_.FullName) -> $dest"
      mega-put -c $_.FullName "$dest/"
    }
  }
  mega-logout
}

function Upload-Binaries {
  $ErrorActionPreference = 'Stop'
  Require-MegaCredentials
  Ensure-MegaPath

  $root = "/$Project/$ProjectVersion"
  if ($Project -eq 'sosci') {
    $variantDest = $root
  } else {
    $variantDest = "$root/$Variant"
  }

  Invoke-NativeChecked mega-login $env:MEGA_USERNAME $env:MEGA_PASSWORD
  Get-ChildItem bin -File | ForEach-Object {
    if ($_.Extension -eq '.pdb') {
      Write-Host "Uploading $($_.FullName) -> $root (debug info)"
      Invoke-NativeChecked mega-put -c $_.FullName "$root/"
    } else {
      Write-Host "Uploading $($_.FullName) -> $variantDest"
      Invoke-NativeChecked mega-put -c $_.FullName "$variantDest/"
    }
  }
  Invoke-NativeChecked mega-logout
}

switch ($Task) {
  'Install' { Install-MegaCmd }
  'UploadPluginvalLogs' { Upload-PluginvalLogs }
  'UploadBinaries' { Upload-Binaries }
}