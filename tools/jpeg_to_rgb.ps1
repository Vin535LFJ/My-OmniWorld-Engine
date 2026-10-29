# jpeg_to_rgb.ps1 - decode JPEG files and stretch them onto the panel canvas,
# writing top-down 24bpp rows (channel order still BGR: the swap is done by
# tools/coco_to_panel.mjs, which is where the byte-level contract lives).
#
# Why PowerShell: System.Drawing is already on every Windows box this repo is
# built on, so the panel needs no new dependency. Node has no JPEG decoder in
# core, and pulling one in (or an FFmpeg build) for a fetch tool would put an
# unreviewed license in the corner of the repo that is supposed to stay clean.
#
# Usage: powershell -NoProfile -File tools/jpeg_to_rgb.ps1 -ListFile list.txt
#        list lines: <source-jpg>;<destination-bgr>
param(
  [Parameter(Mandatory = $true)][string]$ListFile,
  [int]$Width = 640,
  [int]$Height = 480
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$count = 0
foreach ($pair in Get-Content -LiteralPath $ListFile) {
  if ([string]::IsNullOrWhiteSpace($pair)) { continue }
  $parts = $pair -split ';'
  if ($parts.Count -ne 2) { throw "list line is not '<src>;<dst>': $pair" }
  $src = $parts[0]
  $dst = $parts[1]
  if (-not (Test-Path -LiteralPath $src)) { throw "missing source image: $src" }

  $img = [System.Drawing.Image]::FromFile($src)
  try {
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height,
      [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
      # Stretch, not letterbox: normalized coordinates are invariant under a
      # full-canvas stretch, so the truth manifest needs no padding arithmetic
      # (coco_to_panel.mjs records the aspect distortion it introduces).
      $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
      $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
      $g.DrawImage($img, 0, 0, $Width, $Height)
    } finally { $g.Dispose() }

    $rect = New-Object System.Drawing.Rectangle(0, 0, $Width, $Height)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
      [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
      if ($data.Stride -ne $Width * 3) { throw "stride $($data.Stride) != $Width*3" }
      $buf = New-Object byte[] ($Width * $Height * 3)
      [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
      # LockBits stores the first row at the bottom: copy rows in reverse so the
      # file is top-left origin, which is what Observation::rgb is.
      $out = New-Object byte[] $buf.Length
      $rowBytes = $Width * 3
      for ($y = 0; $y -lt $Height; $y++) {
        [System.Array]::Copy($buf, ($Height - 1 - $y) * $rowBytes, $out, $y * $rowBytes, $rowBytes)
      }
      [System.IO.File]::WriteAllBytes($dst, $out)
    } finally { $bmp.UnlockBits($data); $bmp.Dispose() }
  } finally { $img.Dispose() }
  $count++
}
Write-Output "decoded $count image(s) to ${Width}x${Height} top-down BGR"
