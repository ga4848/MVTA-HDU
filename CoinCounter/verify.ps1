$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'bin\Release\CoinCounter.exe'
$testRoot = Join-Path $PSScriptRoot 'verification'
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
Add-Type -AssemblyName System.Drawing
function Assert-Count($imagePath, $name, $expected) {
    $destination = Join-Path $testRoot $name
    & $exe $imagePath $destination
    if ($LASTEXITCODE -ne 0) { throw "Execution failed: $name" }
    $summary = Get-Content -LiteralPath (Join-Path $destination 'summary.json') -Raw | ConvertFrom-Json
    if ($summary.count -ne $expected) { throw "$name expected $expected, got $($summary.count)" }
    $csv = @(Import-Csv -LiteralPath (Join-Path $destination 'objects.csv'))
    if ($csv.Count -ne $expected) { throw "CSV count mismatch: $name" }
    Write-Host "PASS: $name = $expected"
}
Assert-Count (Join-Path $PSScriptRoot 'images\sample.jpg') 'sample' 10
foreach ($name in @('blank', 'circles')) {
    $bitmap = [System.Drawing.Bitmap]::new(640, 480)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $dark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(140,140,140))
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(35,35,35), 6)
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(35,35,35))
        if ($name -eq 'circles') {
            $graphics.FillEllipse([System.Drawing.Brushes]::White, 40, 40, 100, 100)
            $graphics.FillEllipse($dark, 250, 160, 100, 100)
            $graphics.FillEllipse([System.Drawing.Brushes]::White, 440, 330, 100, 100)
            $graphics.FillEllipse([System.Drawing.Brushes]::Black, 72, 72, 36, 36)
            $graphics.DrawLine($pen, 265, 180, 320, 230)
            $graphics.FillRectangle([System.Drawing.Brushes]::White, 400, 40, 80, 80)
            $graphics.FillRectangle([System.Drawing.Brushes]::White, 40, 300, 130, 20)
            $graphics.FillEllipse([System.Drawing.Brushes]::White, 400, 260, 4, 4)
        }
        $path = Join-Path $testRoot ($name + '.png')
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $pen.Dispose(); $dark.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    $expected = if ($name -eq 'circles') { 3 } else { 0 }
    Assert-Count $path $name $expected
}
& $exe (Join-Path $testRoot 'missing.png') (Join-Path $testRoot 'invalid')
if ($LASTEXITCODE -ne 1) { throw 'Missing input must return 1' }
& $exe (Join-Path $PSScriptRoot 'images\sample.jpg') (Join-Path $testRoot 'invalid') --threshold 256
if ($LASTEXITCODE -ne 1) { throw 'Invalid threshold must return 1' }
$protected = Join-Path $testRoot '04_result.png'
# Use a real PNG for the overwrite-protection test.
Copy-Item -LiteralPath (Join-Path $testRoot 'circles.png') -Destination $protected -Force
$before = (Get-FileHash -LiteralPath $protected).Hash
& $exe $protected $testRoot
if ($LASTEXITCODE -ne 1) { throw 'Input overwrite must be rejected' }
if ((Get-FileHash -LiteralPath $protected).Hash -ne $before) { throw 'Source image changed' }
Write-Host 'PASS: missing input, invalid threshold and source overwrite protection'
Write-Host 'All verification checks passed.'
