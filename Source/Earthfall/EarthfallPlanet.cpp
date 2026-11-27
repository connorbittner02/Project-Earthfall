#include "EarthfallPlanet.h"
#include "EarthImpactFeedback.h"
#include "EarthfallPopulationSubsystem.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 LatitudeSegments = 128;
constexpr int32 LongitudeSegments = 256;
constexpr int32 SurfaceTextureWidth = 2048;
constexpr int32 SurfaceTextureHeight = 1024;
}

AEarthfallPlanet::AEarthfallPlanet()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    PlanetMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Planet"));
    PlanetMesh->SetupAttachment(Root);
    PlanetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    PlanetMesh->SetCollisionObjectType(ECC_WorldStatic);
    PlanetMesh->bUseAsyncCooking = true;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SurfaceMaterial(
        TEXT("/Game/Earthfall/Materials/M_EarthfallSurface.M_EarthfallSurface"));
    SurfaceMaterialInstance = SurfaceMaterial.Succeeded()
        ? UMaterialInstanceDynamic::Create(SurfaceMaterial.Object, this) : nullptr;
    if (SurfaceMaterialInstance)
    {
        PlanetMesh->SetMaterial(0, SurfaceMaterialInstance);
    }
}

void AEarthfallPlanet::BeginPlay()
{
    Super::BeginPlay();
    BuildPlanetMesh();
    BuildAtmosphereAndCloudShells();
}

void AEarthfallPlanet::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AddActorLocalRotation(FRotator(0.0f, RotationDegreesPerSecond * DeltaSeconds, 0.0f));
    CloudUpdateAccumulator += DeltaSeconds;
    if (CloudUpdateAccumulator >= 0.15f)
    {
        const float CloudStep = CloudUpdateAccumulator;
        CloudUpdateAccumulator = 0.0f;
        bool bCloudsRecovered = false;
        for (int32 Index = 0; Index < CloudDensity.Num(); ++Index)
        {
            float& Density = CloudDensity[Index];
            if (Density < BaseCloudDensity[Index])
            {
                Density = FMath::Min(BaseCloudDensity[Index], Density + CloudStep * 0.025f);
                const float CloudCoverage = FMath::SmoothStep(0.42f, 0.62f, Density);
                CloudVertices[Index] = BaseVertices[Index].GetSafeNormal() * PlanetRadius * FMath::Lerp(0.98f, 1.012f, CloudCoverage);
                bCloudsRecovered = true;
            }
        }
        if (bCloudsRecovered && CloudLayer && CloudColors.Num() == CloudDensity.Num())
        {
            for (int32 i = 0; i < CloudColors.Num(); ++i) CloudColors[i].A = CloudDensity[i] * 0.12f;
            CloudLayer->UpdateMeshSection_LinearColor(0, CloudVertices, Normals, UVs, CloudColors, Tangents);
        }
    }
    HeatUpdateAccumulator += DeltaSeconds;
    if (HeatUpdateAccumulator >= 0.1f)
    {
        const float HeatStep = HeatUpdateAccumulator;
        HeatUpdateAccumulator = 0.0f;
        bool bHeatChanged = false;
        for (int32 i = 0; i < HeatValues.Num(); ++i)
        {
            if (FMath::IsNearlyZero(HeatValues[i])) continue;
            HeatValues[i] = HeatValues[i] > 0.0f
                ? FMath::Max(0.0f, HeatValues[i] - HeatStep * 0.18f)
                : FMath::Min(0.0f, HeatValues[i] + HeatStep * 0.18f);
            bHeatChanged = true;
        }
        if (bHeatChanged)
        {
            RefreshSurfaceVertexColors();
            PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);
        }
    }
}

void AEarthfallPlanet::BuildPlanetMesh()
{
    BaseVertices.Reset();
    DeformedVertices.Reset();
    Triangles.Reset();
    Normals.Reset();
    UVs.Reset();
    VertexColors.Reset();
    BaseVertexColors.Reset();
    HeatValues.Reset();
    Tangents.Reset();
    if (!SurfaceTexture) BuildProceduralSurfaceTexture();

    for (int32 Lat = 0; Lat <= LatitudeSegments; ++Lat)
    {
        const float V = static_cast<float>(Lat) / LatitudeSegments;
        const float Phi = V * PI;
        for (int32 Lon = 0; Lon <= LongitudeSegments; ++Lon)
        {
            const float U = static_cast<float>(Lon) / LongitudeSegments;
            const float Theta = U * 2.0f * PI;
            const FVector Direction(FMath::Sin(Phi) * FMath::Cos(Theta),
                                    FMath::Sin(Phi) * FMath::Sin(Theta),
                                    FMath::Cos(Phi));
            BaseVertices.Add(Direction * PlanetRadius);
            UVs.Add(FVector2D(U, V));
            const int32 TextureX = FMath::Clamp(FMath::RoundToInt(U * (SurfaceTextureWidth - 1)), 0, SurfaceTextureWidth - 1);
            const int32 TextureY = FMath::Clamp(FMath::RoundToInt(V * (SurfaceTextureHeight - 1)), 0, SurfaceTextureHeight - 1);
            const FColor Texel = SurfaceTexturePixels[TextureY * SurfaceTextureWidth + TextureX];
            FLinearColor SurfaceColor(Texel.R / 255.0f, Texel.G / 255.0f, Texel.B / 255.0f, 1.0f);
            SurfaceColor.A = 0.0f;
            BaseVertexColors.Add(SurfaceColor);
            VertexColors.Add(SurfaceColor);
            HeatValues.Add(0.0f);
            Tangents.Add(FProcMeshTangent(-FMath::Sin(Theta), FMath::Cos(Theta), 0.0f));
        }
    }

    for (int32 Lat = 0; Lat < LatitudeSegments; ++Lat)
    {
        for (int32 Lon = 0; Lon < LongitudeSegments; ++Lon)
        {
            const int32 A = Lat * (LongitudeSegments + 1) + Lon;
            const int32 B = A + LongitudeSegments + 1;
            Triangles.Append({A, B, A + 1, A + 1, B, B + 1});
        }
    }

    DeformedVertices = BaseVertices;
    Normals.SetNumUninitialized(DeformedVertices.Num());
    for (int32 i = 0; i < DeformedVertices.Num(); ++i) Normals[i] = DeformedVertices[i].GetSafeNormal();
    PlanetMesh->CreateMeshSection_LinearColor(0, DeformedVertices, Triangles, Normals, UVs, VertexColors, Tangents, true);
    RefreshSurfaceVertexColors();
    PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);
}

void AEarthfallPlanet::BuildProceduralSurfaceTexture()
{
    SurfaceTexturePixels.SetNumUninitialized(SurfaceTextureWidth * SurfaceTextureHeight);
    for (int32 Y = 0; Y < SurfaceTextureHeight; ++Y)
    {
        const float V = static_cast<float>(Y) / (SurfaceTextureHeight - 1);
        const float Phi = V * PI;
        for (int32 X = 0; X < SurfaceTextureWidth; ++X)
        {
            const float U = static_cast<float>(X) / (SurfaceTextureWidth - 1);
            const float Theta = U * 2.0f * PI;
            const FVector Dir(FMath::Sin(Phi) * FMath::Cos(Theta), FMath::Sin(Phi) * FMath::Sin(Theta), FMath::Cos(Phi));
            const float N = 0.58f * FMath::PerlinNoise3D(Dir * 3.6f) + 0.27f * FMath::PerlinNoise3D(Dir * 8.8f)
                + 0.11f * FMath::PerlinNoise3D(Dir * 18.0f) + 0.04f * FMath::PerlinNoise3D(Dir * 37.0f);
            const float Land = FMath::SmoothStep(-0.035f, 0.09f, N);
            const float Ridge = FMath::SmoothStep(0.34f, 0.58f, N);
            const float Ice = FMath::SmoothStep(0.70f, 0.78f, FMath::Abs(static_cast<float>(Dir.Z))) * FMath::Clamp(0.62f + N * 0.12f, 0.0f, 0.68f);
            const FLinearColor Ocean = FLinearColor(0.006f, 0.035f, 0.12f, 1.0f) * FMath::Clamp(0.45f + N * 0.7f, 0.16f, 0.78f);
            const FLinearColor Ground = FLinearColor::LerpUsingHSV(FLinearColor(0.12f, 0.16f, 0.075f), FLinearColor(0.27f, 0.23f, 0.16f), Ridge);
            const FLinearColor Color = FMath::Lerp(FMath::Lerp(Ocean, Ground, Land), FLinearColor(0.62f, 0.66f, 0.68f), Ice);
            SurfaceTexturePixels[Y * SurfaceTextureWidth + X] = Color.ToFColor(false);
        }
    }
    SurfaceTexture = UTexture2D::CreateTransient(SurfaceTextureWidth, SurfaceTextureHeight, PF_B8G8R8A8);
    if (!SurfaceTexture) return;
    SurfaceTexture->Filter = TF_Bilinear;
    SurfaceTexture->AddressX = TA_Wrap;
    SurfaceTexture->AddressY = TA_Clamp;
    FTexturePlatformData* PlatformData = SurfaceTexture->GetPlatformData();
    if (!PlatformData || PlatformData->Mips.IsEmpty()) return;
    FTexture2DMipMap& Mip = PlatformData->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(Data, SurfaceTexturePixels.GetData(), SurfaceTexturePixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    SurfaceTexture->UpdateResource();
}

void AEarthfallPlanet::BuildAtmosphereAndCloudShells()
{
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Earthfall/Materials/M_EarthfallShockwave.M_EarthfallShockwave"));
    if (Sphere && !AtmosphereShell)
    {
        AtmosphereShell = NewObject<UStaticMeshComponent>(this, TEXT("AtmosphereShell"));
        AtmosphereShell->SetupAttachment(Root);
        AtmosphereShell->SetStaticMesh(Sphere);
        AtmosphereShell->SetRelativeScale3D(FVector(2.02f));
        AtmosphereShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Glow)
        {
            AtmosphereMaterialInstance = UMaterialInstanceDynamic::Create(Glow, this);
            AtmosphereShell->SetMaterial(0, AtmosphereMaterialInstance);
            AtmosphereMaterialInstance->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(0.08f, 0.28f, 0.72f));
            AtmosphereMaterialInstance->SetScalarParameterValue(TEXT("Opacity"), 0.025f);
        }
        AtmosphereShell->RegisterComponent();
    }
    if (!CloudLayer)
    {
        CloudLayer = NewObject<UProceduralMeshComponent>(this, TEXT("CloudLayer"));
        CloudLayer->SetupAttachment(Root);
        CloudLayer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CloudLayer->RegisterComponent();
    }
    CloudDensity.SetNumZeroed(BaseVertices.Num());
    BaseCloudDensity.SetNumZeroed(BaseVertices.Num());
    CloudVertices = BaseVertices;
    for (FVector& Vertex : CloudVertices) Vertex = Vertex.GetSafeNormal() * PlanetRadius * 1.012f;
    for (int32 i = 0; i < CloudVertices.Num(); ++i)
    {
        const FVector D = CloudVertices[i].GetSafeNormal();
        BaseCloudDensity[i] = FMath::Clamp(0.5f + 0.5f * FMath::PerlinNoise3D(D * 24.0f), 0.0f, 1.0f);
        CloudDensity[i] = BaseCloudDensity[i];
        const float CloudCoverage = FMath::SmoothStep(0.42f, 0.62f, CloudDensity[i]);
        CloudVertices[i] = D * PlanetRadius * FMath::Lerp(0.98f, 1.012f, CloudCoverage);
    }
    for (int32 i = 0; i < CloudColors.Num(); ++i) CloudColors[i].A = CloudDensity[i] * 0.12f;
    if (CloudLayer && CloudLayer->GetProcMeshSection(0))
        CloudLayer->UpdateMeshSection_LinearColor(0, CloudVertices, Normals, UVs, CloudColors, Tangents);
    CloudColors.SetNumUninitialized(CloudVertices.Num());
    for (int32 i = 0; i < CloudColors.Num(); ++i) CloudColors[i] = FLinearColor(0.72f, 0.82f, 0.92f, CloudDensity[i] * 0.12f);
    CloudLayer->CreateMeshSection_LinearColor(0, CloudVertices, Triangles, Normals, UVs, CloudColors, Tangents, false);
    if (Glow)
    {
        CloudMaterialInstance = UMaterialInstanceDynamic::Create(Glow, this);
        CloudMaterialInstance->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(0.32f, 0.42f, 0.58f));
        CloudMaterialInstance->SetScalarParameterValue(TEXT("Opacity"), 0.025f);
        CloudLayer->SetMaterial(0, CloudMaterialInstance);
    }
}

void AEarthfallPlanet::ClearCloudsAtImpact(const FVector& LocalDirection, float Radius)
{
    const float CosRadius = FMath::Cos(Radius / PlanetRadius * 1.5f);
    for (int32 i = 0; i < BaseVertices.Num(); ++i)
    {
        if (FVector::DotProduct(BaseVertices[i].GetSafeNormal(), LocalDirection) > CosRadius)
        {
            CloudDensity[i] = 0.0f;
            CloudVertices[i] = BaseVertices[i].GetSafeNormal() * PlanetRadius * 0.98f;
        }
    }
    for (int32 i = 0; i < CloudColors.Num(); ++i) CloudColors[i].A = CloudDensity[i] * 0.12f;
    CloudLayer->UpdateMeshSection_LinearColor(0, CloudVertices, Normals, UVs, CloudColors, Tangents);
}

void AEarthfallPlanet::RefreshSurfaceVertexColors()
{
    UEarthfallPopulationSubsystem* Population = GetWorld()
        ? GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>() : nullptr;
    for (int32 i = 0; i < VertexColors.Num(); ++i)
    {
        FLinearColor Color = BaseVertexColors[i];
        const float CityLight = Population ? Population->GetCityLightAtDirection(BaseVertices[i].GetSafeNormal()) : 0.0f;
        Color += FLinearColor(0.28f, 0.12f, 0.025f, 0.0f) * CityLight;
        const float Heat = HeatValues[i];
        if (Heat > 0.0f)
        {
            const FLinearColor HeatColor(1.0f, 0.17f, 0.025f, 1.0f);
            Color = FLinearColor::LerpUsingHSV(Color, HeatColor, Heat);
        }
        else if (Heat < 0.0f)
        {
            Color = FLinearColor::LerpUsingHSV(Color, FLinearColor(0.55f, 0.86f, 1.0f, 1.0f), -Heat);
        }
        Color.A = FMath::Max(CityLight * 0.55f, FMath::Abs(Heat));
        VertexColors[i] = Color;
    }
}

void AEarthfallPlanet::ResetPlanet()
{
    if (UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>()) Population->ResetSimulation();
    BuildPlanetMesh();
    if (CloudLayer) BuildAtmosphereAndCloudShells();
}

void AEarthfallPlanet::ApplyImpact(FVector WorldLocation, float Radius, float Energy, int32 WeaponIndex)
{
    const FVector LocalDirection = GetActorTransform().InverseTransformVectorNoScale(WorldLocation - GetActorLocation()).GetSafeNormal();
    ClearCloudsAtImpact(LocalDirection, Radius);
    if (UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>())
    {
        Population->ApplyStrike(LocalDirection, Radius / PlanetRadius, Energy, WeaponIndex);
    }
    const FVector WorldNormal = GetActorTransform().TransformVectorNoScale(LocalDirection).GetSafeNormal();
    const FVector SurfacePoint = WorldLocation;
    const float SafeRadius = FMath::Clamp(Radius, 2.0f, PlanetRadius * 0.45f);
    const float WeaponDepthScale[] = {1.0f, 0.72f, 1.38f, 1.55f, 0.5f};
    const float Depth = FMath::Clamp(Energy * 0.12f * WeaponDepthScale[FMath::Clamp(WeaponIndex, 0, 4)], 2.0f, PlanetRadius * 0.28f);
    const float CosAngle = FMath::Cos(SafeRadius / PlanetRadius);

    for (int32 i = 0; i < BaseVertices.Num(); ++i)
    {
        const FVector Direction = DeformedVertices[i].GetSafeNormal();
        const float Dot = FVector::DotProduct(Direction, LocalDirection);
        if (Dot <= CosAngle) continue;

        const float Falloff = FMath::Clamp((Dot - CosAngle) / (1.0f - CosAngle), 0.0f, 1.0f);
        const float SmoothFalloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);
        const float AngularDistance = FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f));
        const float NormalizedDistance = AngularDistance / (SafeRadius / PlanetRadius);
        const float RimIn = FMath::SmoothStep(0.72f, 0.88f, NormalizedDistance);
        const float RimOut = 1.0f - FMath::SmoothStep(0.94f, 1.08f, NormalizedDistance);
        const float RimLift = RimIn * RimOut * Depth * 0.12f;
        const float NewRadius = FMath::Max(PlanetRadius * 0.55f,
            DeformedVertices[i].Size() - Depth * SmoothFalloff + RimLift);
        DeformedVertices[i] = Direction * NewRadius;
        const float Heat = SmoothFalloff;
        HeatValues[i] = WeaponIndex == 4 ? FMath::Min(HeatValues[i], -Heat) : FMath::Max(HeatValues[i], Heat);
        const FLinearColor BaseColor = BaseVertexColors[i];
        const bool bOcean = BaseColor.B > BaseColor.G * 1.6f && BaseColor.B > BaseColor.R * 1.8f;
        const bool bPolar = BaseColor.R > 0.10f && BaseColor.G > 0.16f && BaseColor.B > 0.18f;
        const FLinearColor Scorch = bOcean
            ? FLinearColor(0.16f, 0.055f, 0.025f, 1.0f)
            : (bPolar ? FLinearColor(0.25f, 0.12f, 0.045f, 1.0f) : FLinearColor(0.055f, 0.025f, 0.012f, 1.0f));
        const FLinearColor ColdCore(0.62f, 0.88f, 1.0f, 1.0f);
        const FLinearColor HotCore(1.0f, 0.19f, 0.018f, 1.0f);
        VertexColors[i] = FLinearColor::LerpUsingHSV(BaseColor, Scorch, Heat * 0.82f);
        VertexColors[i] = FLinearColor::LerpUsingHSV(VertexColors[i], WeaponIndex == 4 ? ColdCore : HotCore, Heat * 0.58f);
        VertexColors[i].A = HeatValues[i];
    }

    for (FVector& Normal : Normals) Normal = FVector::ZeroVector;
    for (int32 i = 0; i < Triangles.Num(); i += 3)
    {
        const int32 A = Triangles[i];
        const int32 B = Triangles[i + 1];
        const int32 C = Triangles[i + 2];
        const FVector FaceNormal = FVector::CrossProduct(
            DeformedVertices[B] - DeformedVertices[A], DeformedVertices[C] - DeformedVertices[A]);
        Normals[A] += FaceNormal;
        Normals[B] += FaceNormal;
        Normals[C] += FaceNormal;
    }
    for (int32 i = 0; i < Normals.Num(); ++i)
    {
        Normals[i] = Normals[i].IsNearlyZero() ? BaseVertices[i].GetSafeNormal() : Normals[i].GetSafeNormal();
    }

    RefreshSurfaceVertexColors();
    // Update the existing section and refresh collision for subsequent aiming traces.
    PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AEarthImpactFeedback* Feedback = GetWorld()->SpawnActor<AEarthImpactFeedback>(
        AEarthImpactFeedback::StaticClass(), SurfacePoint, FRotationMatrix::MakeFromZ(WorldNormal).Rotator(), Params);
    if (Feedback)
    {
        static const FLinearColor Colors[] = {
            FLinearColor(1.0f, 0.66f, 0.34f),
            FLinearColor(0.2f, 0.86f, 1.0f),
            FLinearColor(1.0f, 0.22f, 0.035f),
            FLinearColor(0.55f, 0.22f, 0.95f),
            FLinearColor(0.55f, 0.88f, 1.0f)
        };
        static const float Lifetimes[] = {0.42f, 0.82f, 1.45f, 1.25f, 1.1f};
        static const float FlashIntensities[] = {18000.0f, 26000.0f, 38000.0f, 30000.0f, 22000.0f};
        const int32 SafeWeapon = FMath::Clamp(WeaponIndex, 0, 4);
        Feedback->InitFeedback(SafeRadius, Colors[SafeWeapon], Lifetimes[SafeWeapon], SafeWeapon == 1, FlashIntensities[SafeWeapon], SafeWeapon == 3);
    }
}
