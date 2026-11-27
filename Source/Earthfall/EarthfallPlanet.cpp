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
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (VertexColorMaterial.Succeeded()) PlanetMesh->SetMaterial(0, VertexColorMaterial.Object);
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
}

void AEarthfallPlanet::BuildPlanetMesh()
{
    BaseVertices.Reset();
    DeformedVertices.Reset();
    Triangles.Reset();
    Normals.Reset();
    UVs.Reset();
    VertexColors.Reset();
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
            VertexColors.Add(SurfaceColor);
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

void AEarthfallPlanet::ApplyImpact(FVector WorldLocation, float Radius, float Energy)
{
    const FVector LocalDirection = GetActorTransform().InverseTransformVectorNoScale(WorldLocation - GetActorLocation()).GetSafeNormal();
    const FVector SurfacePoint = GetActorLocation() + GetActorTransform().TransformVectorNoScale(LocalDirection * PlanetRadius);
    const float SafeRadius = FMath::Clamp(Radius, 2.0f, PlanetRadius * 0.45f);
    const float Depth = FMath::Clamp(Energy * 0.12f, 2.0f, PlanetRadius * 0.2f);
    const float CosAngle = FMath::Cos(SafeRadius / PlanetRadius);

    for (int32 i = 0; i < BaseVertices.Num(); ++i)
    {
        const FVector Direction = BaseVertices[i].GetSafeNormal();
        const float Dot = FVector::DotProduct(Direction, LocalDirection);
        if (Dot <= CosAngle) continue;

        const float Falloff = FMath::Clamp((Dot - CosAngle) / (1.0f - CosAngle), 0.0f, 1.0f);
        const float SmoothFalloff = Falloff * Falloff * (3.0f - 2.0f * Falloff);
        const float NewRadius = FMath::Max(PlanetRadius * 0.55f, BaseVertices[i].Size() - Depth * SmoothFalloff);
        DeformedVertices[i] = Direction * NewRadius;
        const float Heat = SmoothFalloff;
        VertexColors[i] = FLinearColor(0.018f + Heat * 0.78f, 0.075f - Heat * 0.06f, 0.028f - Heat * 0.02f, 1.0f);
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

    // Recreate the section so the collision used by the next click trace follows the crater.
    PlanetMesh->CreateMeshSection_LinearColor(0, DeformedVertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<AEarthImpactFeedback>(
        AEarthImpactFeedback::StaticClass(), SurfacePoint, FRotationMatrix::MakeFromZ(LocalDirection).Rotator(), Params);
}
