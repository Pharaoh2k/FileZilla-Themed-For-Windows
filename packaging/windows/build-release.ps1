# FileZilla Themed packaging, Pharaoh2k, 2026-09-30, GPL-3.0-or-later.
[CmdletBinding()]
param(
    [string]$Prefix = 'C:\msys64\mingw64',
    [string]$OutputRoot = 'C:\Users\Pharaoh\Downloads\fzbuild\release',
    [ValidateRange(1, 65535)][int]$Revision = 6,
    [string]$ApplicationBundle = '',
    [string]$MakeNsis = '',
    [string]$Git = 'C:\Program Files\Git\cmd\git.exe'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$bin = Join-Path $Prefix 'bin'
$appBin = if ($ApplicationBundle) { (Resolve-Path $ApplicationBundle).Path } else { $bin }
if (!$MakeNsis) { $MakeNsis = Join-Path $bin 'makensis.exe' }
$objdump = Join-Path $bin 'objdump.exe'
$strip = Join-Path $bin 'strip.exe'
$release = Join-Path $OutputRoot "rev$Revision"
$bundleName = 'FileZilla-Themed-3.70.6-win64'
$stage = Join-Path $release $bundleName

. (Join-Path $PSScriptRoot 'native-tools.ps1')
function Get-PeInfo([string]$Path) {
    return (Invoke-Checked $objdump @('-p', $Path)) -join "`n"
}
function Get-Imports([string]$Info) {
    foreach ($match in [regex]::Matches($Info, '(?m)^\s*DLL Name:\s*(\S+)')) {
        $match.Groups[1].Value
    }
}
function Test-SystemDll([string]$Name) {
    return ($Name -match '^(api-ms-win-|ext-ms-win-)' -or
        (Test-Path (Join-Path $env:WINDIR "System32\$Name")))
}

foreach ($tool in @($MakeNsis, $objdump, $strip, $Git)) {
    if (!(Test-Path $tool)) { throw "Required build tool missing: $tool" }
}
if (Test-Path $release) { throw "Output already exists; move it aside before rebuilding: $release" }

# These COM servers do not occur in the executable's DLL import table.
# Check them explicitly, including bitness, exports, and dependencies.
Write-Host 'Validating both shell-extension DLLs...'
$extensions = [ordered]@{'fzshellext.dll' = 'pei-i386'; 'fzshellext_64.dll' = 'pei-x86-64'}
foreach ($dll in $extensions.Keys) {
    $path = Join-Path $appBin $dll
    if (!(Test-Path $path)) { throw "Required Shell Extension missing: $path" }
    $info = Get-PeInfo $path
    if ($info -notmatch ('file format ' + [regex]::Escape($extensions[$dll]) + '\b')) {
        throw "Wrong architecture for $dll"
    }
    foreach ($export in @('DllGetClassObject', 'DllCanUnloadNow', 'DllRegisterServer', 'DllUnregisterServer')) {
        if ($info -notmatch ('\b' + $export + '\b')) { throw "$dll is missing $export" }
    }
    foreach ($dependency in (Get-Imports $info)) {
        if (!(Test-SystemDll $dependency)) {
            throw "$dll requires $dependency. Rebuild the extension with static compiler runtimes."
        }
    }
}

# Resolve application dependencies before writing any output. Unknown imports
# are fatal so a missing runtime cannot silently produce a broken release.
Write-Host 'Resolving application runtime dependencies...'
$files = @{'filezilla.exe' = (Join-Path $appBin 'filezilla.exe')}
$queue = [Collections.Generic.Queue[string]]::new()
$queue.Enqueue('filezilla.exe')
while ($queue.Count) {
    $name = $queue.Dequeue()
    $info = Get-PeInfo $files[$name]
    if ($info -notmatch 'file format pei-x86-64\b') { throw "Wrong architecture for $name" }
    foreach ($dependency in (Get-Imports $info)) {
        if ($files.ContainsKey($dependency)) { continue }
        $path = Join-Path $appBin $dependency
        if (Test-Path $path) {
            $files[$dependency] = $path
            $queue.Enqueue($dependency)
        } elseif (!(Test-SystemDll $dependency)) {
            throw "Unresolved dependency: $name -> $dependency"
        }
    }
}
foreach ($dll in $extensions.Keys) { $files[$dll] = Join-Path $appBin $dll }

New-Item -ItemType Directory -Path $stage | Out-Null
Write-Host "Staging $($files.Count) executable and DLL files..."
foreach ($name in ($files.Keys | Sort-Object)) {
    $destination = Join-Path $stage $name
    Copy-Item $files[$name] $destination
    if (!$ApplicationBundle) {
        Invoke-Checked $strip @('--strip-all', $destination) | Out-Null
    }
}
if ($ApplicationBundle) {
    # Retain the staged application's tested bytes and installed data files.
    foreach ($directory in @('resources', 'docs', 'locales')) {
        Copy-Item (Join-Path $ApplicationBundle $directory) $stage -Recurse
    }
} else {
    Copy-Item (Join-Path $Prefix 'share\filezilla\resources') $stage -Recurse
    Copy-Item (Join-Path $Prefix 'share\filezilla\docs') $stage -Recurse
    foreach ($catalog in (Get-ChildItem (Join-Path $Prefix 'share\locale') -Filter filezilla.mo -Recurse)) {
        $locale = $catalog.Directory.Parent.Name
        $destination = Join-Path $stage "locales\$locale"
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        Copy-Item $catalog.FullName $destination
    }
}
$catalogs = @(Get-ChildItem (Join-Path $stage 'locales') -Filter filezilla.mo -Recurse)
if (!$catalogs.Count) { throw 'No translation catalogs found' }
foreach ($name in @('LICENSE', 'COPYING', 'GPL.html', 'AUTHORS', 'NEWS', 'CHANGELOG.md', 'CHANGES.fork.md')) {
    Copy-Item (Join-Path $repo $name) $stage
}
Copy-Item (Join-Path $PSScriptRoot 'RELEASE-NOTES.md') $stage
Copy-Item (Join-Path $repo 'CHANGELOG.md') $release
Copy-Item (Join-Path $PSScriptRoot 'RELEASE-NOTES.md') $release

# Checksums of the extracted bundle make it easy to audit an installation.
$utf8 = [Text.UTF8Encoding]::new($false)
$manifest = foreach ($file in (Get-ChildItem $stage -File -Recurse | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
    '{0}  {1}' -f (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
}
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'), [string[]]$manifest, $utf8)

Write-Host 'Compiling the installer...'
$buildLog = Invoke-Checked $MakeNsis @('-V3', "-DREVISION=$Revision", "-DSRCDIR=$stage", "-DRELEASEDIR=$release", (Join-Path $PSScriptRoot 'installer.nsi'))
[IO.File]::WriteAllLines((Join-Path $release 'installer-build.log'), [string[]]$buildLog, $utf8)

Add-Type -AssemblyName System.IO.Compression.FileSystem
Write-Host 'Creating release archives and checksums...'
$portableZip = Join-Path $release "$bundleName-rev$Revision.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $portableZip, [IO.Compression.CompressionLevel]::Optimal, $true)
$setupName = "$bundleName-setup-rev$Revision.exe"
$setupZip = Join-Path $release "$bundleName-setup-rev$Revision.zip"
$archive = [IO.Compression.ZipFile]::Open($setupZip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in @($setupName, 'RELEASE-NOTES.md', 'CHANGELOG.md')) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $release $name), $name) | Out-Null
    }
} finally { $archive.Dispose() }

# GitHub supplies source archives from the published release tag.
$commit = Invoke-Checked $Git @('-C', $repo, 'rev-parse', 'HEAD')
$status = Invoke-Checked $Git @('-C', $repo, 'status', '--short')
$buildInfo = @(
    "FileZilla Themed 3.70.6 rev$Revision",
    "Packaged UTC: $([DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss'))",
    "Source base commit: $commit",
    'Publish only after committing the source and tagging the matching revision.',
    "Application binaries from: $appBin",
    "NSIS: $(Invoke-Checked $MakeNsis @('-VERSION'))",
    "Translation catalogs: $($catalogs.Count)",
    'Working tree:', ($status -join "`r`n")
)
[IO.File]::WriteAllLines((Join-Path $release 'BUILD-INFO.txt'), [string[]]$buildInfo, $utf8)
$checksums = foreach ($file in (Get-ChildItem $release -File | Sort-Object Name)) {
    '{0}  {1}' -f (Get-FileHash $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $file.Name
}
[IO.File]::WriteAllLines((Join-Path $release 'SHA256SUMS.txt'), [string[]]$checksums, $utf8)
Write-Host "Release created: $release"
