<#
.SYNOPSIS
    Link the UnrealMCP plugin (from this master repo) into a UE project via NTFS junction.

.DESCRIPTION
    Interactive: prompts for the UE project folder, validates it contains a *.uproject,
    creates Plugins\ if missing, and replaces any existing junction at Plugins\UnrealMCP
    with a fresh junction pointing to this repo's UnrealMCP\ source.

    Also adds 'Plugins/UnrealMCP/' to the project's .gitignore and .dvignore (idempotent).

    Run from anywhere - master plugin location is hardcoded.
#>

$ErrorActionPreference = 'Stop'

# Hardcoded master paths
$PluginName    = 'UnrealMCP'
$MasterRepo    = 'M:\Projects\unreal-engine-mcp'
$MasterPlugin  = Join-Path $MasterRepo $PluginName

if (-not (Test-Path -LiteralPath $MasterPlugin -PathType Container)) {
    throw "Master plugin folder not found: $MasterPlugin"
}

$projectPath = Read-Host -Prompt 'Enter UE project path (folder containing .uproject)'
$projectPath = $projectPath.Trim('"').Trim()
if (-not (Test-Path -LiteralPath $projectPath -PathType Container)) {
    throw "Project folder does not exist: $projectPath"
}

$uproject = Get-ChildItem -LiteralPath $projectPath -Filter '*.uproject' -File -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $uproject) {
    throw "No .uproject found in: $projectPath"
}

$pluginsDir = Join-Path $projectPath 'Plugins'
if (-not (Test-Path -LiteralPath $pluginsDir)) {
    New-Item -ItemType Directory -Path $pluginsDir | Out-Null
    Write-Host "Created: $pluginsDir"
}

$junctionPath = Join-Path $pluginsDir $PluginName

if (Test-Path -LiteralPath $junctionPath) {
    $item = Get-Item -LiteralPath $junctionPath -Force
    $isReparse = ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq [IO.FileAttributes]::ReparsePoint
    if (-not $isReparse) {
        throw "A real directory already exists at $junctionPath. Rename or delete it manually before re-running this script (refusing to delete to avoid data loss)."
    }
    $confirm = Read-Host -Prompt "Existing junction at $junctionPath will be replaced. Continue? [y/N]"
    if ($confirm -notmatch '^(y|Y|yes)$') {
        Write-Host 'Aborted.'
        return
    }
    # Remove junction (use cmd rmdir to detach without following the link)
    & cmd.exe /c "rmdir `"$junctionPath`"" | Out-Null
    if (Test-Path -LiteralPath $junctionPath) {
        throw "Failed to remove existing junction: $junctionPath"
    }
}

New-Item -ItemType Junction -Path $junctionPath -Target $MasterPlugin | Out-Null
Write-Host "Junction created: $junctionPath  ->  $MasterPlugin"

# Idempotent ignore wiring
$ignoreLine = 'Plugins/UnrealMCP/'
foreach ($file in @('.gitignore', '.dvignore')) {
    $path = Join-Path $projectPath $file
    if (-not (Test-Path -LiteralPath $path)) {
        Set-Content -LiteralPath $path -Value $ignoreLine -Encoding utf8
        Write-Host "Created $file with ignore entry."
        continue
    }
    if (Select-String -LiteralPath $path -SimpleMatch -Pattern $ignoreLine -Quiet) {
        Write-Host "$file already ignores $ignoreLine"
    } else {
        Add-Content -LiteralPath $path -Value $ignoreLine
        Write-Host "Appended ignore entry to $file"
    }
}

Write-Host ''
Write-Host 'Done.'
Write-Host "  Project: $($uproject.FullName)"
Write-Host "  Plugin junction: $junctionPath"
Write-Host "  Update workflow: cd $MasterRepo ; git pull  (then rebuild this UE project)"
