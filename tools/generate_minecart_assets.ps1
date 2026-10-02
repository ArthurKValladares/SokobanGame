using namespace System.Collections.Generic
using namespace System.Numerics

param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\assets\custom\models')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function New-Vec([double]$X, [double]$Y, [double]$Z) {
    return [Vector3]::new([single]$X, [single]$Y, [single]$Z)
}

function Add-Vec([Vector3]$A, [Vector3]$B) {
    return [Vector3]::Add($A, $B)
}

function Subtract-Vec([Vector3]$A, [Vector3]$B) {
    return [Vector3]::Subtract($A, $B)
}

function New-Primitive([int]$Material) {
    return [pscustomobject]@{
        Material  = $Material
        Positions = [List[single]]::new()
        Normals   = [List[single]]::new()
        Uvs       = [List[single]]::new()
        Indices   = [List[uint32]]::new()
    }
}

function New-Model([string]$Name) {
    return [pscustomobject]@{
        Name       = $Name
        Primitives = @{}
    }
}

function Get-Primitive($Model, [int]$Material) {
    if (-not $Model.Primitives.ContainsKey($Material)) {
        $Model.Primitives[$Material] = New-Primitive $Material
    }
    return $Model.Primitives[$Material]
}

function Add-RawVertex($Primitive, [Vector3]$Position, [Vector3]$Normal,
                       [single]$U, [single]$V) {
    $index = [uint32]($Primitive.Positions.Count / 3)
    $Primitive.Positions.Add($Position.X)
    $Primitive.Positions.Add($Position.Y)
    $Primitive.Positions.Add($Position.Z)
    $Primitive.Normals.Add($Normal.X)
    $Primitive.Normals.Add($Normal.Y)
    $Primitive.Normals.Add($Normal.Z)
    $Primitive.Uvs.Add($U)
    $Primitive.Uvs.Add($V)
    $Primitive.Indices.Add($index)
}

function Get-FaceNormal([Vector3]$A, [Vector3]$B, [Vector3]$C) {
    $cross = [Vector3]::Cross((Subtract-Vec $B $A), (Subtract-Vec $C $A))
    if ($cross.LengthSquared() -lt 0.00000001) {
        throw 'Degenerate triangle in minecart asset geometry'
    }
    return [Vector3]::Normalize($cross)
}

function Add-Triangle($Model, [int]$Material, [Vector3]$A, [Vector3]$B,
                      [Vector3]$C) {
    $primitive = Get-Primitive $Model $Material
    $normal = Get-FaceNormal $A $B $C
    Add-RawVertex $primitive $A $normal 0.0 0.0
    Add-RawVertex $primitive $B $normal 1.0 0.0
    Add-RawVertex $primitive $C $normal 1.0 1.0
}

function Add-Quad($Model, [int]$Material, [Vector3]$A, [Vector3]$B,
                  [Vector3]$C, [Vector3]$D) {
    Add-Triangle $Model $Material $A $B $C
    Add-Triangle $Model $Material $A $C $D
}

function Add-BoxBasis($Model, [int]$Material, [Vector3]$Center,
                      [Vector3]$HalfX, [Vector3]$HalfY, [Vector3]$HalfZ) {
    $p000 = Subtract-Vec (Subtract-Vec (Subtract-Vec $Center $HalfX) $HalfY) $HalfZ
    $p100 = Add-Vec (Subtract-Vec (Subtract-Vec $Center $HalfY) $HalfZ) $HalfX
    $p010 = Add-Vec (Subtract-Vec (Subtract-Vec $Center $HalfX) $HalfZ) $HalfY
    $p110 = Add-Vec (Add-Vec (Subtract-Vec $Center $HalfZ) $HalfX) $HalfY
    $p001 = Add-Vec (Subtract-Vec (Subtract-Vec $Center $HalfX) $HalfY) $HalfZ
    $p101 = Add-Vec (Add-Vec (Subtract-Vec $Center $HalfY) $HalfX) $HalfZ
    $p011 = Add-Vec (Add-Vec (Subtract-Vec $Center $HalfX) $HalfY) $HalfZ
    $p111 = Add-Vec (Add-Vec (Add-Vec $Center $HalfX) $HalfY) $HalfZ

    Add-Quad $Model $Material $p000 $p010 $p110 $p100
    Add-Quad $Model $Material $p001 $p101 $p111 $p011
    Add-Quad $Model $Material $p000 $p001 $p011 $p010
    Add-Quad $Model $Material $p100 $p110 $p111 $p101
    Add-Quad $Model $Material $p000 $p100 $p101 $p001
    Add-Quad $Model $Material $p010 $p011 $p111 $p110
}

function Add-Box($Model, [int]$Material, [double]$CenterX,
                 [double]$CenterY, [double]$CenterZ, [double]$SizeX,
                 [double]$SizeY, [double]$SizeZ) {
    Add-BoxBasis $Model $Material (New-Vec $CenterX $CenterY $CenterZ) (New-Vec ($SizeX * 0.5) 0 0) (New-Vec 0 ($SizeY * 0.5) 0) (New-Vec 0 0 ($SizeZ * 0.5))
}

function Add-ChamferedPrismY($Model, [int]$Material, [double]$MinimumX,
                             [double]$MaximumX, [double]$MinimumZ,
                             [double]$MaximumZ, [double]$MinimumY,
                             [double]$MaximumY, [double]$Bevel) {
    [Vector3[]]$bottom = @(
        (New-Vec ($MinimumX + $Bevel) $MinimumY $MinimumZ),
        (New-Vec $MinimumX $MinimumY ($MinimumZ + $Bevel)),
        (New-Vec $MinimumX $MinimumY ($MaximumZ - $Bevel)),
        (New-Vec ($MinimumX + $Bevel) $MinimumY $MaximumZ),
        (New-Vec ($MaximumX - $Bevel) $MinimumY $MaximumZ),
        (New-Vec $MaximumX $MinimumY ($MaximumZ - $Bevel)),
        (New-Vec $MaximumX $MinimumY ($MinimumZ + $Bevel)),
        (New-Vec ($MaximumX - $Bevel) $MinimumY $MinimumZ)
    )
    [Vector3[]]$top = @()
    foreach ($point in $bottom) {
        $top += New-Vec $point.X $MaximumY $point.Z
    }

    $centerTop = New-Vec (($MinimumX + $MaximumX) * 0.5) $MaximumY (($MinimumZ + $MaximumZ) * 0.5)
    $centerBottom = New-Vec (($MinimumX + $MaximumX) * 0.5) $MinimumY (($MinimumZ + $MaximumZ) * 0.5)
    for ($i = 0; $i -lt $bottom.Count; ++$i) {
        $next = ($i + 1) % $bottom.Count
        Add-Triangle $Model $Material $centerTop $top[$i] $top[$next]
        Add-Triangle $Model $Material $centerBottom $bottom[$next] $bottom[$i]
        Add-Quad $Model $Material $bottom[$i] $bottom[$next] $top[$next] $top[$i]
    }
}

function Add-OctagonalRail($Model, [int]$Material, [double]$CenterX,
                           [double]$MinimumZ, [double]$MaximumZ,
                           [double]$MinimumY, [double]$MaximumY,
                           [double]$Width, [double]$Bevel) {
    $minimumX = $CenterX - $Width * 0.5
    $maximumX = $CenterX + $Width * 0.5
    [Vector3[]]$start = @(
        (New-Vec ($minimumX + $Bevel) $MinimumY $MinimumZ),
        (New-Vec $minimumX ($MinimumY + $Bevel) $MinimumZ),
        (New-Vec $minimumX ($MaximumY - $Bevel) $MinimumZ),
        (New-Vec ($minimumX + $Bevel) $MaximumY $MinimumZ),
        (New-Vec ($maximumX - $Bevel) $MaximumY $MinimumZ),
        (New-Vec $maximumX ($MaximumY - $Bevel) $MinimumZ),
        (New-Vec $maximumX ($MinimumY + $Bevel) $MinimumZ),
        (New-Vec ($maximumX - $Bevel) $MinimumY $MinimumZ)
    )
    [Vector3[]]$finish = @()
    foreach ($point in $start) {
        $finish += New-Vec $point.X $point.Y $MaximumZ
    }
    $centerStart = New-Vec $CenterX (($MinimumY + $MaximumY) * 0.5) $MinimumZ
    $centerFinish = New-Vec $CenterX (($MinimumY + $MaximumY) * 0.5) $MaximumZ
    for ($i = 0; $i -lt $start.Count; ++$i) {
        $next = ($i + 1) % $start.Count
        Add-Quad $Model $Material $start[$i] $finish[$i] $finish[$next] $start[$next]
        Add-Triangle $Model $Material $centerStart $start[$next] $start[$i]
        Add-Triangle $Model $Material $centerFinish $finish[$i] $finish[$next]
    }
}

function Add-CurvedRail($Model, [int]$Material, [double]$Radius,
                        [double]$Width, [double]$MinimumY,
                        [double]$MaximumY, [int]$Segments) {
    $innerRadius = $Radius - $Width * 0.5
    $outerRadius = $Radius + $Width * 0.5
    for ($i = 0; $i -lt $Segments; ++$i) {
        $theta0 = ([Math]::PI * 0.5) * $i / $Segments
        $theta1 = ([Math]::PI * 0.5) * ($i + 1) / $Segments
        $inner0Bottom = New-Vec ($innerRadius * [Math]::Cos($theta0)) $MinimumY (-$innerRadius * [Math]::Sin($theta0))
        $outer0Bottom = New-Vec ($outerRadius * [Math]::Cos($theta0)) $MinimumY (-$outerRadius * [Math]::Sin($theta0))
        $inner1Bottom = New-Vec ($innerRadius * [Math]::Cos($theta1)) $MinimumY (-$innerRadius * [Math]::Sin($theta1))
        $outer1Bottom = New-Vec ($outerRadius * [Math]::Cos($theta1)) $MinimumY (-$outerRadius * [Math]::Sin($theta1))
        $inner0Top = New-Vec $inner0Bottom.X $MaximumY $inner0Bottom.Z
        $outer0Top = New-Vec $outer0Bottom.X $MaximumY $outer0Bottom.Z
        $inner1Top = New-Vec $inner1Bottom.X $MaximumY $inner1Bottom.Z
        $outer1Top = New-Vec $outer1Bottom.X $MaximumY $outer1Bottom.Z

        Add-Quad $Model $Material $inner0Top $outer0Top $outer1Top $inner1Top
        Add-Quad $Model $Material $outer0Bottom $inner0Bottom $inner1Bottom $outer1Bottom
        Add-Quad $Model $Material $outer0Bottom $outer1Bottom $outer1Top $outer0Top
        Add-Quad $Model $Material $inner1Bottom $inner0Bottom $inner0Top $inner1Top

        if ($i -eq 0) {
            Add-Quad $Model $Material $outer0Bottom $outer0Top $inner0Top $inner0Bottom
        }
        if ($i -eq ($Segments - 1)) {
            Add-Quad $Model $Material $inner1Bottom $inner1Top $outer1Top $outer1Bottom
        }
    }
}

function Add-RadialBox($Model, [int]$Material, [double]$Radius,
                       [double]$Theta, [double]$CenterY,
                       [double]$RadialSize, [double]$Height,
                       [double]$TangentialSize) {
    $cosine = [Math]::Cos($Theta)
    $sine = [Math]::Sin($Theta)
    $center = New-Vec ($Radius * $cosine) $CenterY (-$Radius * $sine)
    $radial = New-Vec ($cosine * $RadialSize * 0.5) 0 (-$sine * $RadialSize * 0.5)
    $vertical = New-Vec 0 ($Height * 0.5) 0
    $tangent = New-Vec ($sine * $TangentialSize * 0.5) 0 ($cosine * $TangentialSize * 0.5)
    Add-BoxBasis $Model $Material $center $radial $vertical $tangent
}

function Add-CylinderX($Model, [int]$Material, [double]$CenterX,
                       [double]$CenterY, [double]$CenterZ,
                       [double]$Length, [double]$Radius, [int]$Sides) {
    $minimumX = $CenterX - $Length * 0.5
    $maximumX = $CenterX + $Length * 0.5
    $centerMinimum = New-Vec $minimumX $CenterY $CenterZ
    $centerMaximum = New-Vec $maximumX $CenterY $CenterZ
    for ($i = 0; $i -lt $Sides; ++$i) {
        $angle0 = 2.0 * [Math]::PI * $i / $Sides
        $angle1 = 2.0 * [Math]::PI * ($i + 1) / $Sides
        $minimum0 = New-Vec $minimumX ($CenterY + $Radius * [Math]::Cos($angle0)) ($CenterZ + $Radius * [Math]::Sin($angle0))
        $minimum1 = New-Vec $minimumX ($CenterY + $Radius * [Math]::Cos($angle1)) ($CenterZ + $Radius * [Math]::Sin($angle1))
        $maximum0 = New-Vec $maximumX $minimum0.Y $minimum0.Z
        $maximum1 = New-Vec $maximumX $minimum1.Y $minimum1.Z
        Add-Quad $Model $Material $minimum0 $minimum1 $maximum1 $maximum0
        Add-Triangle $Model $Material $centerMinimum $minimum1 $minimum0
        Add-Triangle $Model $Material $centerMaximum $maximum0 $maximum1
    }
}

function Add-StraightRailGeometry($Model) {
    foreach ($centerZ in @(-0.08, -0.29, -0.50, -0.71, -0.92)) {
        Add-ChamferedPrismY $Model 0 0.04 0.96 ($centerZ - 0.065) ($centerZ + 0.065) 0.00 0.075 0.018
        foreach ($centerX in @(0.32, 0.68)) {
            Add-Box $Model 2 $centerX 0.092 $centerZ 0.115 0.045 0.105
        }
    }
    Add-OctagonalRail $Model 1 0.32 -1.0 0.0 0.075 0.165 0.085 0.014
    Add-OctagonalRail $Model 1 0.68 -1.0 0.0 0.075 0.165 0.085 0.014
}

function New-StraightRailModel {
    $model = New-Model 'MinecartRailStraight'
    Add-StraightRailGeometry $model
    return $model
}

function New-StopRailModel {
    $model = New-Model 'MinecartRailStop'
    Add-StraightRailGeometry $model

    # A two-level square plate reads as a stop marker from the isometric
    # camera while remaining below the 0.165-unit rail tops.
    Add-ChamferedPrismY $model 2 0.375 0.625 -0.625 -0.375 0.076 0.130 0.025
    Add-ChamferedPrismY $model 3 0.420 0.580 -0.580 -0.420 0.130 0.160 0.018
    return $model
}

function New-CornerRailModel {
    $model = New-Model 'MinecartRailCorner'
    foreach ($degrees in @(20.0, 32.5, 45.0, 57.5, 70.0)) {
        $theta = $degrees * [Math]::PI / 180.0
        Add-RadialBox $model 0 0.50 $theta 0.0375 0.64 0.075 0.13
        foreach ($radius in @(0.32, 0.68)) {
            Add-RadialBox $model 2 $radius $theta 0.0975 0.115 0.045 0.105
        }
    }
    Add-CurvedRail $model 1 0.32 0.085 0.075 0.165 16
    Add-CurvedRail $model 1 0.68 0.085 0.075 0.165 16
    return $model
}

function New-HandcarModel {
    $model = New-Model 'MinecartHandcar'

    # Underframe and two axles use the same dark metal as the rail fasteners.
    Add-Box $model 2 0.32 0.205 -0.50 0.055 0.10 0.58
    Add-Box $model 2 0.68 0.205 -0.50 0.055 0.10 0.58
    foreach ($centerZ in @(-0.29, -0.71)) {
        Add-CylinderX $model 2 0.50 0.145 $centerZ 0.50 0.030 8
        foreach ($centerX in @(0.32, 0.68)) {
            Add-CylinderX $model 2 $centerX 0.145 $centerZ 0.072 0.105 12
            Add-CylinderX $model 1 $centerX 0.145 $centerZ 0.085 0.082 12
        }
    }

    # Five separated deck planks form an unobstructed cargo platform.
    foreach ($centerX in @(0.18, 0.34, 0.50, 0.66, 0.82)) {
        Add-ChamferedPrismY $model 0 ($centerX - 0.072) ($centerX + 0.072) -0.82 -0.18 0.245 0.335 0.014
    }
    Add-Box $model 2 0.50 0.255 -0.22 0.84 0.055 0.050
    Add-Box $model 2 0.50 0.255 -0.78 0.84 0.055 0.050
    return $model
}

function Align-Stream([System.IO.MemoryStream]$Stream, [System.IO.BinaryWriter]$Writer) {
    while (($Stream.Position % 4) -ne 0) {
        $Writer.Write([byte]0)
    }
}

function Add-FloatBufferView($Values, [System.IO.MemoryStream]$Stream,
                             [System.IO.BinaryWriter]$Writer,
                             [List[object]]$BufferViews, [int]$Target) {
    Align-Stream $Stream $Writer
    $offset = [int]$Stream.Position
    foreach ($value in $Values) {
        $Writer.Write([single]$value)
    }
    $length = [int]$Stream.Position - $offset
    $index = $BufferViews.Count
    $BufferViews.Add([ordered]@{
        buffer = 0
        byteOffset = $offset
        byteLength = $length
        target = $Target
    })
    return $index
}

function Add-IndexBufferView($Values, [System.IO.MemoryStream]$Stream,
                             [System.IO.BinaryWriter]$Writer,
                             [List[object]]$BufferViews) {
    Align-Stream $Stream $Writer
    $offset = [int]$Stream.Position
    foreach ($value in $Values) {
        $Writer.Write([uint32]$value)
    }
    $length = [int]$Stream.Position - $offset
    $index = $BufferViews.Count
    $BufferViews.Add([ordered]@{
        buffer = 0
        byteOffset = $offset
        byteLength = $length
        target = 34963
    })
    return $index
}

function Add-Accessor([List[object]]$Accessors, [int]$BufferView,
                      [int]$ComponentType, [int]$Count, [string]$Type,
                      $Minimum = $null, $Maximum = $null) {
    $accessor = [ordered]@{
        bufferView = $BufferView
        byteOffset = 0
        componentType = $ComponentType
        count = $Count
        type = $Type
    }
    if ($null -ne $Minimum) {
        $accessor.min = $Minimum
        $accessor.max = $Maximum
    }
    $index = $Accessors.Count
    $Accessors.Add($accessor)
    return $index
}

function Get-PositionBounds($Positions) {
    [single[]]$minimum = @([single]::PositiveInfinity, [single]::PositiveInfinity, [single]::PositiveInfinity)
    [single[]]$maximum = @([single]::NegativeInfinity, [single]::NegativeInfinity, [single]::NegativeInfinity)
    for ($i = 0; $i -lt $Positions.Count; $i += 3) {
        for ($axis = 0; $axis -lt 3; ++$axis) {
            $value = [single]$Positions[$i + $axis]
            $minimum[$axis] = [Math]::Min($minimum[$axis], $value)
            $maximum[$axis] = [Math]::Max($maximum[$axis], $value)
        }
    }
    return [pscustomobject]@{ Minimum = $minimum; Maximum = $maximum }
}

function Write-Glb($Model, $Materials, [string]$Path) {
    $binaryStream = [System.IO.MemoryStream]::new()
    $binaryWriter = [System.IO.BinaryWriter]::new($binaryStream)
    $bufferViews = [List[object]]::new()
    $accessors = [List[object]]::new()
    $meshPrimitives = [List[object]]::new()

    try {
        foreach ($materialIndex in @($Model.Primitives.Keys | Sort-Object)) {
            $primitive = $Model.Primitives[$materialIndex]
            if ($primitive.Indices.Count -eq 0) {
                continue
            }
            $bounds = Get-PositionBounds $primitive.Positions
            $positionView = Add-FloatBufferView $primitive.Positions $binaryStream $binaryWriter $bufferViews 34962
            $normalView = Add-FloatBufferView $primitive.Normals $binaryStream $binaryWriter $bufferViews 34962
            $uvView = Add-FloatBufferView $primitive.Uvs $binaryStream $binaryWriter $bufferViews 34962
            $indexView = Add-IndexBufferView $primitive.Indices $binaryStream $binaryWriter $bufferViews

            $positionAccessor = Add-Accessor $accessors $positionView 5126 ($primitive.Positions.Count / 3) 'VEC3' $bounds.Minimum $bounds.Maximum
            $normalAccessor = Add-Accessor $accessors $normalView 5126 ($primitive.Normals.Count / 3) 'VEC3'
            $uvAccessor = Add-Accessor $accessors $uvView 5126 ($primitive.Uvs.Count / 2) 'VEC2'
            $indexAccessor = Add-Accessor $accessors $indexView 5125 $primitive.Indices.Count 'SCALAR'

            $meshPrimitives.Add([ordered]@{
                attributes = [ordered]@{
                    POSITION = $positionAccessor
                    NORMAL = $normalAccessor
                    TEXCOORD_0 = $uvAccessor
                }
                indices = $indexAccessor
                material = [int]$materialIndex
                mode = 4
            })
        }
        Align-Stream $binaryStream $binaryWriter
        [byte[]]$binaryBytes = $binaryStream.ToArray()
    } finally {
        $binaryWriter.Dispose()
        $binaryStream.Dispose()
    }

    $document = [ordered]@{
        asset = [ordered]@{
            version = '2.0'
            generator = 'Sokoban minecart asset generator'
        }
        scene = 0
        scenes = @([ordered]@{ nodes = @(0) })
        nodes = @([ordered]@{ name = $Model.Name; mesh = 0 })
        meshes = @([ordered]@{
            name = $Model.Name
            primitives = @($meshPrimitives.ToArray())
        })
        materials = @($Materials)
        accessors = @($accessors.ToArray())
        bufferViews = @($bufferViews.ToArray())
        buffers = @([ordered]@{ byteLength = $binaryBytes.Length })
    }

    $json = $document | ConvertTo-Json -Depth 12 -Compress
    $utf8 = [System.Text.UTF8Encoding]::new($false)
    [byte[]]$jsonBytes = $utf8.GetBytes($json)
    $jsonPadding = (4 - ($jsonBytes.Length % 4)) % 4
    if ($jsonPadding -gt 0) {
        $padded = [byte[]]::new($jsonBytes.Length + $jsonPadding)
        [Array]::Copy($jsonBytes, $padded, $jsonBytes.Length)
        for ($i = $jsonBytes.Length; $i -lt $padded.Length; ++$i) {
            $padded[$i] = 0x20
        }
        $jsonBytes = $padded
    }

    $totalLength = 12 + 8 + $jsonBytes.Length + 8 + $binaryBytes.Length
    $file = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
    $writer = [System.IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint32]0x46546C67)
        $writer.Write([uint32]2)
        $writer.Write([uint32]$totalLength)
        $writer.Write([uint32]$jsonBytes.Length)
        $writer.Write([uint32]0x4E4F534A)
        $writer.Write($jsonBytes)
        $writer.Write([uint32]$binaryBytes.Length)
        $writer.Write([uint32]0x004E4942)
        $writer.Write($binaryBytes)
    } finally {
        $writer.Dispose()
        $file.Dispose()
    }
}

$materials = @(
    [ordered]@{
        name = 'Warm Wood'
        doubleSided = $true
        pbrMetallicRoughness = [ordered]@{
            baseColorFactor = @(0.72, 0.39, 0.14, 1.0)
            metallicFactor = 0.0
            roughnessFactor = 0.78
        }
    },
    [ordered]@{
        name = 'Gunmetal'
        doubleSided = $true
        pbrMetallicRoughness = [ordered]@{
            baseColorFactor = @(0.18, 0.24, 0.32, 1.0)
            metallicFactor = 0.72
            roughnessFactor = 0.34
        }
    },
    [ordered]@{
        name = 'Dark Iron'
        doubleSided = $true
        pbrMetallicRoughness = [ordered]@{
            baseColorFactor = @(0.075, 0.095, 0.12, 1.0)
            metallicFactor = 0.62
            roughnessFactor = 0.46
        }
    },
    [ordered]@{
        name = 'Stop Red'
        doubleSided = $true
        pbrMetallicRoughness = [ordered]@{
            baseColorFactor = @(1.0, 0.02, 0.015, 1.0)
            metallicFactor = 0.16
            roughnessFactor = 0.48
        }
        emissiveFactor = @(0.18, 0.0, 0.0)
    }
)

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

$outputs = @(
    @{ Model = New-StraightRailModel; File = 'minecart_rail_straight.glb' },
    @{ Model = New-StopRailModel; File = 'minecart_rail_stop.glb' },
    @{ Model = New-CornerRailModel; File = 'minecart_rail_corner.glb' },
    @{ Model = New-HandcarModel; File = 'minecart_handcar.glb' }
)

foreach ($output in $outputs) {
    $path = Join-Path $resolvedOutput $output.File
    Write-Glb $output.Model $materials $path
    $item = Get-Item -LiteralPath $path
    Write-Output ("Generated {0} ({1} bytes)" -f $item.FullName, $item.Length)
}
