#include "EarthImpactFeedback.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr int32 RingSegments = 32;
constexpr int32 TubeSegments = 5;
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
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingMaterialAsset(
        TEXT("/Game/Earthfall/Materials/M_EarthfallShockwave.M_EarthfallShockwave"));
    if (RingMaterialAsset.Succeeded())
    {
        RingMaterial = UMaterialInstanceDynamic::Create(RingMaterialAsset.Object, this);
        Ring->SetMaterial(0, RingMaterial);
    }
    BuildRing();
    OrbitalBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OrbitalBeam"));
    OrbitalBeam->SetupAttachment(Scene);
    OrbitalBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    OrbitalBeam->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (CylinderMesh.Succeeded()) OrbitalBeam->SetStaticMesh(CylinderMesh.Object);
    if (RingMaterial) RingMaterial->SetVectorParameterValue(TEXT("GlowColor"), AccentColor);
    if (RingMaterialAsset.Succeeded())
    {
        BeamMaterial = UMaterialInstanceDynamic::Create(RingMaterialAsset.Object, this);
        OrbitalBeam->SetMaterial(0, BeamMaterial);
        BeamMaterial->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(0.15f, 0.8f, 1.0f));
    }
    SetLifeSpan(Lifetime);
}

void AEarthImpactFeedback::InitFeedback(float ImpactRadius, FLinearColor InAccentColor, float InLifetime, bool bShowBeam, float FlashIntensity, bool bInImplosion)
{
    AccentColor = InAccentColor;
    Lifetime = FMath::Max(0.1f, InLifetime);
    PeakFlashIntensity = FlashIntensity;
    ExpansionRadius = ImpactRadius * 3.2f;
    Age = 0.0f;
    bImplosion = bInImplosion;
    SetLifeSpan(Lifetime);
    Flash->SetLightColor(AccentColor);
    Flash->SetIntensity(FlashIntensity);
    if (RingMaterial) RingMaterial->SetVectorParameterValue(TEXT("GlowColor"), AccentColor);
    if (bShowBeam)
    {
        OrbitalBeam->SetVisibility(true);
        OrbitalBeam->SetRelativeLocation(FVector(0, 0, 70.0f));
        OrbitalBeam->SetRelativeScale3D(FVector(0.075f, 0.075f, 1.4f));
        if (BeamMaterial) BeamMaterial->SetVectorParameterValue(TEXT("GlowColor"), AccentColor);
    }
}

void AEarthImpactFeedback::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    const float Alpha = FMath::Clamp(Age / Lifetime, 0.0f, 1.0f);
    const float Fade = 1.0f - Alpha;
    const float RadiusScale = FMath::Lerp(0.035f, ExpansionRadius / 90.0f, bImplosion ? 1.0f - Alpha : Alpha);
    Ring->SetRelativeScale3D(FVector(RadiusScale));
    if (RingMaterial) RingMaterial->SetScalarParameterValue(TEXT("Opacity"), Fade);
    Flash->SetIntensity(FMath::Lerp(PeakFlashIntensity, 0.0f, Alpha));
    if (OrbitalBeam->IsVisible() && BeamMaterial) BeamMaterial->SetScalarParameterValue(TEXT("Opacity"), Fade * 0.8f);
}

void AEarthImpactFeedback::BuildRing()
{
    const float Radius = 90.0f;
    const float TubeRadius = 2.2f;
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
            Colors.Add(FLinearColor::White);
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
