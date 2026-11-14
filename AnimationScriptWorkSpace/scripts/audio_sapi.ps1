param(
    [Parameter(Mandatory=$true)][string]$Manifest,
    [string]$Voice = 'Microsoft Yaoyao',
    [switch]$AuditionsOnly
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$taskSpeechSource = @'
using System;
using System.Collections.Generic;
using System.Speech.Synthesis;
using System.Speech.AudioFormat;
using System.IO;
public class HlsSpeechCue {
    public double start;
    public int position;
    public int length;
    public string text;
}
public static class HlsSpeechRenderer {
    public static List<HlsSpeechCue> Render(string text, string voice, int rate, string path) {
        var cues = new List<HlsSpeechCue>();
        using (var synth = new SpeechSynthesizer()) {
            synth.SelectVoice(voice);
            synth.Rate = rate;
            synth.Volume = 100;
            synth.SpeakProgress += delegate(object sender, SpeakProgressEventArgs e) {
                cues.Add(new HlsSpeechCue {start=e.AudioPosition.TotalSeconds,position=e.CharacterPosition,length=e.CharacterCount,text=e.Text});
            };
            // Keep the voice's native 16 kHz output here. Several OneCore
            // voices report invalid AudioPosition values when SAPI performs
            // output resampling; Python resamples the finished PCM instead.
            synth.SetOutputToWaveFile(path, new SpeechAudioFormatInfo(16000, AudioBitsPerSample.Sixteen, AudioChannel.Mono));
            synth.Speak(text);
            synth.SetOutputToNull();
        }
        return cues;
    }
}
'@
$taskReferences = @([System.Speech.Synthesis.SpeechSynthesizer].Assembly.Location)
if (Test-Path -LiteralPath (Join-Path $PSHOME 'ref/System.Runtime.dll')) {
    $taskReferences += @('System.Runtime.dll','System.Collections.dll','System.ComponentModel.Primitives.dll') | ForEach-Object { Join-Path $PSHOME ('ref/'+$_) }
}
Add-Type -TypeDefinition $taskSpeechSource -ReferencedAssemblies $taskReferences
$taskData = Get-Content -LiteralPath $Manifest -Raw -Encoding utf8 | ConvertFrom-Json
$taskOutput = Split-Path -Parent $PSScriptRoot
$taskRoot = Join-Path $taskOutput 'assets/audio'
New-Item -ItemType Directory -Path $taskRoot -Force | Out-Null
$taskAuditions = Join-Path $taskRoot 'auditions'
New-Item -ItemType Directory -Path $taskAuditions -Force | Out-Null
$taskAuditVoices = @('Microsoft Huihui','Microsoft Kangkang','Microsoft Yaoyao')
$taskSample = $taskData.films[0].cues[0].spoken
foreach ($taskAuditVoice in $taskAuditVoices) {
    $taskSafeName = $taskAuditVoice.Replace(' ','-')
    $taskAuditPath = Join-Path $taskAuditions ($taskSafeName+'.wav')
    if (-not (Test-Path -LiteralPath $taskAuditPath)) {
        [HlsSpeechRenderer]::Render($taskSample,$taskAuditVoice,0,$taskAuditPath) | Out-Null
    }
}
if ($AuditionsOnly) { exit 0 }
foreach ($taskFilm in $taskData.films) {
    foreach ($taskCue in $taskFilm.cues) {
        $taskPath = Join-Path $taskRoot ('raw/'+$taskCue.id+'.wav')
        New-Item -ItemType Directory -Path (Split-Path -Parent $taskPath) -Force | Out-Null
        $taskRate = [int]$taskCue.rate
        $taskProgress = [HlsSpeechRenderer]::Render($taskCue.spoken,$Voice,$taskRate,$taskPath)
        $taskCueMeta = [ordered]@{id=$taskCue.id;voice=$Voice;rate=$taskRate;spoken=$taskCue.spoken;events=@($taskProgress)}
        $taskCueMeta | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath ($taskPath+'.json') -Encoding utf8
        Write-Output ('Synthesized '+$taskCue.id)
    }
}
