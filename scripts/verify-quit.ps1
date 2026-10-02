# Closing the application ends the process (S19).
#
# Launches DAW IA on a throwaway project, asks its window to close the way the
# close button does (WM_CLOSE), and checks that the process is gone within
# 3 seconds. Twice: once as it is, once with --quit-stall, a teardown that
# hangs on purpose where a real one would, to see the deadline hold.
#
#   powershell -File scripts/verify-quit.ps1 -Exe "build\windows-msvc-release\core\app\daw_app_artefacts\Release\DAW IA.exe"
#
# Never on the person's own project: every run gets its own folder in %TEMP%.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$LimitMs = 3000,
    [int]$StartupSeconds = 8
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

$root = Join-Path $env:TEMP ("daw-verify-quit-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$failed = 0

foreach ($case in @(@{ Name = "close"; Extra = "" }, @{ Name = "close, teardown stalled"; Extra = "--quit-stall" })) {
    $project = Join-Path $root ($case.Extra.TrimStart("-") + "p.dawproj")
    New-Item -ItemType Directory -Force $root | Out-Null

    $process = Start-Process -FilePath $Exe -ArgumentList "--no-copilot --project `"$project`" $($case.Extra)" -PassThru
    Start-Sleep -Seconds $StartupSeconds
    $process.Refresh()
    if ($process.HasExited -or $process.MainWindowHandle -eq [IntPtr]::Zero) {
        Write-Output "FAIL $($case.Name): no window after $StartupSeconds s"
        $failed++
        continue
    }

    $clock = [Diagnostics.Stopwatch]::StartNew()
    [QuitWindow]::PostMessage($process.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $gone = $process.WaitForExit($LimitMs)
    $elapsed = $clock.ElapsedMilliseconds

    if ($gone) {
        Write-Output "ok   $($case.Name): process gone in $elapsed ms"
    } else {
        Write-Output "FAIL $($case.Name): process alive after $LimitMs ms (pid $($process.Id)), killed"
        $process.Kill()
        $process.WaitForExit()
        $failed++
    }
}

Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
if ($failed -gt 0) { exit 1 }
Write-Output "verify-quit: passed"
