<#
.SYNOPSIS
  Build and package a UE code plugin for Fab across multiple UE versions (Windows PowerShell).

.DESCRIPTION
  Uses Unreal AutomationTool (RunUAT.bat BuildPlugin) to compile the plugin for each engine version,
  strips build artifacts, and produces Fab-ready zips with EngineVersion updated in the .uplugin.

.PARAMETER PluginDir
  Path to the plugin root folder (contains *.uplugin).

.PARAMETER OutputDir
  Where to place packaged zips.

.PARAMETER EngineRoots
  One or more Unreal Engine root folders (each must contain Engine/Build/BatchFiles/RunUAT.bat).

.PARAMETER EngineVersions
  Corresponding engine version strings to place in the .uplugin (e.g., 5.4.0 5.5.0 5.6.0).

.EXAMPLE
  .\build_fab.ps1 -PluginDir "U:\UnrealProjects\VMCLiveLinkProject\Plugins\VMCLiveLink" `
                  -OutputDir "U:\FabBuilds" `
                  -EngineRoots "C:\UE\5.4","C:\UE\5.5","C:\UE\5.6" `
                  -EngineVersions "5.4.0","5.5.0","5.6.0"
#>

param(
  [Parameter(Mandatory=$true)][string]$PluginDir,
  [Parameter(Mandatory=$true)][string]$OutputDir,
  [Parameter(Mandatory=$true)][string[]]$EngineRoots,
  [Parameter(Mandatory=$true)][string[]]$EngineVersions
)

function Fail($msg) { Write-Error $msg; exit 1 }

if ($EngineRoots.Count -ne $EngineVersions.Count) {
  Fail "EngineRoots and EngineVersions must have same length."
}

$PluginDir = (Resolve-Path $PluginDir).Path
$OutputDir = (Resolve-Path $OutputDir -ErrorAction SilentlyContinue)
if (-not $OutputDir) { $null = New-Item -ItemType Directory -Path $OutputDir -Force; $OutputDir = (Resolve-Path $OutputDir).Path }

$uplugin = Get-ChildItem -Path $PluginDir -Filter *.uplugin -Recurse | Select-Object -First 1
if (-not $uplugin) { Fail "No .uplugin found under $PluginDir" }
$upluginPath = $uplugin.FullName
$pluginName = Split-Path $uplugin.DirectoryName -Leaf

Write-Host "Plugin: $pluginName"
Write-Host "UPlugin: $upluginPath"
Write-Host "Output: $OutputDir"

function Set-EngineVersionInUPlugin($jsonPath, $engineVersion) {
  $txt = Get-Content -Raw -Path $jsonPath
  # Remove comments crudely
  $txt2 = $txt -replace '(?m)//.*', ''
  $txt2 = [System.Text.RegularExpressions.Regex]::Replace($txt2, '/\*.*?\*/', '', 'Singleline')
  $data = $null
  try { $data = $txt2 | ConvertFrom-Json -ErrorAction Stop } catch { $data = $null }
  if ($null -ne $data) {
    $data.EngineVersion = $engineVersion
    if (-not $data.SupportedTargetPlatforms) { $data.SupportedTargetPlatforms = @("Win64") }
    if (-not $data.Plugins) { $data.Plugins = @() }
    $names = @($data.Plugins | ForEach-Object { $_.Name })
    foreach ($dep in @("LiveLink","OSC")) {
      if ($names -notcontains $dep) { $data.Plugins += @{ Name=$dep; Enabled=$true } }
    }
    ($data | ConvertTo-Json -Depth 100) | Out-File -Encoding UTF8 -FilePath $jsonPath
  } else {
    # Text patch fallback
    if ($txt -match '"EngineVersion"\s*:') {
      $txt = [System.Text.RegularExpressions.Regex]::Replace($txt, '("EngineVersion"\s*:\s*")[^"]*(")', "`$1$engineVersion`$2", 1)
    } else {
      $txt = $txt -replace '\{', "{`n  `"EngineVersion`": `"$engineVersion`"," , 1
    }
    Set-Content -Value $txt -Path $jsonPath -Encoding UTF8
  }
}

for ($i = 0; $i -lt $EngineRoots.Count; $i++) {
  $root = (Resolve-Path $EngineRoots[$i]).Path
  $ver = $EngineVersions[$i]
  $uat = Join-Path $root "Engine\Build\BatchFiles\RunUAT.bat"
  if (-not (Test-Path $uat)) { Fail "RunUAT not found at $uat" }

  $stage = Join-Path $OutputDir "$pluginName-UE$($ver.Replace('.','_'))-Stage"
  if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
  New-Item -ItemType Directory -Path $stage | Out-Null

  # Copy plugin tree to stage and set EngineVersion
  Copy-Item -Recurse -Force $PluginDir $stage
  $stagePluginDir = Join-Path $stage $pluginName
  $stageUPlugin = Get-ChildItem -Path $stagePluginDir -Filter *.uplugin -Recurse | Select-Object -First 1
  if (-not $stageUPlugin) { Fail "Stage .uplugin missing in $stagePluginDir" }
  Set-EngineVersionInUPlugin $stageUPlugin.FullName $ver

  # BuildPlugin with UAT (outputs to a packaged folder without Intermediate/Binaries by default)
  $packageDir = Join-Path $OutputDir "$pluginName-UE$($ver.Replace('.','_'))-Packaged"
  if (Test-Path $packageDir) { Remove-Item -Recurse -Force $packageDir }
  & "$uat" BuildPlugin -Plugin="$($stageUPlugin.FullName)" -Package="$packageDir" -Rocket -StrictIncludes
  if ($LASTEXITCODE -ne 0) { Fail "BuildPlugin failed for UE $ver" }

  # Zip
  $zipPath = Join-Path $OutputDir "$pluginName_UE$($ver.Split('.')[0])_$($ver.Split('.')[1])_Fab.zip"
  if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [System.IO.Compression.ZipFile]::CreateFromDirectory($packageDir, $zipPath)

  Write-Host "Packaged: $zipPath"
}

Write-Host "Done."
