#include "EarthfallGameMode.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "EarthfallHUD.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AEarthfallGameMode::AEarthfallGameMode()
{
    DefaultPawnClass = AEarthOrbitPawn::StaticClass();
    HUDClass = AEarthfallHUD::StaticClass();
}

void AEarthfallGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!GetWorld()) return;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FTransform PlanetTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::OneVector);
    GetWorld()->SpawnActor<AEarthfallPlanet>(AEarthfallPlanet::StaticClass(), PlanetTransform, Params);

    // Aim the sun toward the initial camera-facing hemisphere so the opening
    // view presents the day map instead of the dark-side city-light layer.
    const FVector LightDirection(1.0f, -0.35f, -0.7f);
    ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(
        LightDirection.Rotation().Vector() * -1000.0f, LightDirection.Rotation(), Params);
    if (Sun)
    {
        Sun->GetLightComponent()->SetIntensity(3.2f);
    }
    ASkyLight* Fill = GetWorld()->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (Fill)
    {
        Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Fill->GetLightComponent()->SetIntensity(0.25f);
        Fill->GetLightComponent()->SetLightColor(FLinearColor(0.12f, 0.20f, 0.34f));
        Fill->GetLightComponent()->RecaptureSky();
    }

    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    UMaterialInterface* SpaceMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Earth/Materials/M_SpaceSky.M_SpaceSky"));
    if (Sphere && SpaceMaterial)
    {
        UStaticMeshComponent* Sky = NewObject<UStaticMeshComponent>(this, TEXT("EarthfallSpaceBackdrop"));
        Sky->SetStaticMesh(Sphere);
        Sky->SetMaterial(0, SpaceMaterial);
        Sky->SetWorldScale3D(FVector(3000.0f));
        Sky->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Sky->SetCastShadow(false);
        Sky->SetReverseCulling(true);
        Sky->RegisterComponentWithWorld(GetWorld());
    }

    APostProcessVolume* PostProcess = GetWorld()->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
    if (PostProcess)
    {
        PostProcess->bUnbound = true;
        FPostProcessSettings& Settings = PostProcess->Settings;
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = 0.85f;
        Settings.bOverride_BloomThreshold = true;
        Settings.BloomThreshold = 1.0f;
        Settings.bOverride_AutoExposureMethod = true;
        Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
        Settings.bOverride_AutoExposureBias = true;
        Settings.AutoExposureBias = 0.0f;
        Settings.bOverride_AutoExposureMinBrightness = true;
        Settings.AutoExposureMinBrightness = 1.0f;
        Settings.bOverride_AutoExposureMaxBrightness = true;
        Settings.AutoExposureMaxBrightness = 1.0f;
    }
}
