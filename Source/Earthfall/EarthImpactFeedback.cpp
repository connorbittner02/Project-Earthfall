#include "EarthImpactFeedback.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SceneComponent.h"

AEarthImpactFeedback::AEarthImpactFeedback()
{
    PrimaryActorTick.bCanEverTick = true;
    auto* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Scene);
    Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("ImpactFlash"));
    Flash->SetupAttachment(Scene);
    Flash->Intensity = 120000.0f;
    Flash->AttenuationRadius = 600.0f;
    Flash->SetLightColor(FLinearColor(1.0f, 0.22f, 0.04f));
    Ring = CreateDefaultSubobject<USphereComponent>(TEXT("Shockwave"));
    Ring->SetupAttachment(Scene);
    Ring->SetSphereRadius(20.0f);
    Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ring->SetHiddenInGame(false);
    Ring->SetCollisionProfileName(TEXT("NoCollision"));
    Ring->SetGenerateOverlapEvents(false);
    SetLifeSpan(0.8f);
}

void AEarthImpactFeedback::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    const float Alpha = FMath::Clamp(Age / 0.8f, 0.0f, 1.0f);
    Ring->SetSphereRadius(FMath::Lerp(20.0f, 260.0f, Alpha));
    Flash->SetIntensity(FMath::Lerp(120000.0f, 0.0f, Alpha));
}
