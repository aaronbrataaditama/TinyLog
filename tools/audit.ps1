# Audits a built TinyLog.exe for imported DLLs and registry API usage (Spec 5 / 12).
# Exit 0 = pass, 1 = fail.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/audit.ps1 -Path build/release/TinyLog.exe
param([Parameter(Mandatory)][string]$Path)
$ErrorActionPreference = 'Stop'

$allowedDlls = @('KERNEL32','USER32','GDI32','SHELL32','OLE32','OLEAUT32','COMCTL32','UxTheme',
                 'ADVAPI32','COMDLG32','SHLWAPI')
$forbiddenPattern = '^(Reg(Create|Set|Open|Delete|Query|Enum)|SHReg)'

function Find-Dumpbin {
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'dumpbin.exe not on PATH and vswhere.exe not found. Run from a VS developer prompt.' }
    # Same VC-tools filter as tools/build.cmd; -find handles the MSVC version folder.
    $vsDir = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
    if (-not $vsDir) { throw 'No Visual Studio install with the C++ tools found.' }
    $hit = Get-ChildItem -Path (Join-Path $vsDir 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
           Sort-Object Name -Descending |
           ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' } |
           Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $hit) { throw "dumpbin.exe not found under $vsDir." }
    return $hit
}

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { Write-Host "FAIL: file not found: $Path"; exit 1 }
$exe      = (Resolve-Path -LiteralPath $Path).Path
$dumpbin  = Find-Dumpbin
Write-Host "Using dumpbin: $dumpbin"
Write-Host "Auditing:      $exe"

$deps    = & $dumpbin /nologo /dependents $exe
if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL: dumpbin /dependents failed'; exit 1 }
$imports = & $dumpbin /nologo /imports $exe
if ($LASTEXITCODE -ne 0) { Write-Host 'FAIL: dumpbin /imports failed'; exit 1 }

# DLL names: indented lines ending in .dll (dependents list, and import section headers)
$dllsFromDeps = @()
foreach ($line in $deps) {
    if ($line -match '^\s+([\w\-\.]+\.dll)\s*$') { $dllsFromDeps += $matches[1] }
}
$dllsFromDeps = @($dllsFromDeps | Sort-Object -Unique)

# Imported function names: "  <hint/rva> <Name>" lines (single identifier token after hex)
$funcs = New-Object System.Collections.Generic.List[string]
foreach ($line in $imports) {
    if ($line -match '^\s+[0-9A-Fa-f]+\s+([A-Za-z_][A-Za-z0-9_]*)\s*$') { $funcs.Add($matches[1]) }
}
$funcs = @($funcs | Sort-Object -Unique)

$failures = New-Object System.Collections.Generic.List[string]

Write-Host ''
Write-Host "== DLL dependencies ($($dllsFromDeps.Count)) =="
foreach ($dll in $dllsFromDeps) {
    $base = [IO.Path]::GetFileNameWithoutExtension($dll)
    $ok   = $allowedDlls | Where-Object { $_ -ieq $base }
    if ($ok) { Write-Host "  [ok]   $dll" }
    else     { Write-Host "  [FAIL] $dll  (not in allowlist)"; $failures.Add("DLL not allowed: $dll") }
}

Write-Host ''
Write-Host "== Registry API check ($($funcs.Count) imported names scanned) =="
$badFuncs = @($funcs | Where-Object { $_ -match $forbiddenPattern })
if ($badFuncs.Count -eq 0) { Write-Host '  [ok]   no Reg*/SHReg* imports' }
else {
    foreach ($f in $badFuncs) { Write-Host "  [FAIL] forbidden import: $f"; $failures.Add("Forbidden registry import: $f") }
}

Write-Host ''
if ($failures.Count -eq 0) { Write-Host 'AUDIT PASS'; exit 0 }
Write-Host "AUDIT FAIL ($($failures.Count) issue(s))"
exit 1
