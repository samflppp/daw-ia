# Closing the application ends the process (S19, S21).
#
# Launches DAW IA on a throwaway project, asks its window to close the way the
# close button does (WM_CLOSE), and checks that the process is gone within
# 3 seconds and that daw.log does not say "quit: stuck": a process ended by the
# watchdog of S19 is gone in time, but its teardown did hang, and that is the
# bug, not its guard.
#
# The cases, each run -Repeat times:
#   close                    without the copilot
#   close, copilot           with the copilot's process (needs the key)
#   close, playing, copilot  the song playing when the window closes
# then once with --quit-stall, a teardown that hangs on purpose, to see the
# deadline hold (there, "quit: stuck" is expected).
#
#   powershell -File scripts/verify-quit.ps1 -Exe "build\windows-msvc-release\core\app\daw_app_artefacts\Release\DAW IA.exe" -Repeat 20
#
# Never on the person's own project or layout: every run gets its own folder in
# %TEMP%, and --layout a file in it.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$LimitMs = 3000,
    [int]$StartupSeconds = 8,
    [int]$Repeat = 1
)

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class QuitWindow {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@

$others = Get-Process -Name "DAW IA" -ErrorAction SilentlyContinue
if ($others) {
    Write-Output "another DAW IA is running (pid $($others.Id -join ', ')): close it first"
    exit 1
}

$log = Join-Path $env:APPDATA "DAW IA\daw.log"
function StuckCount {
    if (-not (Test-Path $log)) { return 0 }
    return @(Select-String -Path $log -SimpleMatch "quit: stuck").Count
}

$hasKey = [bool]$env:DAW_IA_ANTHROPIC_API_KEY
$root = Join-Path $env:TEMP ("daw-verify-quit-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $root | Out-Null

$cases = @(@{ Name = "close"; Extra = "--no-copilot"; Play = $false; Stall = $false })
if ($hasKey) {
    $cases += @{ Name = "close, copilot"; Extra = ""; Play = $false; Stall = $false }
    $cases += @{ Name = "close, playing, copilot"; Extra = ""; Play = $true; Stall = $false }
} else {
    Write-Output "no DAW_IA_ANTHROPIC_API_KEY: the copilot cases are skipped"
}

$runs = @()
foreach ($case in $cases) { for ($i = 1; $i -le $Repeat; $i++) { $runs += $case } }
$runs += @{ Name = "close, teardown stalled"; Extra = "--no-copilot --quit-stall"; Play = $false; Stall = $true }

$failed = 0
$index = 0
foreach ($case in $runs) {
    $index++
    $folder = Join-Path $root ("run" + $index)
    New-Item -ItemType Directory -Force $folder | Out-Null
    $arguments = "$($case.Extra) --project `"$(Join-Path $folder 'p.dawproj')`" --layout `"$(Join-Path $folder 'layout.xml')`""

    $stuckBefore = StuckCount
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru
    Start-Sleep -Seconds $StartupSeconds
    $process.Refresh()
    if ($process.HasExited -or $process.MainWindowHandle -eq [IntPtr]::Zero) {
        Write-Output "FAIL $($case.Name) #${index}: no window after $StartupSeconds s"
        $failed++
        continue
    }

    if ($case.Play) {
        # Space, the way the keyboard sends it: the song plays when the window closes.
        [QuitWindow]::PostMessage($process.MainWindowHandle, 0x0100, [IntPtr]0x20, [IntPtr]::Zero) | Out-Null
        [QuitWindow]::PostMessage($process.MainWindowHandle, 0x0101, [IntPtr]0x20, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Seconds 2
    }

    $clock = [Diagnostics.Stopwatch]::StartNew()
    [QuitWindow]::PostMessage($process.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $gone = $process.WaitForExit($LimitMs)
    $elapsed = $clock.ElapsedMilliseconds
    $stuck = (StuckCount) - $stuckBefore

    if (-not $gone) {
        Write-Output "FAIL $($case.Name) #${index}: process alive after $LimitMs ms (pid $($process.Id)), killed"
        $process.Kill()
        $process.WaitForExit()
        $failed++
    } elseif ($stuck -gt 0 -and -not $case.Stall) {
        $line = (Select-String -Path $log -SimpleMatch "quit: stuck" | Select-Object -Last 1).Line
        Write-Output "FAIL $($case.Name) #${index}: gone in $elapsed ms, but by the watchdog: $line"
        $failed++
    } elseif ($stuck -eq 0 -and $case.Stall) {
        Write-Output "FAIL $($case.Name) #${index}: gone in $elapsed ms, and the watchdog did not say where it stalled"
        $failed++
    } else {
        Write-Output "ok   $($case.Name) #${index}: process gone in $elapsed ms"
    }
}

Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
if ($failed -gt 0) {
    Write-Output "verify-quit: $failed failed out of $($runs.Count)"
    exit 1
}
Write-Output "verify-quit: passed, $($runs.Count) closes"
