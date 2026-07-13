$iconFile = Join-Path $PSScriptRoot "app.ico"
if (Test-Path $iconFile) { Remove-Item $iconFile -Force }

Add-Type -AssemblyName System.Drawing

$bmp = New-Object System.Drawing.Bitmap(32, 32)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(0, 120, 215))
$font = New-Object System.Drawing.Font('Microsoft YaHei', 15, [System.Drawing.FontStyle]::Bold)
$g.DrawString('IP', $font, [System.Drawing.Brushes]::White, 2, 4)
$g.Dispose()

$fs = [System.IO.File]::OpenWrite($iconFile)
$bw = New-Object System.IO.BinaryWriter($fs)

$xorSize = 32 * 32 * 4     # 4096
$andSize = 32 * 32 / 8      # 128
$bmpInfoSize = 40
$imageDataSize = $bmpInfoSize + $xorSize + $andSize   # 4264

# ICO header
$bw.Write([UInt16]0)        # reserved
$bw.Write([UInt16]1)        # type: ICO
$bw.Write([UInt16]1)        # count: 1 image

# Directory entry
$bw.Write([byte]32)         # width
$bw.Write([byte]32)         # height
$bw.Write([byte]0)          # color count
$bw.Write([byte]0)          # reserved
$bw.Write([UInt16]0)        # planes (hotspot X)
$bw.Write([UInt16]32)       # bpp (hotspot Y)
$bw.Write([UInt32]$imageDataSize)   # image data size
$bw.Write([UInt32]22)       # offset to image data (header size = 6+2*entry = 22)

# BITMAPINFOHEADER
$bw.Write([UInt32]40)       # biSize
$bw.Write([Int32]32)       # biWidth
$bw.Write([Int32]64)       # biHeight (XOR + AND)
$bw.Write([UInt16]1)        # biPlanes
$bw.Write([UInt16]32)       # biBitCount
$bw.Write([UInt32]0)        # biCompression (BI_RGB)
$bw.Write([UInt32]$xorSize) # biSizeImage
$bw.Write([Int32]0)        # biXPelsPerMeter
$bw.Write([Int32]0)        # biYPelsPerMeter
$bw.Write([UInt32]0)        # biClrUsed
$bw.Write([UInt32]0)        # biClrImportant

# XOR mask (bottom-to-top rows)
for ($y = 31; $y -ge 0; $y--) {
    for ($x = 0; $x -lt 32; $x++) {
        $c = $bmp.GetPixel($x, $y)
        $bw.Write($c.B)
        $bw.Write($c.G)
        $bw.Write($c.R)
        $bw.Write($c.A)
    }
}

# AND mask (all zeros = fully opaque)
for ($i = 0; $i -lt $andSize; $i++) {
    $bw.Write([byte]0)
}

$bw.Close()
$fs.Close()
$bmp.Dispose()

Write-Host "app.ico created successfully"