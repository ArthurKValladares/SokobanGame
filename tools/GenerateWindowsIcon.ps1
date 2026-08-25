param(
    [string]$OutputPath = (Join-Path $PSScriptRoot "..\resources\Sokoban.ico")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class SokobanIconNative {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool DestroyIcon(IntPtr hIcon);
}
"@

$outputDirectory = Split-Path -Parent $OutputPath
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$size = 256
$bitmap = [System.Drawing.Bitmap]::new(
    $size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

try {
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $background = [System.Drawing.RectangleF]::new(8, 8, 240, 240)
    $corner = 48
    $rounded = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $rounded.AddArc($background.X, $background.Y, $corner, $corner, 180, 90)
    $rounded.AddArc($background.Right - $corner, $background.Y, $corner, $corner, 270, 90)
    $rounded.AddArc($background.Right - $corner, $background.Bottom - $corner, $corner, $corner, 0, 90)
    $rounded.AddArc($background.X, $background.Bottom - $corner, $corner, $corner, 90, 90)
    $rounded.CloseFigure()
    $graphics.FillPath(
        [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 20, 33, 61)),
        $rounded)

    $text = [System.Drawing.StringFormat]::new()
    $text.Alignment = [System.Drawing.StringAlignment]::Center
    $text.LineAlignment = [System.Drawing.StringAlignment]::Center
    $font = [System.Drawing.Font]::new("Segoe UI", 154, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
    $graphics.DrawString(
        "S", $font,
        [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 248, 245, 236)),
        [System.Drawing.RectangleF]::new(12, -6, 232, 214), $text)
    $graphics.DrawLine(
        [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 233, 168, 58), 12),
        63, 207, 193, 207)

    $iconHandle = $bitmap.GetHicon()
    try {
        $icon = [System.Drawing.Icon]::FromHandle($iconHandle)
        try {
            $stream = [System.IO.File]::Open(
                $OutputPath,
                [System.IO.FileMode]::Create,
                [System.IO.FileAccess]::Write,
                [System.IO.FileShare]::None)
            try {
                $icon.Save($stream)
            } finally {
                $stream.Dispose()
            }
        } finally {
            $icon.Dispose()
        }
    } finally {
        [SokobanIconNative]::DestroyIcon($iconHandle) | Out-Null
    }
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}
