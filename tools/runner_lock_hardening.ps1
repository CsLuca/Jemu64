function Test-LockErrorText {
    param([string]$Text)
    if ($null -eq $Text) {
        return $false
    }
    return ($Text -match "Impossibile accedere al file|Permission denied|utilizzato da un altro processo|cannot open output file|reopening")
}

function Stop-ExeIfRunning {
    param([string]$ExeName)
    try {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($ExeName)
        if ([string]::IsNullOrWhiteSpace($base)) {
            return
        }
        $procs = @(Get-Process -Name $base -ErrorAction SilentlyContinue)
        foreach ($p in $procs) {
            Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
        }
    }
    catch {
        # Best-effort cleanup only.
    }
}

function Convert-ToMsysPath {
    param([string]$WindowsPath)

    $normalized = $WindowsPath -replace '\\', '/'
    if ($normalized -match '^([A-Za-z]):/(.+)$') {
        $drive = $matches[1].ToLowerInvariant()
        $rest = $matches[2]
        return "/$drive/$rest"
    }
    return $normalized
}

function Invoke-WithRetry {
    param(
        [scriptblock]$Action,
        [int]$RetryCount = 3,
        [int]$RetryDelayMs = 250
    )

    $lastExit = 0
    for ($attempt = 0; $attempt -le $RetryCount; $attempt++) {
        try {
            & $Action
            $lastExit = $LASTEXITCODE
        }
        catch {
            $lastExit = 1
        }
        if ($lastExit -eq 0) {
            return $true
        }
        if ($attempt -lt $RetryCount) {
            Start-Sleep -Milliseconds ($RetryDelayMs * ($attempt + 1))
        }
    }
    $script:LASTEXITCODE = $lastExit
    return $false
}

function Build-Profile-Retry {
    param(
        [string]$Macro,
        [string]$OutFile,
        [string]$CompilerPath,
        [string]$RepoPath,
        [string]$SourceFile = "c64_11.cpp",
        [int]$RetryCount = 4
    )

    $sourcePath = if ([System.IO.Path]::IsPathRooted($SourceFile)) { $SourceFile } else { Join-Path -Path $RepoPath -ChildPath $SourceFile }
    $outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) { $OutFile } else { Join-Path -Path $RepoPath -ChildPath $OutFile }

    $msysShell = 'C:\msys64\msys2_shell.cmd'
    $preferUcrtShell = ($CompilerPath -ieq 'C:\msys64\ucrt64\bin\g++.exe' -and (Test-Path -LiteralPath $msysShell))

    for ($attempt = 0; $attempt -le $RetryCount; $attempt++) {
        Stop-ExeIfRunning -ExeName $OutFile
        $savedEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        try {
            if ($preferUcrtShell) {
                $sourceMsys = Convert-ToMsysPath -WindowsPath $sourcePath
                $outMsys = Convert-ToMsysPath -WindowsPath $outPath
                $cmd = "g++ -std=c++17 -O2 -DRUN_PROFILE=$Macro '$sourceMsys' -o '$outMsys'"
                $buildOutput = @(& $msysShell -ucrt64 -defterm -no-start -here -c $cmd 2>&1 | ForEach-Object { "$_" })
            }
            else {
                $buildOutput = @(& $CompilerPath -std=c++17 -O2 "-DRUN_PROFILE=$Macro" $sourcePath -o $outPath 2>&1 | ForEach-Object { "$_" })
            }
            $exitCode = $LASTEXITCODE
        }
        finally {
            $ErrorActionPreference = $savedEap
        }

        if ($exitCode -eq 0) {
            return 0
        }

        $txt = ($buildOutput | Out-String)
        if (Test-LockErrorText -Text $txt) {
            Stop-ExeIfRunning -ExeName $OutFile
        }

        if ($attempt -lt $RetryCount) {
            Start-Sleep -Milliseconds (250 * ($attempt + 1))
        }
    }

    return $LASTEXITCODE
}

function Invoke-BinaryWithLockRetry {
    param(
        [string]$ExePath,
        [int]$RetryCount = 3
    )

    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        for ($attempt = 0; $attempt -le $RetryCount; $attempt++) {
            try {
                $output = @(& $ExePath 2>&1 | ForEach-Object { "$_" })
            }
            catch {
                $output = @("$($_.Exception.Message)")
            }
            $exitCode = $LASTEXITCODE
            if ($null -eq $exitCode) {
                $exitCode = 1
            }
            if ($exitCode -eq 0) {
                return ,@($output, $exitCode)
            }
            $txt = ($output | Out-String)
            if (Test-LockErrorText -Text $txt) {
                Stop-ExeIfRunning -ExeName ([System.IO.Path]::GetFileName($ExePath))
                if ($attempt -lt $RetryCount) {
                    Start-Sleep -Milliseconds (200 * ($attempt + 1))
                    continue
                }
            }
            return ,@($output, $exitCode)
        }
    }
    finally {
        $ErrorActionPreference = $savedEap
    }
    return ,@(@(), 1)
}
