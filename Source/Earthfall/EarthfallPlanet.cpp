#include "EarthfallPlanet.h"
#include "EarthImpactFeedback.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 LatitudeSegments = 64;
constexpr int32 LongitudeSegments = 128;
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
    if (SurfaceMaterial.Succeeded()) PlanetMesh->SetMaterial(0, SurfaceMaterial.Object);
}

void AEarthfallPlanet::BeginPlay()
{
    Super::BeginPlay();
    BuildPlanetMesh();
}

void AEarthfallPlanet::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AddActorLocalRotation(FRotator(0.0f, RotationDegreesPerSecond * DeltaSeconds, 0.0f));
    HeatUpdateAccumulator += DeltaSeconds;
    if (HeatUpdateAccumulator >= 0.1f)
    {
        const float HeatStep = HeatUpdateAccumulator;
        HeatUpdateAccumulator = 0.0f;
        bool bHeatChanged = false;
        for (int32 i = 0; i < HeatValues.Num(); ++i)
        {
            if (HeatValues[i] <= 0.0f) continue;
            HeatValues[i] = FMath::Max(0.0f, HeatValues[i] - HeatStep * 0.18f);
            VertexColors[i] = FLinearColor::LerpUsingHSV(BaseVertexColors[i], VertexColors[i], HeatValues[i]);
            VertexColors[i].A = HeatValues[i];
            bHeatChanged = true;
        }
        if (bHeatChanged)
            PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);
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
            const float Noise = 0.62f * FMath::PerlinNoise3D(Direction * 3.8f)
                              + 0.26f * FMath::PerlinNoise3D(Direction * 8.1f)
                              + 0.12f * FMath::PerlinNoise3D(Direction * 16.7f);
            const float Polar = FMath::Abs(Direction.Z);
            FLinearColor SurfaceColor(0.004f, 0.022f, 0.075f, 1.0f);
            if (Polar > 0.88f) SurfaceColor = FLinearColor(0.18f, 0.28f, 0.32f, 1.0f);
            else if (Noise > 0.02f && Noise < 0.08f) SurfaceColor = FLinearColor(0.20f, 0.12f, 0.035f, 1.0f);
            else if (Noise >= 0.08f) SurfaceColor = Polar > 0.68f
                ? FLinearColor(0.13f, 0.18f, 0.08f, 1.0f)
                : FLinearColor(0.018f, 0.075f, 0.028f, 1.0f);
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
}

void AEarthfallPlanet::ResetPlanet()
{
    BuildPlanetMesh();
}

void AEarthfallPlanet::ApplyImpact(FVector WorldLocation, float Radius, float Energy, int32 WeaponIndex)
{
    const FVector LocalDirection = GetActorTransform().InverseTransformVectorNoScale(WorldLocation - GetActorLocation()).GetSafeNormal();
    const FVector SurfacePoint = WorldLocation;
    const float SafeRadius = FMath::Clamp(Radius, 2.0f, PlanetRadius * 0.45f);
    const float Depth = FMath::Clamp(Energy * 0.12f, 2.0f, PlanetRadius * 0.2f);
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
        HeatValues[i] = FMath::Max(HeatValues[i], Heat);
        const FLinearColor BaseColor = BaseVertexColors[i];
        const bool bOcean = BaseColor.B > BaseColor.G * 1.6f && BaseColor.B > BaseColor.R * 1.8f;
        const bool bPolar = BaseColor.R > 0.10f && BaseColor.G > 0.16f && BaseColor.B > 0.18f;
        const FLinearColor Scorch = bOcean
            ? FLinearColor(0.16f, 0.055f, 0.025f, 1.0f)
            : (bPolar ? FLinearColor(0.25f, 0.12f, 0.045f, 1.0f) : FLinearColor(0.055f, 0.025f, 0.012f, 1.0f));
        const FLinearColor HotCore(1.0f, 0.19f, 0.018f, 1.0f);
        VertexColors[i] = FLinearColor::LerpUsingHSV(BaseColor, Scorch, Heat * 0.82f);
        VertexColors[i] = FLinearColor::LerpUsingHSV(VertexColors[i], HotCore, Heat * 0.58f);
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

    // Update the existing section and refresh collision for subsequent aiming traces.
    PlanetMesh->UpdateMeshSection_LinearColor(0, DeformedVertices, Normals, UVs, VertexColors, Tangents);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AEarthImpactFeedback* Feedback = GetWorld()->SpawnActor<AEarthImpactFeedback>(
        AEarthImpactFeedback::StaticClass(), SurfacePoint, FRotationMatrix::MakeFromZ(LocalDirection).Rotator(), Params);
    if (Feedback)
    {
        static const FLinearColor Colors[] = {
            FLinearColor(1.0f, 0.66f, 0.34f),
            FLinearColor(0.2f, 0.86f, 1.0f),
            FLinearColor(1.0f, 0.22f, 0.035f)
        };
        static const float Lifetimes[] = {0.42f, 0.82f, 1.45f};
        static const float FlashIntensities[] = {18000.0f, 26000.0f, 38000.0f};
        const int32 SafeWeapon = FMath::Clamp(WeaponIndex, 0, 2);
        Feedback->InitFeedback(SafeRadius, Colors[SafeWeapon], Lifetimes[SafeWeapon], SafeWeapon == 1, FlashIntensities[SafeWeapon]);
    }
}
