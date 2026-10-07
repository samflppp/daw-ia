# The push-to-talk test set, spoken by Windows' own voices (S25).
#
#   powershell -File scripts/voix/synthese.ps1
#
# Reads services/tests/voix/phrases.tsv and writes, for each phrase with a
# voice, services/tests/voix/synthese/<id>.wav: 16 kHz, mono, 16 bits. The
# French voices (Paul, Julie, Hortense) are Windows' OneCore voices; Zira, the
# English one, is the desktop voice. Phrases without a voice (silence, noise)
# are made by services/tools/voix_essai.py.
#
# Synthetic voices are clean, unaccented, and say English words the French
# way: what they give is an optimistic error rate, never the founder's.
param(
    [string]$Phrases = (Join-Path $PSScriptRoot "..\..\services\tests\voix\phrases.tsv"),
    [string]$Out = (Join-Path $PSScriptRoot "..\..\services\tests\voix\synthese")
)

Add-Type -AssemblyName System.Speech
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.SpeechSynthesis.SpeechSynthesizer, Windows.Media.SpeechSynthesis, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime]
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Await($operation, $type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
    $task.Wait() | Out-Null
    $task.Result
}

$oneCore = New-Object Windows.Media.SpeechSynthesis.SpeechSynthesizer
$french = [Windows.Media.SpeechSynthesis.SpeechSynthesizer]::AllVoices | Where-Object { $_.Language -eq 'fr-FR' }
$desktop = New-Object System.Speech.Synthesis.SpeechSynthesizer
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)

New-Item -ItemType Directory -Force $Out | Out-Null
$written = 0
foreach ($line in Get-Content -Encoding UTF8 $Phrases) {
    if (-not $line.Trim() -or $line.StartsWith('#')) { continue }
    $id, $category, $voice, $text = $line.Split("`t", 4)
    $path = Join-Path $Out "$id.wav"
    if ($voice -eq 'aucune') { continue }
    if ($voice -eq 'Zira') {
        $desktop.SelectVoice('Microsoft Zira Desktop')
        $desktop.SetOutputToWaveFile($path, $format)
        $desktop.Speak($text)
        $desktop.SetOutputToNull()
    } else {
        $oneCore.Voice = $french | Where-Object { $_.DisplayName -like "*$voice*" } | Select-Object -First 1
        if (-not $oneCore.Voice) { throw "voix introuvable : $voice" }
        $result = Await ($oneCore.SynthesizeTextToStreamAsync($text)) ([Windows.Media.SpeechSynthesis.SpeechSynthesisStream])
        $reader = New-Object Windows.Storage.Streams.DataReader($result.GetInputStreamAt(0))
        $size = [uint32]$result.Size
        $null = Await ($reader.LoadAsync($size)) ([uint32])
        $bytes = New-Object byte[] $size
        $reader.ReadBytes($bytes)
        [IO.File]::WriteAllBytes($path, $bytes)
    }
    $written++
}
Write-Output "synthese: $written phrases dans $Out"
