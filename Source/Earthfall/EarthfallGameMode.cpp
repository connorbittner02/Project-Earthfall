#include "EarthfallGameMode.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AEarthfallGameMode::AEarthfallGameMode()
{
    DefaultPawnClass = AEarthOrbitPawn::StaticClass();
}

void AEarthfallGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (!GetWorld()) return;

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FTransform PlanetTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector::OneVector);
    GetWorld()->SpawnActor<AEarthfallPlanet>(AEarthfallPlanet::StaticClass(), PlanetTransform, Params);

    const FVector LightDirection(-1.0f, -0.35f, -0.7f);
    ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(
        LightDirection.Rotation().Vector() * -1000.0f, LightDirection.Rotation(), Params);
    if (Sun)
    {
        Sun->GetLightComponent()->SetIntensity(8.0f);
    }
}
