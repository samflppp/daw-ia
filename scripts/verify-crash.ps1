# A crash, and the launch after it (S26).
#
# Three runs of DAW IA, one --reglages folder for all three, so the marker the
# first leaves is the one the second reads:
#   1. --verify-plantage on project A: the song of the list's first steps,
#      its state kept (etat.json), then a crash on purpose;
#   2. --verify-reprise on project B: what the crash left (marker, report,
#      minidump, daw.log), and "Rouvrir" naming A;
#   3. --verify-reopen on A, against etat.json: the same state, to the byte,
#      and the same history depth.
# Prints each run's result line; exits 1 if any did not pass.
#
#   powershell -File scripts/verify-crash.ps1 -Exe "build\windows-msvc-release\core\app\daw_app_artefacts\Release\DAW IA.exe"
#
# Never on the person's own project, layout or settings: everything lives in a
# folder of %TEMP% (or -Root), as every verification.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [string]$Root = (Join-Path $env:TEMP ("daw-crash-" + [guid]::NewGuid().ToString("N").Substring(0, 8))),
    [int]$Minutes = 10
)

$others = Get-Process -Name "DAW IA" -ErrorAction SilentlyContinue
if ($others) {
    Write-Output "another DAW IA is running (pid $($others.Id -join ', ')): close it first"
    exit 1
}

New-Item -ItemType Directory -Force $Root | Out-Null
# Only what this script's runs start is counted: a service left by something
# else is not this crash's.
$started = Get-Date
$settings = Join-Path $Root "reglages"
$projectA = Join-Path $Root "A.dawproj"
$projectB = Join-Path $Root "B.dawproj"
$crash = Join-Path $Root "plantage"
$layout = Join-Path $Root "layout.xml"

function Run([string]$Arguments) {
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit($Minutes * 60000)) {
        $process.Kill()
        return "did not finish in $Minutes minutes"
    }
    return "exit code $($process.ExitCode)"
}

function Result([string]$Folder) {
    $report = Join-Path $Folder "rapport.md"
    if (-not (Test-Path $report)) { return "no report" }
    $line = Select-String -Path $report -Encoding UTF8 -Pattern "v.rifications pass.es" | Select-Object -Last 1
    if ($line) { return $line.Line } else { return "no result line" }
}

$failed = $false

$ended = Run "--verify-plantage `"$crash`" --project `"$projectA`" --layout `"$layout`" --reglages `"$settings`""
$line = Result $crash
Write-Output "1. plantage ($ended) : $line"
# A crash ends with the exception's code, never 0.
if ($ended -eq "exit code 0" -or $line -notmatch " 0 en .chec, puis le plantage") { $failed = $true }

# The services the crashed DAW started die with it (ProcessTree.h): none of
# them may be left, the copilot's Python included.
Start-Sleep -Seconds 3
$left = @(Get-CimInstance Win32_Process -Filter "Name='python.exe' OR Name='uv.exe'" |
    Where-Object { ($_.CommandLine -like "*daw-services*" -or $_.CommandLine -like "*daw_services*") -and
        $_.CreationDate -gt $started })
Write-Output "   services still alive after the crash : $($left.Count)"
if ($left.Count -ne 0) {
    $failed = $true
    $left | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
}

$recovery = Join-Path $Root "reprise"
$ended = Run "--verify-reprise `"$recovery`" --project `"$projectB`" --layout `"$layout`" --reglages `"$settings`""
$line = Result $recovery
Write-Output "2. reprise ($ended) : $line"
if ($line -notmatch " 0 en .chec") { $failed = $true }

# --verify-reopen reads etat.json in its folder: the crash run's.
$ended = Run "--verify-reopen `"$crash`" --project `"$projectA`" --layout `"$layout`" --reglages `"$settings`""
$report = Join-Path $crash "rapport.md"
$line = Result $crash
Write-Output "3. rouvert ($ended) : $line"
if ($line -notmatch " 0 en .chec") { $failed = $true }

Write-Output "dossier : $Root"
if ($failed) { exit 1 }
