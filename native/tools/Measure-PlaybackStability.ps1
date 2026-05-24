param(
  [Parameter(Mandatory = $true)]
  [string]$ExePath,

  [int]$DurationSeconds = 14400,
  [int]$SampleSeconds = 60,
  [int]$MaxPrivateGrowthMb = 96,
  [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ExePath)) {
  throw "Executable not found: $ExePath"
}

if ($DurationSeconds -lt 1) {
  throw "DurationSeconds must be >= 1"
}

if ($SampleSeconds -lt 1) {
  throw "SampleSeconds must be >= 1"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
  $OutputDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "bottlemusic-stability"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$csvPath = Join-Path $OutputDirectory "playback-stability-$stamp.csv"
$jsonPath = Join-Path $OutputDirectory "playback-stability-$stamp.summary.json"

$process = Start-Process -FilePath $ExePath -PassThru -WindowStyle Hidden
$samples = New-Object System.Collections.Generic.List[object]
$failed = $false
$failure = ""
$startedAt = Get-Date

try {
  while ($true) {
    Start-Sleep -Seconds ([Math]::Min($SampleSeconds, [Math]::Max(1, $DurationSeconds)))
    $elapsed = [int]((Get-Date) - $startedAt).TotalSeconds

    try {
      $process.Refresh()
    } catch {
      $failed = $true
      $failure = "Process refresh failed: $($_.Exception.Message)"
      break
    }

    $sample = [pscustomobject]@{
      ElapsedSeconds = $elapsed
      Timestamp = (Get-Date).ToString("o")
      Id = $process.Id
      Responding = $process.Responding
      HasExited = $process.HasExited
      WorkingSetMb = [Math]::Round($process.WorkingSet64 / 1MB, 1)
      PrivateMb = [Math]::Round($process.PrivateMemorySize64 / 1MB, 1)
    }
    $samples.Add($sample)

    if ($process.HasExited) {
      $failed = $true
      $failure = "Process exited before stability window completed"
      break
    }
    if (-not $process.Responding) {
      $failed = $true
      $failure = "Process stopped responding"
      break
    }
    if ($elapsed -ge $DurationSeconds) {
      break
    }
  }
} finally {
  if ($samples.Count -gt 0) {
    $samples | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csvPath
  }
  if (-not $process.HasExited) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
  }
}

$firstPrivate = if ($samples.Count -gt 0) { $samples[0].PrivateMb } else { 0 }
$lastPrivate = if ($samples.Count -gt 0) { $samples[$samples.Count - 1].PrivateMb } else { 0 }
$privateGrowth = [Math]::Round($lastPrivate - $firstPrivate, 1)
if ($privateGrowth -gt $MaxPrivateGrowthMb) {
  $failed = $true
  $failure = "Private memory grew by $privateGrowth MB, over $MaxPrivateGrowthMb MB budget"
}

$summary = [pscustomobject]@{
  Passed = -not $failed
  Failure = $failure
  ExePath = (Resolve-Path -LiteralPath $ExePath).Path
  DurationSeconds = $DurationSeconds
  SampleSeconds = $SampleSeconds
  SampleCount = $samples.Count
  FirstPrivateMb = $firstPrivate
  LastPrivateMb = $lastPrivate
  PrivateGrowthMb = $privateGrowth
  CsvPath = $csvPath
}

$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $jsonPath
$summary

if ($failed) {
  exit 2
}
