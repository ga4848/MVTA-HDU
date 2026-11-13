$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'bin\Release\CircleCenterDetector.exe'
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
Assert-Count (Join-Path $PSScriptRoot 'images\sample.jpg') 'sample' 4
foreach ($name in @('blank', 'known_centers')) {
    $bitmap = [System.Drawing.Bitmap]::new(640, 480)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(70,70,70))
    try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(180,180,180))
        if ($name -eq 'known_centers') {
            $graphics.FillEllipse($brush, 40, 40, 100, 100)
            $graphics.FillEllipse($brush, 250, 170, 60, 60)
            $graphics.FillEllipse($brush, 440, 340, 40, 40)
            $graphics.FillEllipse($brush, -35, 250, 70, 70)
            $graphics.FillRectangle($brush, 400, 40, 80, 80)
            $graphics.FillRectangle($brush, 60, 350, 130, 20)
            $graphics.FillEllipse($brush, 380, 260, 4, 4)
        }
        $path = Join-Path $testRoot ($name + '.png')
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $brush.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    $expected = if ($name -eq 'known_centers') { 3 } else { 0 }
    Assert-Count $path $name $expected
}
$actual = @(Import-Csv -LiteralPath (Join-Path $testRoot 'known_centers\objects.csv'))
$truth = @(@(90.0,90.0,50.0), @(280.0,200.0,30.0), @(460.0,360.0,20.0))
for ($i = 0; $i -lt $truth.Count; $i++) {
    $dx = [double]$actual[$i].center_x_px - $truth[$i][0]
    $dy = [double]$actual[$i].center_y_px - $truth[$i][1]
    $centerError = [math]::Sqrt($dx*$dx + $dy*$dy)
    $radiusError = [math]::Abs([double]$actual[$i].radius_px - $truth[$i][2])
    if ($centerError -gt 1.0 -or $radiusError -gt 1.5) { throw "Circle $i exceeded numeric tolerance" }
    Write-Host "PASS: circle $($i+1), center error $([math]::Round($centerError,3)) px, radius error $([math]::Round($radiusError,3)) px"
}
& $exe (Join-Path $testRoot 'missing.png') (Join-Path $testRoot 'invalid')
if ($LASTEXITCODE -ne 1) { throw 'Missing input must return 1' }
& $exe (Join-Path $PSScriptRoot 'images\sample.jpg') (Join-Path $testRoot 'invalid') --threshold 256
if ($LASTEXITCODE -ne 1) { throw 'Invalid threshold must return 1' }
$protected = Join-Path $testRoot '04_result.png'
Copy-Item -LiteralPath (Join-Path $testRoot 'known_centers.png') -Destination $protected -Force
$before = (Get-FileHash -LiteralPath $protected).Hash
& $exe $protected $testRoot
if ($LASTEXITCODE -ne 1) { throw 'Input overwrite must be rejected' }
if ((Get-FileHash -LiteralPath $protected).Hash -ne $before) { throw 'Source image changed' }
Write-Host 'PASS: missing input, invalid threshold and source overwrite protection'
Write-Host 'All verification checks passed.'
