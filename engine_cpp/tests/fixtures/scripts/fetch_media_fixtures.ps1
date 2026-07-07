# Downloads real media test fixtures for ebbackup tests.
# Usage: .\fetch_media_fixtures.ps1 [-UseMirror]

param(
    [switch]$UseMirror
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$MediaRoot = Join-Path (Split-Path -Parent $ScriptDir) "real_world\media"

$Sources = @{
    "images/earth.jpg" = @{
        Primary = "https://raw.githubusercontent.com/python-pillow/Pillow/main/Tests/images/hopper.jpg"
        Mirror  = "https://cdn.jsdelivr.net/gh/python-pillow/Pillow@main/Tests/images/hopper.jpg"
        License = "Pillow test image (HPND, test use)"
    }
    "images/demo.webp" = @{
        Primary = "https://www.gstatic.com/webp/gallery/1.webp"
        Mirror  = "https://cdn.jsdelivr.net/gh/webmproject/webp@main/examples/1.webp"
        License = "Google WebP gallery / webmproject examples"
    }
    "images/demo_alt.webp" = @{
        Primary = "https://www.gstatic.com/webp/gallery/2.webp"
        Mirror  = "https://cdn.jsdelivr.net/gh/webmproject/webp@main/examples/2.webp"
        License = "Google WebP gallery / webmproject examples (incremental swap)"
    }
    "video/sample.mp4" = @{
        Primary = "https://raw.githubusercontent.com/mdn/learning-area/master/html/multimedia-and-embedding/video-and-audio-content/rabbit320.mp4"
        Mirror  = "https://cdn.jsdelivr.net/gh/mdn/learning-area@master/html/multimedia-and-embedding/video-and-audio-content/rabbit320.mp4"
        License = "MDN learning-area sample (CC0)"
    }
    "nested/l0/l1/l2/badge.gif" = @{
        Primary = "https://raw.githubusercontent.com/python-pillow/Pillow/main/Tests/images/hopper.gif"
        Mirror  = "https://cdn.jsdelivr.net/gh/python-pillow/Pillow@main/Tests/images/hopper.gif"
        License = "Pillow test image (HPND, test use)"
    }
    "binaries/sample.exe" = @{
        Primary = "https://live.sysinternals.com/Sigcheck.exe"
        Mirror  = "https://live.sysinternals.com/Sigcheck.exe"
        License = "Microsoft Sysinternals Sigcheck (test binary; https://learn.microsoft.com/sysinternals)"
    }
}

function Download-File($Url, $Dest) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Dest) | Out-Null
    Write-Host "  GET $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
}

function Try-Download($Entry, $RelPath) {
    $dest = Join-Path $MediaRoot ($RelPath -replace '/', '\')
    $urls = if ($UseMirror) { @($Entry.Mirror, $Entry.Primary) } else { @($Entry.Primary, $Entry.Mirror) }
    foreach ($url in $urls) {
        if ([string]::IsNullOrWhiteSpace($url)) { continue }
        try {
            Download-File $url $dest
            return $true
        } catch {
            Write-Warning "Failed: $url - $_"
        }
    }
    return $false
}

Write-Host "Media fixture root: $MediaRoot"
if (Test-Path $MediaRoot) {
    Remove-Item -Recurse -Force $MediaRoot
}
New-Item -ItemType Directory -Force -Path $MediaRoot | Out-Null

$attribution = @()
foreach ($kv in ($Sources.GetEnumerator() | Sort-Object { $_.Key })) {
    $rel = $kv.Key
    $entry = $kv.Value
    Write-Host "Fetching $rel ..."
    if (-not (Try-Download $entry $rel)) {
        throw "All URLs failed for $rel"
    }
    $attribution += "- ``$rel``: $($entry.License)"
}

# Deep nested video copy
$videoSrc = Join-Path $MediaRoot "video\sample.mp4"
$clipDest = Join-Path $MediaRoot "nested\l0\l1\l2\clip.mp4"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $clipDest) | Out-Null
Copy-Item $videoSrc $clipDest -Force
$attribution += "- ``nested/l0/l1/l2/clip.mp4``: copy of video/sample.mp4 (same bytes)"

# ZIP archive (local)
Write-Host "Building archives/sample.zip ..."
$zipStage = Join-Path $env:TEMP "eb_media_zip_stage"
if (Test-Path $zipStage) { Remove-Item -Recurse -Force $zipStage }
New-Item -ItemType Directory -Force -Path $zipStage | Out-Null
@'
ebbackup zip fixture
'@ | Set-Content -Encoding UTF8 (Join-Path $zipStage "readme.txt")
@'
{"in_archive":true}
'@ | Set-Content -Encoding UTF8 (Join-Path $zipStage "inner.json")
$zipOut = Join-Path $MediaRoot "archives\sample.zip"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $zipOut) | Out-Null
if (Test-Path $zipOut) { Remove-Item -Force $zipOut }
Compress-Archive -Path (Join-Path $zipStage "*") -DestinationPath $zipOut -Force
$attribution += "- ``archives/sample.zip``: generated locally (Compress-Archive)"

# tar.gz in nested path
Write-Host "Building nested/l0/l1/payload.tar.gz ..."
$tarStage = Join-Path $env:TEMP "eb_media_tar_stage"
if (Test-Path $tarStage) { Remove-Item -Recurse -Force $tarStage }
New-Item -ItemType Directory -Force -Path $tarStage | Out-Null
'tar payload' | Set-Content -Encoding UTF8 (Join-Path $tarStage "payload.txt")
$tarGz = Join-Path $MediaRoot "nested\l0\l1\payload.tar.gz"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $tarGz) | Out-Null
if (Test-Path $tarGz) { Remove-Item -Force $tarGz }
tar -czf $tarGz -C $tarStage .
$attribution += "- ``nested/l0/l1/payload.tar.gz``: generated locally (tar -czf)"

@'
{"fixture":"media","version":1,"formats":["jpg","webp","mp4","gif","zip","tar.gz","exe"]}
'@ | Set-Content -Encoding UTF8 (Join-Path $MediaRoot "meta.json")

$attrText = @"
# Media fixture attribution

Downloaded for ebbackup integration tests. Refresh with:
``engine_cpp/tests/fixtures/scripts/fetch_media_fixtures.ps1`` (add ``-UseMirror`` for jsDelivr-first)

Sources:

$($attribution -join "`n")

Generated: $(Get-Date -Format 'yyyy-MM-dd')
"@
Set-Content -Encoding UTF8 (Join-Path $MediaRoot "ATTRIBUTION.md") $attrText

$files = Get-ChildItem -Path $MediaRoot -Recurse -File |
    Where-Object { $_.Name -ne 'media_manifest.json' } |
    Sort-Object FullName
$entries = foreach ($f in $files) {
    $rel = $f.FullName.Substring($MediaRoot.Length + 1).Replace('\', '/')
    $hash = (Get-FileHash -Path $f.FullName -Algorithm SHA256).Hash.ToLower()
    [PSCustomObject]@{ path = $rel; sha256 = $hash }
}
($entries | ConvertTo-Json -Compress) | Set-Content -Encoding UTF8 (Join-Path $MediaRoot "media_manifest.json")

$totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host "Done: $($files.Count) files, $([math]::Round($totalBytes/1MB, 2)) MB"
