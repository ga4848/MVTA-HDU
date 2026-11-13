$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'bin\Release\PaperclipCounter.exe'
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
Assert-Count (Join-Path $PSScriptRoot 'images\sample.png') 'sample' 13
# Independent synthetic objects test: three disconnected elongated rings,
# a touching frame and dust that must not contribute to the count.
foreach ($name in @('blank', 'rings')) {
    $bitmap = [System.Drawing.Bitmap]::new(640, 480)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::Black, 6)
    try {
        $graphics.Clear([System.Drawing.Color]::White)
        if ($name -eq 'rings') {
            $graphics.DrawEllipse($pen, 60, 70, 150, 40)
            $graphics.DrawEllipse($pen, 280, 160, 40, 150)
            $graphics.DrawEllipse($pen, 390, 370, 150, 40)
            $graphics.DrawRectangle($pen, 0, 0, 639, 479)
            $graphics.FillEllipse([System.Drawing.Brushes]::Black, 400, 200, 4, 4)
        }
        $path = Join-Path $testRoot ($name + '.png')
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $pen.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    $expected = if ($name -eq 'rings') { 3 } else { 0 }
    Assert-Count $path $name $expected
}
& $exe (Join-Path $testRoot 'missing.png') (Join-Path $testRoot 'invalid')
if ($LASTEXITCODE -ne 1) { throw 'Missing input must return 1' }
& $exe (Join-Path $PSScriptRoot 'images\sample.png') (Join-Path $testRoot 'invalid') --threshold 256
if ($LASTEXITCODE -ne 1) { throw 'Invalid threshold must return 1' }
$protected = Join-Path $testRoot '04_result.png'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'images\sample.png') -Destination $protected -Force
$before = (Get-FileHash -LiteralPath $protected).Hash
& $exe $protected $testRoot
if ($LASTEXITCODE -ne 1) { throw 'Input overwrite must be rejected' }
if ((Get-FileHash -LiteralPath $protected).Hash -ne $before) { throw 'Source image changed' }
Write-Host 'PASS: missing input, invalid threshold and source overwrite protection'
Write-Host 'All verification checks passed.'
