#include "EarthImpactFeedback.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 RingSegments = 48;
constexpr int32 TubeSegments = 6;
constexpr float RingLifetime = 1.1f;
}

AEarthImpactFeedback::AEarthImpactFeedback()
{
    PrimaryActorTick.bCanEverTick = true;
    auto* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Scene);
    Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("ImpactFlash"));
    Flash->SetupAttachment(Scene);
    Flash->Intensity = 22000.0f;
    Flash->AttenuationRadius = 450.0f;
    Flash->SetLightColor(FLinearColor(1.0f, 0.22f, 0.04f));
    Flash->SetCastShadows(false);

    Ring = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ShockwaveRing"));
    Ring->SetupAttachment(Scene);
    Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ring->SetGenerateOverlapEvents(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (VertexColorMaterial.Succeeded()) Ring->SetMaterial(0, VertexColorMaterial.Object);
    UpdateRing(0.0f);
    SetLifeSpan(RingLifetime);
}

void AEarthImpactFeedback::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    const float Alpha = FMath::Clamp(Age / RingLifetime, 0.0f, 1.0f);
    UpdateRing(Alpha);
    Flash->SetIntensity(FMath::Lerp(22000.0f, 0.0f, Alpha));
}

void AEarthImpactFeedback::UpdateRing(float Alpha)
{
    const float Radius = FMath::Lerp(5.0f, 90.0f, Alpha);
    const float TubeRadius = FMath::Lerp(3.2f, 1.0f, Alpha);
    const float Glow = 1.0f - Alpha;
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    for (int32 Major = 0; Major <= RingSegments; ++Major)
    {
        const float U = static_cast<float>(Major) / RingSegments;
        const float Theta = U * 2.0f * PI;
        const FVector Radial(FMath::Cos(Theta), FMath::Sin(Theta), 0.0f);
        for (int32 Minor = 0; Minor <= TubeSegments; ++Minor)
        {
            const float V = static_cast<float>(Minor) / TubeSegments;
            const float Phi = V * 2.0f * PI;
            const FVector Normal = Radial * FMath::Cos(Phi) + FVector::UpVector * FMath::Sin(Phi);
            Vertices.Add(Radial * (Radius + TubeRadius * FMath::Cos(Phi)) + FVector::UpVector * (TubeRadius * FMath::Sin(Phi)));
            Normals.Add(Normal);
            UVs.Add(FVector2D(U, V));
            Colors.Add(FLinearColor(Glow, Glow * 0.22f, Glow * 0.025f, 1.0f));
            Tangents.Add(FProcMeshTangent(-FMath::Sin(Theta), FMath::Cos(Theta), 0.0f));
        }
    }
    for (int32 Major = 0; Major < RingSegments; ++Major)
    {
        for (int32 Minor = 0; Minor < TubeSegments; ++Minor)
        {
            const int32 A = Major * (TubeSegments + 1) + Minor;
            const int32 B = A + TubeSegments + 1;
            Indices.Append({A, B, A + 1, A + 1, B, B + 1});
        }
    }
    Ring->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UVs, Colors, Tangents, false);
}
