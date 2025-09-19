<#
.SYNOPSIS
  Build and package a UE code plugin for Fab across multiple UE versions (Windows PowerShell).

.DESCRIPTION
  Uses Unreal AutomationTool (RunUAT.bat BuildPlugin) to compile the plugin for each engine version,
  strips build artifacts, and produces Fab-ready zips with EngineVersion updated in the .uplugin.

  Fixes/changes:
  - More robust comment stripping that does not remove '//' or '/* */' that appear inside JSON string values.
  - Avoids accidental literal $1/$2 insertion by using a MatchEvaluator when doing regex replacements.
  - Writes JSON using Set-Content with explicit UTF8 to avoid PowerShell formatting quirks.
  - Minor robustness improvements (explicit -Path and -Destination, -ErrorAction on critical steps).

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

$PluginDir = (Resolve-Path -Path $PluginDir -ErrorAction Stop).Path

$OutputDirRaw = $OutputDir
$resolved = Resolve-Path -LiteralPath $OutputDirRaw -ErrorAction SilentlyContinue
if ($null -eq $resolved) {
  New-Item -ItemType Directory -Path $OutputDirRaw -Force | Out-Null
  $resolved = Resolve-Path -LiteralPath $OutputDirRaw -ErrorAction Stop
}
$OutputDir = $resolved.Path

$uplugin = Get-ChildItem -Path $PluginDir -Filter *.uplugin -Recurse -File | Select-Object -First 1
if (-not $uplugin) { Fail "No .uplugin found under $PluginDir" }
$upluginPath = $uplugin.FullName
$pluginName = Split-Path $uplugin.DirectoryName -Leaf

Write-Host "Plugin: $pluginName"
Write-Host "UPlugin: $upluginPath"
Write-Host "Output: $OutputDir"

# Remove JSON comments using a simple state machine so we don't remove sequences inside string literals.
function Remove-JsonComments([string]$txt) {
  $sb = New-Object System.Text.StringBuilder
  $inString = $false
  $inSingleLineComment = $false
  $inMultiLineComment = $false
  $escape = $false

  for ($i=0; $i -lt $txt.Length; $i++) {
    $ch = $txt[$i]

    if ($inSingleLineComment) {
      if ($ch -eq "`n") {
        $inSingleLineComment = $false
        $sb.Append($ch) | Out-Null
      } else {
        # skip
      }
      continue
    }

    if ($inMultiLineComment) {
      if ($ch -eq '*' -and ($i+1 -lt $txt.Length) -and $txt[$i+1] -eq '/') {
        $inMultiLineComment = $false
        $i++ # skip '/'
      }
      continue
    }

    if ($inString) {
      if (-not $escape -and $ch -eq '"') {
        $inString = $false
        $sb.Append($ch) | Out-Null
        $escape = $false
        continue
      }
      if (-not $escape -and $ch -eq '\') {
        $escape = $true
        $sb.Append($ch) | Out-Null
        continue
      }
      if ($escape) {
        $escape = $false
      }
      $sb.Append($ch) | Out-Null
      continue
    }

    # Not in string or comment
    if ($ch -eq '"') {
      $inString = $true
      $sb.Append($ch) | Out-Null
      continue
    }

    # Check for // (single line)
    if ($ch -eq '/' -and ($i+1 -lt $txt.Length) -and $txt[$i+1] -eq '/') {
      $inSingleLineComment = $true
      $i++ # skip second '/'
      continue
    }

    # Check for /* */ (multi-line)
    if ($ch -eq '/' -and ($i+1 -lt $txt.Length) -and $txt[$i+1] -eq '*') {
      $inMultiLineComment = $true
      $i++ # skip '*'
      continue
    }

    $sb.Append($ch) | Out-Null
  }

  return $sb.ToString()
}

function Set-EngineVersionInUPlugin($jsonPath, $engineVersion) {
  $txt = Get-Content -Raw -Path $jsonPath -ErrorAction Stop

  # Remove comments safely
  $txt2 = Remove-JsonComments $txt

  $data = $null
  try {
    $data = $txt2 | ConvertFrom-Json -ErrorAction Stop
  } catch {
    $data = $null
  }

  if ($null -ne $data) {
    # Some JSON->PowerShell conversions can produce objects that are not directly mutable
    # (arrays, non-PSCustomObject wrappers, etc). Build a fresh PSCustomObject (or array thereof)
    # and modify that so we avoid "property cannot be found" / "cannot set" exceptions.
    $makeMutable = {
      param($obj)
      if ($obj -is [System.Array]) {
        # If it's an array of objects, try to make each element a PSCustomObject where possible
        $newArr = @()
        foreach ($el in $obj) {
          if ($el -ne $null -and $el.PSObject -and $el.PSObject.Properties.Count -gt 0) {
            $h = @{}
            foreach ($p in $el.PSObject.Properties) { $h[$p.Name] = $p.Value }
            $newArr += [PSCustomObject]$h
          } else {
            $newArr += $el
          }
        }
        return ,$newArr
      } elseif ($obj -ne $null -and $obj.PSObject -and $obj.PSObject.Properties.Count -gt 0) {
        $h = @{}
        foreach ($p in $obj.PSObject.Properties) { $h[$p.Name] = $p.Value }
        return [PSCustomObject]$h
      } else {
        return $obj
      }
    }

    $mutable = & $makeMutable $data

    # Now we expect $mutable to be a PSCustomObject (or array with PSCustomObject element(s))
    if ($mutable -is [System.Array]) {
      # If it's an array, attempt to update the first object element that has properties.
      $updated = $false
      for ($idx = 0; $idx -lt $mutable.Count; $idx++) {
        $el = $mutable[$idx]
        if ($el -and $el.PSObject -and $el.PSObject.Properties.Count -gt 0) {
          if (-not $el.PSObject.Properties.Match('SupportedTargetPlatforms')) {
            $el | Add-Member -MemberType NoteProperty -Name SupportedTargetPlatforms -Value @("Win64")
          } elseif (-not $el.SupportedTargetPlatforms) {
            $el.SupportedTargetPlatforms = @("Win64")
          }
          $mutable[$idx] = $el
          $updated = $true
          break
        }
      }
      if (-not $updated) {
        # couldn't find a suitable element to update, fall back to text-patch below
        $mutable = $null
      }
    } else {
      # single object
      if (-not $mutable.PSObject.Properties.Match('SupportedTargetPlatforms')) {
        $mutable | Add-Member -MemberType NoteProperty -Name SupportedTargetPlatforms -Value @("Win64")
      } elseif (-not $mutable.SupportedTargetPlatforms) {
        $mutable.SupportedTargetPlatforms = @("Win64")
      }
    }

    if ($null -ne $mutable) {
      $jsonOut = $mutable | ConvertTo-Json -Depth 100 -Compress:$false
      # Write UTF8 without BOM
      Set-Content -Path $jsonPath -Value $jsonOut -Encoding UTF8
      return
    }
    # else fall through to text patch fallback
  } 

  # Text patch fallback (careful to only change the EngineVersion property)
  if ($txt -match '"EngineVersion"\s*:') {
    # Use a MatchEvaluator to avoid accidental literal $1/$2 in the output
    $pattern = '("EngineVersion"\s*:\s*")([^"]*)(")'
    $evaluator = [System.Text.RegularExpressions.MatchEvaluator]{
      param($m)
      return $m.Groups[1].Value + $engineVersion + $m.Groups[3].Value
    }
    $txt = [System.Text.RegularExpressions.Regex]::Replace($txt, $pattern, $evaluator, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
  } else {
    # Insert EngineVersion after the opening brace only (anchor to start)
    # Preserve indentation of the first line if possible
    $txt = $txt -replace '^\s*\{', "{`n  `"EngineVersion`": `"$engineVersion`","
  }
  # Write out the patched file
  Set-Content -Value $txt -Path $jsonPath -Encoding UTF8
}

for ($i = 0; $i -lt $EngineRoots.Count; $i++) {
  $root = (Resolve-Path -Path $EngineRoots[$i] -ErrorAction Stop).Path
  $ver = $EngineVersions[$i]
  $uat = Join-Path $root "Engine\Build\BatchFiles\RunUAT.bat"
  if (-not (Test-Path -Path $uat)) { Fail "RunUAT not found at $uat" }
  
  $stage = Join-Path $OutputDir "$pluginName-UE$($ver.Replace('.','_'))-Stage"
  if (Test-Path -Path $stage) { Remove-Item -Recurse -Force -Path $stage }
  New-Item -ItemType Directory -Path $stage -Force | Out-Null

  # Copy plugin tree to stage and set EngineVersion
  Copy-Item -Recurse -Force -Path $PluginDir -Destination $stage
  $stagePluginDir = Join-Path $stage $pluginName
  $stageUPlugin = Get-ChildItem -Path $stagePluginDir -Filter *.uplugin -Recurse -File | Select-Object -First 1
  if (-not $stageUPlugin) { Fail "Stage .uplugin missing in $stagePluginDir" }
  Set-EngineVersionInUPlugin -jsonPath $stageUPlugin.FullName -engineVersion $ver

  # BuildPlugin with UAT (outputs to a packaged folder without Intermediate/Binaries by default)
  $packageDir = Join-Path $OutputDir "$pluginName-UE$($ver.Replace('.','_'))-Packaged"
  if (Test-Path -Path $packageDir) { Remove-Item -Recurse -Force -Path $packageDir }
  & $uat BuildPlugin -Plugin="$($stageUPlugin.FullName)" -Package="$packageDir" -Rocket -VeryVerbose
  if ($LASTEXITCODE -ne 0) { Fail "BuildPlugin failed for UE $ver" }

  # Zip
  $verParts = $ver.Split('.')
  $zipPath = Join-Path $OutputDir ("$pluginName" + "_UE" + $verParts[0] + "_" + $verParts[1] + "_Fab.zip")
  if (Test-Path -Path $zipPath) { Remove-Item -Force -Path $zipPath }
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  [System.IO.Compression.ZipFile]::CreateFromDirectory($packageDir, $zipPath)

  Write-Host "Packaged: $zipPath"
}

Write-Host "Done."
