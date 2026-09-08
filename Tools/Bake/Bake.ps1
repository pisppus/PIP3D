param(
  [switch]$Clean,
  [switch]$NoRun,
  [int]$Jobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = "Stop"
if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::InputEncoding = [System.Text.Encoding]::UTF8
$null = chcp 65001

$ESC = [char]27
$TAG = "$ESC[36m[Pip3D]$ESC[0m"
$OK = "$ESC[32m[+]$ESC[0m"
$WARN = "$ESC[33m[!]$ESC[0m"

$dir = (Resolve-Path (Split-Path $PSScriptRoot -Parent)).Path
while ($true) {
  if (Test-Path -LiteralPath (Join-Path $dir "platformio.ini")) { break }
  $parent = Split-Path $dir -Parent
  if ($parent -eq $dir) { $dir = $PSScriptRoot; break }
  $dir = $parent
}
$root = $dir

$outDir = Join-Path $root "Tools\Bake\Output"
if (-not $env:PIP3D_BAKE_OUT) {
  $env:PIP3D_BAKE_OUT = "Tools/Bake/Output"
}
$sceneName = if ($env:PIP3D_BAKE_SCENE -and $env:PIP3D_BAKE_SCENE.Trim() -ne "") { $env:PIP3D_BAKE_SCENE } else { "Scene" }
$bakedHpp = Join-Path $root "src\Lighting\Baked$sceneName.hpp"
if ($sceneName -eq "Scene" -and -not (Test-Path -LiteralPath $bakedHpp)) {
  $bakedHpp = Join-Path $root "src\Lighting\BakedScene.hpp"
}

function Test-SafeBakeOutDir([string]$p, [string]$r) {
  if ([string]::IsNullOrWhiteSpace($p)) { return $false }
  try {
    $fp = [IO.Path]::GetFullPath($p)
    $fr = [IO.Path]::GetFullPath($r)
    if ($fp.Length -le $fr.Length) { return $false }
    return $fp.StartsWith($fr, [StringComparison]::OrdinalIgnoreCase)
  } catch { return $false }
}
if (-not (Test-SafeBakeOutDir $outDir $root)) { throw "Refusing to clean unsafe outDir: $outDir" }

$buildDir = Join-Path $root "Tools\Bake\Build"
$stampDir = Join-Path $buildDir ".vsenv"
$envCache = Join-Path $stampDir "vsenv.ps1"
$bakeCachePath = Join-Path $buildDir "bake_cache.json"
$pioCachePath = Join-Path $root ".pio\pip3d_assetdb.json"

function Get-FileHashSafe([string]$path) {
  try {
    if (-not (Test-Path -LiteralPath $path)) { return "MISSING" }
    $h = Get-FileHash -LiteralPath $path -Algorithm SHA256 -ErrorAction Stop
    return $h.Hash.ToLowerInvariant()
  } catch { return "ERROR" }
}

function Get-ScreenDefines {
  $w = "480"; $h = "320"; $band = "4"
  $pioIni = Join-Path $root "platformio.ini"
  if (Test-Path -LiteralPath $pioIni) {
    $flags = Get-Content -LiteralPath $pioIni -Raw -ErrorAction SilentlyContinue
    if ($flags -match "-DPIP3D_SCREEN_WIDTH\s*=\s*(\d+)") { $w = $Matches[1] }
    if ($flags -match "-DPIP3D_SCREEN_HEIGHT\s*=\s*(\d+)") { $h = $Matches[1] }
    if ($flags -match "-DPIP3D_SCREEN_BAND_COUNT\s*=\s*(\d+)") { $band = $Matches[1] }
  }
  return @($w, $h, $band)
}

function Get-BakeFingerprint([string]$rootPath) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $files = @()
  $bakeSrcDir = Join-Path $rootPath "Tools\Bake\Source"
  if (Test-Path -LiteralPath $bakeSrcDir) {
    $files += Get-ChildItem -LiteralPath $bakeSrcDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @(".cpp",".hpp",".comp",".py") -or $_.Name -eq "CMakeLists.txt" }
  }
  $files += Get-Item -LiteralPath (Join-Path $rootPath "Tools\Bake\Bake.ps1") -ErrorAction SilentlyContinue
  $srcDir = Join-Path $rootPath "src"
  if (Test-Path -LiteralPath $srcDir) {
    $files += Get-ChildItem -LiteralPath $srcDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @(".cpp",".hpp",".c",".h") -and $_.FullName -notlike "*\Lighting\Baked*.hpp" }
  }

  $engineDir = Join-Path $rootPath "lib\Pip3D"
  if (Test-Path -LiteralPath $engineDir) {
    $files += Get-ChildItem -LiteralPath $engineDir -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @(".cpp",".hpp",".c",".h") }
  }
  $files = $files | Sort-Object -Property FullName -Unique
  foreach ($f in $files) {
    if (-not $f -or -not (Test-Path -LiteralPath $f.FullName)) { continue }
    $fh = Get-FileHashSafe $f.FullName
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($fh + "`0" + $f.FullName + "`0")
    $null = $sha.TransformBlock($bytes, 0, $bytes.Length, $null, 0)
  }
  $envLines = Get-ChildItem Env:PIP3D_BAKE_* -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }
  $envStr = ($envLines -join "|")
  $screen = Get-ScreenDefines
  $envStr += "|SCREEN_W=$($screen[0])|SCREEN_H=$($screen[1])|BAND=$($screen[2])"
  $envBytes = [System.Text.Encoding]::UTF8.GetBytes($envStr)
  $null = $sha.TransformBlock($envBytes, 0, $envBytes.Length, $null, 0)
  $sha.TransformFinalBlock([byte[]]::new(0), 0, 0) | Out-Null
  $hash = [BitConverter]::ToString($sha.Hash).Replace("-", "").ToLowerInvariant()
  $sha.Dispose()
  return $hash
}

if ($Clean) {
  if (Test-Path -LiteralPath $buildDir) { Remove-Item -LiteralPath $buildDir -Recurse -Force -ErrorAction SilentlyContinue }
  if (Test-Path -LiteralPath $bakeCachePath) { Remove-Item -LiteralPath $bakeCachePath -Force -ErrorAction SilentlyContinue }
  if (Test-Path -LiteralPath $pioCachePath) {
    try {
      $j = Get-Content -LiteralPath $pioCachePath -Raw -ErrorAction Stop | ConvertFrom-Json
      if ($j.entries -and $j.entries."bake:scene") {
        $j.entries.PSObject.Properties.Remove("bake:scene")
        $tmp = "$pioCachePath.tmp"
        [IO.File]::WriteAllText($tmp, ($j | ConvertTo-Json -Depth 10))
        Move-Item -LiteralPath $tmp -Destination $pioCachePath -Force -ErrorAction SilentlyContinue
      }
    } catch {}
  }
  if (Test-Path -LiteralPath $outDir) {
    if (Test-SafeBakeOutDir $outDir $root) {
      Get-ChildItem -LiteralPath $outDir -Force -ErrorAction SilentlyContinue | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
    }
  }
  if (Test-Path -LiteralPath $bakedHpp) { Remove-Item -LiteralPath $bakedHpp -Force -ErrorAction SilentlyContinue }
  Write-Host "$OK Clean complete."
  if ($NoRun) { return }
}

$skipBuildAndRun = $false
if (-not $Clean -and -not $NoRun -and (Test-Path -LiteralPath $bakedHpp) -and (Test-Path -LiteralPath $bakeCachePath)) {
  $cache = $null
  try { $cache = Get-Content -LiteralPath $bakeCachePath -Raw -ErrorAction Stop | ConvertFrom-Json } catch { $cache = $null }
  if ($cache -and $cache.version -eq 1 -and $cache.fingerprint) {
    $fpNow = Get-BakeFingerprint $root
    if ($cache.fingerprint -eq $fpNow) {
      $outHash = Get-FileHashSafe $bakedHpp
      if ($outHash -ne "MISSING" -and $outHash -ne "ERROR" -and $cache.output_hash -eq $outHash) {
        Write-Host "$TAG Bake up-to-date: $bakedHpp - skipping build & bake (use -Clean to force)"
        $skipBuildAndRun = $true
      }
    }
  }
}
if ($skipBuildAndRun) {
  $srcMain = Join-Path $root "src\main.cpp"
  if ((Test-Path -LiteralPath $srcMain) -and (Test-Path -LiteralPath $bakedHpp) -and (Get-Item -LiteralPath $srcMain).LastWriteTime -gt (Get-Item -LiteralPath $bakedHpp).LastWriteTime) {
    Write-Host "$TAG src/main.cpp newer than $bakedHpp, forcing rebuild"
    $skipBuildAndRun = $false
  }
}
if ($skipBuildAndRun) { return }

function Resolve-VcVars64 {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found" }
  $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $installPath) { throw "Visual Studio with C++ tools not found" }
  $vcvars = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
  if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
  return $vcvars
}

$vcvarsPath = $null
try { $vcvarsPath = Resolve-VcVars64 } catch { $vcvarsPath = $null }
$needInit = $true
if ((Test-Path -LiteralPath $envCache) -and $vcvarsPath -and (Test-Path -LiteralPath $vcvarsPath)) {
  $needInit = (Get-Item -LiteralPath $envCache).LastWriteTime -lt (Get-Item -LiteralPath $vcvarsPath).LastWriteTime
}
if ($needInit) {
  if (-not $vcvarsPath) { $vcvarsPath = Resolve-VcVars64 }
  Write-Host "$TAG Initializing Visual Studio build environment..."
  New-Item -ItemType Directory -Force -Path $stampDir | Out-Null
  $tmp = [IO.Path]::GetTempFileName()
  cmd /c "`"$vcvarsPath`" >NUL && set" | Out-File -Encoding ascii $tmp
  Get-Content -LiteralPath $tmp | Where-Object { $_ -match "^[A-Za-z0-9_()]+=" } | ForEach-Object {
    $idx = $_.IndexOf("=")
    $name = $_.Substring(0, $idx)
    $val = $_.Substring($idx + 1)
    Set-Item -Path ("Env:" + $name) -Value $val
    $vEsc = $val -replace "'", "''"
    "Set-Item -Path 'Env:$name' -Value '$vEsc'"
  } | Out-File -Encoding ascii $envCache
  Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
  Write-Host "$OK Visual Studio environment initialized."
} else {
  . $envCache
}

$cl = $null
$clCache = Join-Path $stampDir "cl.path.txt"
if ((Test-Path -LiteralPath $clCache) -and -not $Clean) {
  $cached = (Get-Content -LiteralPath $clCache -ErrorAction SilentlyContinue | Select-Object -First 1)
  if ($cached -and (Test-Path -LiteralPath $cached)) {
    try {
      $cTime = (Get-Item -LiteralPath $cached).LastWriteTime
      $cacheTime = (Get-Item -LiteralPath $clCache).LastWriteTime
      if ($cTime -le $cacheTime) { $cl = $cached } else { $cl = $null }
    } catch { $cl = $cached }
  }
}
if (-not $cl) { $cl = (Get-Command cl.exe -ErrorAction SilentlyContinue).Source }
if (-not $cl) {
  $cl = Get-ChildItem -Path "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $cl) { throw "cl.exe not found in PATH" }

if ($cl -like "*\Llvm\*") {
  $alt = Get-ChildItem -Path "${env:ProgramFiles}\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe" -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notlike "*\Llvm\*" } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
  if ($alt) { $cl = $alt }
}
try { Set-Content -LiteralPath $clCache -Value $cl -Encoding UTF8 -Force } catch {}

$cmakeLists = Join-Path $root "Tools\Bake\Source\CMakeLists.txt"
$needConfigure = $false
$buildNinja = Join-Path $buildDir "build.ninja"
if (-not (Test-Path -LiteralPath $buildNinja)) { $needConfigure = $true } else {
  $ninjaTime = (Get-Item -LiteralPath $buildNinja).LastWriteTime
  if ((Test-Path -LiteralPath $cmakeLists) -and (Get-Item -LiteralPath $cmakeLists).LastWriteTime -gt $ninjaTime) { $needConfigure = $true } else {
    $latestSrc = Get-ChildItem -LiteralPath (Join-Path $root "Tools\Bake\Source") -Recurse -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latestSrc -and $latestSrc.LastWriteTime -gt $ninjaTime) { $needConfigure = $true }
    $latestSrc2 = Get-ChildItem -LiteralPath (Join-Path $root "src") -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @(".cpp",".hpp",".c",".h") } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $needConfigure -and $latestSrc2 -and $latestSrc2.LastWriteTime -gt $ninjaTime) { $needConfigure = $true }
  }
}
$screen = Get-ScreenDefines
$screenStamp = Join-Path $stampDir "screen.cfg"
$screenCfg = "$($screen[0]) $($screen[1]) $($screen[2])"
if (Test-Path -LiteralPath $screenStamp) {
  if ((Get-Content -LiteralPath $screenStamp -ErrorAction SilentlyContinue | Select-Object -First 1) -ne $screenCfg) { $needConfigure = $true }
} else {
  $needConfigure = $true
}
if ($needConfigure) {
  Write-Host "$TAG Configuring CMake for bake... ($cl)"
  $env:CXX = $cl
  $env:CC = $cl
  $rc = $null; $mt = $null
  try { $rc = (Get-Command rc.exe -ErrorAction SilentlyContinue).Source } catch {}
  if (-not $rc) { $rc = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\rc.exe" -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName }
  try { $mt = (Get-Command mt.exe -ErrorAction SilentlyContinue).Source } catch {}
  if (-not $mt) { $mt = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\mt.exe" -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName }
  $cmakeRcArgs = @()
  if ($rc -and (Test-Path -LiteralPath $rc)) { $rcFwd = $rc -replace '\\','/'; $cmakeRcArgs += "-DCMAKE_RC_COMPILER=`"$rcFwd`""; Write-Host "$TAG Using rc: $rc" }
  if ($mt -and (Test-Path -LiteralPath $mt)) { $mtFwd = $mt -replace '\\','/'; $cmakeRcArgs += "-DCMAKE_MT=`"$mtFwd`""; Write-Host "$TAG Using mt: $mt" }
  cmake -S (Join-Path $root "Tools\Bake\Source") -B $buildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_CXX_COMPILER="$cl" `
    -DPIP3D_SCREEN_WIDTH="$($screen[0])" `
    -DPIP3D_SCREEN_HEIGHT="$($screen[1])" `
    -DPIP3D_SCREEN_BAND_COUNT="$($screen[2])" @cmakeRcArgs
  if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
  Set-Content -LiteralPath $screenStamp -Value $screenCfg -Encoding UTF8 -Force
}

cmake --build $buildDir --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

$exe = Join-Path $buildDir "bake.exe"

if ($NoRun) {
  Write-Host "$OK Build complete: $exe"
  return
}

$fpBeforeRun = Get-BakeFingerprint $root
$cachedAfterBuild = $null
try { $cachedAfterBuild = Get-Content -LiteralPath $bakeCachePath -Raw -ErrorAction Stop | ConvertFrom-Json } catch { $cachedAfterBuild = $null }
if ($cachedAfterBuild -and $cachedAfterBuild.fingerprint -eq $fpBeforeRun -and (Test-Path -LiteralPath $bakedHpp)) {
  $h2 = Get-FileHashSafe $bakedHpp
  if ($h2 -eq $cachedAfterBuild.output_hash) {
    Write-Host "$TAG Bake up-to-date after build - skipping run."
    return
  }
}

if (Test-Path -LiteralPath $outDir) {
  Get-ChildItem -LiteralPath $outDir -Force -ErrorAction SilentlyContinue | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
} else {
  New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

if (-not $env:PIP3D_BAKE_SHOT) { $env:PIP3D_BAKE_SHOT = "1" }

Push-Location $root
try {
  & $exe
  $code = $LASTEXITCODE
}
finally {
  Pop-Location
}
if ($code -ne 0) { exit $code }

$py = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($py -and (Test-Path -LiteralPath (Join-Path $outDir "Sparse.png"))) {
  & $py (Join-Path $root "Tools\Bake\Sparsemap.py") (Join-Path $outDir "Sparse.png") (Join-Path $outDir "Sparsemap.png")
  if ($LASTEXITCODE -ne 0) { Write-Host "$WARN Sparse map report skipped (python/matplotlib unavailable)" }
}

try {
  New-Item -ItemType Directory -Force -Path (Split-Path $bakeCachePath -Parent) | Out-Null
  $outHash2 = Get-FileHashSafe $bakedHpp
  $altHpp = Join-Path $root "src\Lighting\BakedScene.hpp"
  if ($altHpp -ne $bakedHpp -and (Test-Path -LiteralPath $altHpp)) { $outHash2 = Get-FileHashSafe $altHpp }
  $cacheObj = [ordered]@{ version = 1; fingerprint = $fpBeforeRun; output_hash = $outHash2; built_at = (Get-Date).ToString("o") }
  $tmpCache = "$bakeCachePath.tmp"
  [IO.File]::WriteAllText($tmpCache, ($cacheObj | ConvertTo-Json -Depth 5))
  Move-Item -LiteralPath $tmpCache -Destination $bakeCachePath -Force -ErrorAction SilentlyContinue
  if (Test-Path -LiteralPath $pioCachePath) {
    try {
      $pj = Get-Content -LiteralPath $pioCachePath -Raw -ErrorAction Stop | ConvertFrom-Json
      if (-not $pj.entries) { $pj | Add-Member -NotePropertyName entries -NotePropertyValue ([pscustomobject]@{}) -Force }
      $pj.entries | Add-Member -NotePropertyName "bake:scene" -NotePropertyValue ([pscustomobject]@{ fingerprint = $fpBeforeRun; output_hash = $outHash2 }) -Force
      $tmp2 = "$pioCachePath.tmp"
      [IO.File]::WriteAllText($tmp2, ($pj | ConvertTo-Json -Depth 10))
      Move-Item -LiteralPath $tmp2 -Destination $pioCachePath -Force -ErrorAction SilentlyContinue
    } catch {}
  }
} catch {}

exit $code
