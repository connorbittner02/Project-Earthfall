#include "EarthfallMoon.h"
#include "EarthfallTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AEarthfallMoon::AEarthfallMoon()
{
	PrimaryActorTick.bCanEverTick = true;
	MoonMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MoonMesh"));
	SetRootComponent(MoonMesh);
	MoonMesh->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Earth/Materials/M_EarthMoon.M_EarthMoon"));
	if (Sphere.Succeeded()) MoonMesh->SetStaticMesh(Sphere.Object);
	MoonMaterial = Material.Succeeded() ? Material.Object : nullptr;
	MoonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MoonMesh->SetGenerateOverlapEvents(false);
	MoonMesh->SetCastShadow(false);
}

void AEarthfallMoon::BeginPlay()
{
	Super::BeginPlay();
	// The engine sphere has a 50-unit radius.
	MoonMesh->SetWorldScale3D(FVector(MoonRadius / 50.0f));
	if (MoonMaterial)
	{
		MoonMID = UMaterialInstanceDynamic::Create(MoonMaterial, this);
		MoonMesh->SetMaterial(0, MoonMID);
	}
	UpdateSun();
	Tick(0.0f);
}

void AEarthfallMoon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	OrbitPhase = FMath::Fmod(OrbitPhase + DeltaSeconds * 0.012f, 2.0f * PI);
	const float Inclination = FMath::DegreesToRadians(5.0f);
	SetActorLocation(FVector(OrbitRadius * FMath::Cos(OrbitPhase),
		OrbitRadius * FMath::Sin(OrbitPhase) * FMath::Cos(Inclination),
		OrbitRadius * FMath::Sin(OrbitPhase) * FMath::Sin(Inclination)));

	SunUpdateAccumulator += DeltaSeconds;
	if (SunUpdateAccumulator >= 5.0f)
	{
		SunUpdateAccumulator = 0.0f;
		UpdateSun();
	}
}

void AEarthfallMoon::UpdateSun()
{
	if (!MoonMID) return;
	// The Moon never rotates, so its local axes are the world's.
	const FVector ToSun = EarthfallSolar::ComputeToSun();
	MoonMID->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(ToSun.X, ToSun.Y, ToSun.Z, 0.0f));
}
