function Resolve-RepoPathSafe {
    param(
        [string]$RepoPath,
        [string]$PathInput
    )

    if ([string]::IsNullOrWhiteSpace($PathInput)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($PathInput)) {
        return $PathInput
    }
    return (Join-Path -Path $RepoPath -ChildPath $PathInput)
}

function New-RunId {
    param([string]$Prefix = "run")

    $ts = Get-Date -Format "yyyyMMdd_HHmmss_fff"
    $pidPart = "p$PID"
    $rand = [System.Guid]::NewGuid().ToString("N").Substring(0, 8)
    return "$Prefix`_$ts`_$pidPart`_$rand"
}

function New-RunContext {
    param(
        [string]$RepoPath,
        [string]$OutputDir = "",
        [switch]$AutoOutputDir,
        [string]$AutoRoot = "artifacts\\runs",
        [string]$Prefix = "run"
    )

    if ($AutoOutputDir -and -not [string]::IsNullOrWhiteSpace($OutputDir)) {
        throw "Use either -OutputDir or -AutoOutputDir, not both"
    }

    $runId = ""
    $resolved = ""
    $scoped = $false

    if ($AutoOutputDir) {
        $runId = New-RunId -Prefix $Prefix
        $root = Resolve-RepoPathSafe -RepoPath $RepoPath -PathInput $AutoRoot
        if (-not (Test-Path -LiteralPath $root)) {
            New-Item -ItemType Directory -Path $root -Force | Out-Null
        }
        $resolved = Join-Path -Path $root -ChildPath $runId
        New-Item -ItemType Directory -Path $resolved -Force | Out-Null
        $scoped = $true
    } elseif (-not [string]::IsNullOrWhiteSpace($OutputDir)) {
        $resolved = Resolve-RepoPathSafe -RepoPath $RepoPath -PathInput $OutputDir
        if (-not (Test-Path -LiteralPath $resolved)) {
            New-Item -ItemType Directory -Path $resolved -Force | Out-Null
        }
        $scoped = $true
    }

    return [pscustomobject]@{
        OutputDir = $resolved
        Scoped = $scoped
        RunId = $runId
    }
}
