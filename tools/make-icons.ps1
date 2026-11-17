# Generates resources/idle.ico and resources/recording.ico from the mascot artwork resources/tinylog.png.
#   idle.ico      — the mascot (also the EXE icon: IDI_IDLE is the first icon resource)
#   recording.ico — the mascot with a green "recording" badge in the bottom-right corner
# Each .ico holds PNG-encoded 32-bit ARGB images (valid on Win10+) at the sizes below.
# Run after changing the artwork and commit the outputs:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/make-icons.ps1
param([string]$Source = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root  = Split-Path -Parent $PSScriptRoot
if (-not $Source) { $Source = Join-Path $root 'resources\tinylog.png' }
$sizes = [int[]](16, 20, 24, 32, 40, 48, 64, 256)

# Crop the artwork to its opaque content and centre it on a square transparent canvas.
function Get-SquareArtwork {
    param([System.Drawing.Bitmap]$Src)
    $minX = $Src.Width; $minY = $Src.Height; $maxX = -1; $maxY = -1
    $rect = New-Object System.Drawing.Rectangle 0, 0, $Src.Width, $Src.Height
    $data = $Src.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $bytes = New-Object byte[] ($data.Stride * $Src.Height)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    } finally { $Src.UnlockBits($data) }
    for ($y = 0; $y -lt $Src.Height; $y++) {
        $row = $y * $data.Stride
        for ($x = 0; $x -lt $Src.Width; $x++) {
            if ($bytes[$row + $x * 4 + 3] -gt 16) {
                if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) { throw "Artwork $Source is fully transparent" }
    $w = $maxX - $minX + 1; $h = $maxY - $minY + 1
    $side = [Math]::Max($w, $h)
    $sq = New-Object System.Drawing.Bitmap $side, $side, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($sq)
    try {
        $g.Clear([System.Drawing.Color]::Transparent)
        $dst = New-Object System.Drawing.Rectangle ([int](($side - $w) / 2)), ([int](($side - $h) / 2)), $w, $h
        $g.DrawImage($Src, $dst, $minX, $minY, $w, $h, [System.Drawing.GraphicsUnit]::Pixel)
    } finally { $g.Dispose() }
    return $sq
}

function New-IconPng {
    param([System.Drawing.Bitmap]$Art, [int]$Px, [bool]$Badge)
    $bmp = New-Object System.Drawing.Bitmap $Px, $Px, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $attr = New-Object System.Drawing.Imaging.ImageAttributes
        $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)   # no fringe at the edges
        $g.DrawImage($Art, (New-Object System.Drawing.Rectangle 0, 0, $Px, $Px), 0, 0, $Art.Width, $Art.Height,
                     [System.Drawing.GraphicsUnit]::Pixel, $attr)
        if ($Badge) {
            # Green dot with a white ring, bottom-right, ~45% of the icon so it reads at 16 px.
            $d = [Math]::Max(7, [int][Math]::Round($Px * 0.45))
            $ring = [Math]::Max(1, [int][Math]::Round($Px / 16.0))
            $x = $Px - $d; $y = $Px - $d
            $white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
            $green = New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#1FB85A'))
            $g.FillEllipse($white, $x, $y, $d, $d)
            $g.FillEllipse($green, $x + $ring, $y + $ring, $d - 2 * $ring, $d - 2 * $ring)
            $white.Dispose(); $green.Dispose()
        }
    } finally { $g.Dispose() }
    $ms = New-Object System.IO.MemoryStream
    try {
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$ms.ToArray()
    } finally { $ms.Dispose(); $bmp.Dispose() }
}

function Write-Ico {
    param([string]$OutPath, [System.Collections.Generic.List[byte[]]]$Pngs, [int[]]$Sizes)
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    try {
        $bw.Write([uint16]0)              # ICONDIR reserved
        $bw.Write([uint16]1)              # type: icon
        $bw.Write([uint16]$Pngs.Count)
        $offset = 6 + 16 * $Pngs.Count
        for ($i = 0; $i -lt $Pngs.Count; $i++) {
            $s = $Sizes[$i]
            $bw.Write([byte]($s -band 0xFF))   # width  (0 means 256)
            $bw.Write([byte]($s -band 0xFF))   # height (0 means 256)
            $bw.Write([byte]0)                 # palette colours
            $bw.Write([byte]0)                 # reserved
            $bw.Write([uint16]1)               # planes
            $bw.Write([uint16]32)              # bits per pixel
            $bw.Write([uint32]$Pngs[$i].Length)
            $bw.Write([uint32]$offset)
            $offset += $Pngs[$i].Length
        }
        foreach ($p in $Pngs) { $bw.Write($p) }
        $bw.Flush()
        [System.IO.File]::WriteAllBytes($OutPath, $ms.ToArray())
    } finally { $bw.Dispose(); $ms.Dispose() }
}

$src = [System.Drawing.Bitmap]::FromFile($Source)
try { $art = Get-SquareArtwork -Src $src } finally { $src.Dispose() }
try {
    foreach ($variant in @(@{ Name = 'idle'; Badge = $false }, @{ Name = 'recording'; Badge = $true })) {
        $pngs = New-Object 'System.Collections.Generic.List[byte[]]'
        foreach ($s in $sizes) { $pngs.Add((New-IconPng -Art $art -Px $s -Badge $variant.Badge)) }
        Write-Ico -OutPath (Join-Path $root "resources\$($variant.Name).ico") -Pngs $pngs -Sizes $sizes
    }
} finally { $art.Dispose() }
Write-Host "Wrote resources\idle.ico and resources\recording.ico ($($sizes.Count) images each: $($sizes -join ', ') px)"
