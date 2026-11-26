#include "EarthfallPlanet.h"
#include "EarthImpactFeedback.h"
#include "ProceduralMeshComponent.h"

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
            VertexColors.Add(FLinearColor(0.07f, 0.22f, 0.36f, 1.0f));
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
        Normals[i] = DeformedVertices[i].GetSafeNormal();
        const float Heat = SmoothFalloff;
        VertexColors[i] = FLinearColor(0.07f + Heat * 0.8f, 0.22f - Heat * 0.17f, 0.36f - Heat * 0.30f, 1.0f);
    }

    // Recreate the section so the collision used by the next click trace follows the crater.
    PlanetMesh->CreateMeshSection_LinearColor(0, DeformedVertices, Triangles, Normals, UVs, VertexColors, Tangents, true);

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<AEarthImpactFeedback>(
        AEarthImpactFeedback::StaticClass(), SurfacePoint, FRotationMatrix::MakeFromZ(LocalDirection).Rotator(), Params);
}
