# Builds a multi-resolution .ico file from a PNG image.
# Usage: powershell -File Tools\make-icon.ps1 -Source refs\ICON.png -Destination refs\ICON.ico
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

$sourceImage = [System.Drawing.Image]::FromFile((Resolve-Path $Source).Path)

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$payloads = New-Object 'System.Collections.Generic.List[byte[]]'

foreach ($size in $sizes) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $graphics.DrawImage($sourceImage, 0, 0, $size, $size)
    $graphics.Dispose()

    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $payloads.Add($stream.ToArray())
    $stream.Dispose()
    $bitmap.Dispose()
}

$sourceImage.Dispose()

$output = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $output

# ICONDIR
$writer.Write([uint16]0)                # reserved
$writer.Write([uint16]1)                # type: icon
$writer.Write([uint16]$sizes.Count)     # image count

$offset = 6 + 16 * $sizes.Count

for ($i = 0; $i -lt $sizes.Count; $i++) {
    $size = $sizes[$i]
    $payload = $payloads[$i]

    $writer.Write([byte]($size % 256))  # 256 is stored as 0
    $writer.Write([byte]($size % 256))
    $writer.Write([byte]0)              # palette size
    $writer.Write([byte]0)              # reserved
    $writer.Write([uint16]1)            # color planes
    $writer.Write([uint16]32)           # bits per pixel
    $writer.Write([uint32]$payload.Length)
    $writer.Write([uint32]$offset)

    $offset += $payload.Length
}

foreach ($payload in $payloads) {
    $writer.Write($payload)
}

$writer.Flush()

$destinationPath = if ([System.IO.Path]::IsPathRooted($Destination)) {
    $Destination
} else {
    Join-Path (Get-Location) $Destination
}

[System.IO.File]::WriteAllBytes($destinationPath, $output.ToArray())

$writer.Dispose()
$output.Dispose()

Write-Host "Wrote $destinationPath ($((Get-Item $destinationPath).Length) bytes, $($sizes.Count) resolutions)"
