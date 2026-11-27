$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Stage = Join-Path (Split-Path $PSScriptRoot -Parent) 'Saved\SourceTextures'
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

$Sources = @(
    @{ Name = 'NASA_BlueMarble_Day.jpg'; Url = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-base/october/world.200410.3x21600x10800.jpg' },
    @{ Name = 'NASA_BlackMarble_2016_3km.jpg'; Url = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/imagerecords/144000/144898/BlackMarble_2016_3km.jpg' },
    @{ Name = 'NASA_BlueMarble_TopoBathy.jpg'; Url = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/world.topo.bathy.200412.3x21600x10800.jpg' }
)

foreach ($Source in $Sources) {
    $Path = Join-Path $Stage $Source.Name
    Invoke-WebRequest -Uri $Source.Url -OutFile $Path
    Write-Host ("Downloaded {0} ({1:N1} MB)" -f $Source.Name, ((Get-Item -LiteralPath $Path).Length / 1MB))
}

# NASA's original Blue Marble Clouds file is hosted on the legacy eoimages.gsfc.nasa.gov
# host. If that host is unavailable, use a byte-identical public mirror and verify its
# published NASA-source SHA-256 before processing it.
$CloudPath = Join-Path $Stage 'NASA_BlueMarble_Clouds_8192.tif'
$CloudUrl = 'https://eoimages.gsfc.nasa.gov/images/imagerecords/57000/57747/cloud_combined_8192.tif'
$CloudHash = 'd137775d8966ab8d443fd5126dc6e7ad72072bc1ed50555c5818d221735daf0f'
try {
    Invoke-WebRequest -Uri $CloudUrl -OutFile $CloudPath -TimeoutSec 30
} catch {
    Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/voltura/voltura-worlds/main/assets/orbit/earth-clouds-8192.tif' -OutFile $CloudPath
}
$ActualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $CloudPath).Hash.ToLowerInvariant()
if ($ActualHash -ne $CloudHash) { throw "NASA cloud map checksum mismatch: $ActualHash" }
Write-Host ("Verified NASA Blue Marble Clouds source checksum: {0}" -f $ActualHash)

Write-Host 'Run Scripts/prepare_earth_textures.py next; it validates every source as a >=4K 2:1 image.'
