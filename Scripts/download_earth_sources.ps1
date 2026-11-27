$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Stage = Join-Path (Split-Path $PSScriptRoot -Parent) 'Saved\SourceTextures'
New-Item -ItemType Directory -Force -Path $Stage | Out-Null

$Sources = @(
    @{ Name = 'NASA_BlueMarble_Day.jpg'; Url = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-base/october/world.200410.3x21600x10800.jpg' },
    @{ Name = 'NASA_BlackMarble_2016_3km.jpg'; Url = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/imagerecords/144000/144898/BlackMarble_2016_3km.jpg' },
    @{ Name = 'NASA_BlueMarble_TopoBathy.jpg'; Url = 'https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography-bathymetry/december/world.topo.bathy.200412.3x21600x10800.jpg' },
    # ESO/S. Brunier, "The Milky Way panorama" (eso0932a), CC BY 4.0.
    @{ Name = 'ESO_MilkyWay_Brunier_eso0932a.jpg'; Url = 'https://cdn.eso.org/images/large/eso0932a.jpg' }
)

foreach ($Source in $Sources) {
    $Path = Join-Path $Stage $Source.Name
    Invoke-WebRequest -Uri $Source.Url -OutFile $Path
    $Info = Get-Item -LiteralPath $Path
    if ($Info.Length -le 500000) { throw "Downloaded source is unexpectedly small: $($Source.Name)" }
    Write-Host ("Downloaded {0} ({1:N1} MB)" -f $Source.Name, ($Info.Length / 1MB))
}

Write-Host 'Run Scripts/prepare_earth_textures.py next; it validates every source as a >=4K 2:1 image.'
