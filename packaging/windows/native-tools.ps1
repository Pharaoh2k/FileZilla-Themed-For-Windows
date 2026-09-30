# Wait explicitly for native tools, including GUI-subsystem MSYS2 builds.
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $Program
    $process.StartInfo.Arguments = ($Arguments | ForEach-Object {
        '"' + ([regex]::Replace($_, '(\\+)(?="|$)', '$1$1')).Replace('"', '\"') + '"'
    }) -join ' '
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    try {
        [void]$process.Start()
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $stdout = $stdoutTask.Result
        $stderr = $stderrTask.Result
        if ($process.ExitCode -ne 0) {
            throw "$Program failed ($($process.ExitCode)):`n$stdout`n$stderr"
        }
        if ($stderr) { Write-Host $stderr }
        if ($stdout) { return $stdout.TrimEnd("`r", "`n") -split '\r?\n' }
    } finally { $process.Dispose() }
}
