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

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingMaterialAsset(
        TEXT("/Game/Earthfall/Materials/M_EarthfallShockwave.M_EarthfallShockwave"));
    Ring = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ShockwaveRing"));
    Ring->SetupAttachment(Scene);
    Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ring->SetGenerateOverlapEvents(false);
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
    ThermalPlume = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThermalPlume"));
    ThermalPlume->SetupAttachment(Scene);
    ThermalPlume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ThermalPlume->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PlumeMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (PlumeMesh.Succeeded()) ThermalPlume->SetStaticMesh(PlumeMesh.Object);
    if (RingMaterial) RingMaterial->SetVectorParameterValue(TEXT("GlowColor"), AccentColor);
    if (RingMaterialAsset.Succeeded())
    {
        BeamMaterial = UMaterialInstanceDynamic::Create(RingMaterialAsset.Object, this);
        OrbitalBeam->SetMaterial(0, BeamMaterial);
        BeamMaterial->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(0.15f, 0.8f, 1.0f));
    }
    SetLifeSpan(Lifetime);
}

void AEarthImpactFeedback::InitFeedback(float ImpactRadius, FLinearColor InAccentColor, float InLifetime, bool bShowBeam, float FlashIntensity, bool bInImplosion, bool bInNuclear)
{
    AccentColor = InAccentColor;
    Lifetime = FMath::Max(0.1f, InLifetime);
    PeakFlashIntensity = FlashIntensity;
    ExpansionRate = 1.0f;
    ExpansionRadius = ImpactRadius * 3.2f;
    Age = 0.0f;
    bImplosion = bInImplosion;
    bNuclear = bInNuclear;
    if (bNuclear)
    {
        ExpansionRadius = ImpactRadius * 13.0f;
        ExpansionRate = 1.0f;
        AccentColor = FLinearColor(1.0f, 0.36f, 0.08f);
    }
    SetLifeSpan(Lifetime);
    Flash->SetLightColor(AccentColor);
    Flash->SetIntensity(FlashIntensity);
    if (RingMaterial) RingMaterial->SetVectorParameterValue(TEXT("GlowColor"), AccentColor);
    if (bNuclear) Flash->SetAttenuationRadius(1700.0f);
    if (bNuclear)
    {
        ThermalPlume->SetVisibility(true);
        ThermalPlume->SetRelativeLocation(FVector(0.0f, 0.0f, 14.0f));
        ThermalPlume->SetRelativeScale3D(FVector(0.22f, 0.22f, 0.34f));
        if (BeamMaterial)
        {
            BeamMaterial->SetVectorParameterValue(TEXT("GlowColor"), FLinearColor(1.0f, 0.24f, 0.035f));
            ThermalPlume->SetMaterial(0, BeamMaterial);
        }
    }
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
    const float WaveAlpha = FMath::Clamp(Alpha / ExpansionRate, 0.0f, 1.0f);
    const float RadiusScale = FMath::Lerp(0.035f, ExpansionRadius / 90.0f, bImplosion ? 1.0f - WaveAlpha : WaveAlpha);
    Ring->SetRelativeScale3D(FVector(RadiusScale));
    if (RingMaterial) RingMaterial->SetScalarParameterValue(TEXT("Opacity"), Fade);
    const float FlashEnvelope = bNuclear
        ? (Alpha < 0.12f ? 1.0f : (Alpha < 0.22f ? 0.08f : FMath::Clamp(1.0f - (Alpha - 0.22f) / 0.78f, 0.0f, 1.0f)))
        : 1.0f - Alpha;
    Flash->SetIntensity(PeakFlashIntensity * FlashEnvelope);
    if (bNuclear) Flash->SetLightColor(Alpha < 0.22f ? FLinearColor(0.72f, 0.88f, 1.0f) : FLinearColor(1.0f, 0.34f, 0.08f));
    if (OrbitalBeam->IsVisible() && BeamMaterial) BeamMaterial->SetScalarParameterValue(TEXT("Opacity"), Fade * 0.8f);
    if (bNuclear && ThermalPlume)
    {
        const float Growth = FMath::Lerp(0.22f, 0.95f, FMath::Clamp(Alpha * 1.5f, 0.0f, 1.0f));
        ThermalPlume->SetRelativeLocation(FVector(0.0f, 0.0f, 14.0f + 42.0f * Alpha));
        ThermalPlume->SetRelativeScale3D(FVector(Growth, Growth, Growth * 1.35f));
        if (BeamMaterial) BeamMaterial->SetScalarParameterValue(TEXT("Opacity"), Fade * 0.9f);
    }
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
