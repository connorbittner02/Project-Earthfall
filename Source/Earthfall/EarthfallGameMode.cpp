#include "EarthfallGameMode.h"
#include "EarthOrbitPawn.h"
#include "EarthfallCaptureDirector.h"
#include "EarthfallHUD.h"
#include "EarthfallMoon.h"
#include "EarthfallPlanet.h"
#include "EarthfallTerrainStreamer.h"
#include "EarthfallTypes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
/** Far enough that orbiting the camera never shifts the Sun against the lighting. */
constexpr float SunDistance = 60000.0f;
constexpr float SunAngularRadiusDegrees = 0.37f;
constexpr float CoronaAngularRadiusDegrees = 6.5f;
constexpr float SkyRadius = 1200000.0f;
constexpr float EngineSphereRadius = 50.0f;
}

AEarthfallGameMode::AEarthfallGameMode()
{
	DefaultPawnClass = AEarthOrbitPawn::StaticClass();
	HUDClass = AEarthfallHUD::StaticClass();
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Space(TEXT("/Game/Earth/Materials/M_SpaceSky.M_SpaceSky"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Sun(TEXT("/Game/Earth/Materials/M_EarthSun.M_EarthSun"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Corona(TEXT("/Game/Earth/Materials/M_SunCorona.M_SunCorona"));
	CelestialSphereMesh = Sphere.Succeeded() ? Sphere.Object : nullptr;
	SpaceBackdropMaterial = Space.Succeeded() ? Space.Object : nullptr;
	SunDiscMaterial = Sun.Succeeded() ? Sun.Object : nullptr;
	SunCoronaMaterial = Corona.Succeeded() ? Corona.Object : nullptr;
}

UStaticMeshComponent* AEarthfallGameMode::SpawnCelestialSphere(const TCHAR* Name, UMaterialInterface* Material,
	const FVector& Location, float Radius, bool bSeenFromInside, int32 SortPriority)
{
	if (!CelestialSphereMesh || !Material || !GetWorld()) return nullptr;
	// AGameModeBase is an AInfo, which is hidden: components registered on it never
	// render. Each body therefore gets a plain visible actor of its own.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Body = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Body) return nullptr;
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Body, Name);
	Component->SetMobility(EComponentMobility::Movable);
	Body->SetRootComponent(Component);
	Component->SetStaticMesh(CelestialSphereMesh);
	Component->SetMaterial(0, Material);
	Component->SetWorldLocation(Location);
	Component->SetWorldScale3D(FVector(Radius / EngineSphereRadius));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCastShadow(false);
	Component->SetReverseCulling(bSeenFromInside);
	Component->SetTranslucentSortPriority(SortPriority);
	Component->RegisterComponentWithWorld(GetWorld());
	return Component;
}

void AEarthfallGameMode::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	if (!World) return;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<AEarthfallPlanet>(AEarthfallPlanet::StaticClass(), FTransform::Identity, Params);
	World->SpawnActor<AEarthfallMoon>(AEarthfallMoon::StaticClass(), FTransform::Identity, Params);
	// Hands low orbit over to streamed terrain when Cesium for Unreal is installed.
	World->SpawnActor<AEarthfallTerrainStreamer>(AEarthfallTerrainStreamer::StaticClass(), FTransform::Identity, Params);

	// The planet, clouds, Moon and Sun are unlit and shade themselves from the
	// same UTC sun vector. This light only exists for lit Niagara particles; it
	// casts no shadows, so there are no cascades to pay for or to band the globe.
	const FVector ToSun = EarthfallSolar::ComputeToSun();
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(ToSun * 1000.0f, (-ToSun).Rotation(), Params))
	{
		Sun->Tags.Add(FName(TEXT("EarthfallSun")));
		Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sun->GetLightComponent()->SetIntensity(4.0f);
		Sun->GetLightComponent()->SetCastShadows(false);
	}

	// No ASkyAtmosphere on purpose: at its default 6,360 km scale it buries a
	// 100-unit globe in grey fog. The atmosphere is the planet's own shell material.
	SpawnCelestialSphere(TEXT("EarthfallSpaceBackdrop"), SpaceBackdropMaterial, FVector::ZeroVector, SkyRadius, true, -20);
	SpawnCelestialSphere(TEXT("EarthfallSunCorona"), SunCoronaMaterial, ToSun * SunDistance,
		SunDistance * FMath::Tan(FMath::DegreesToRadians(CoronaAngularRadiusDegrees)), false, -10);
	SpawnCelestialSphere(TEXT("EarthfallVisibleSun"), SunDiscMaterial, ToSun * SunDistance,
		SunDistance * FMath::Tan(FMath::DegreesToRadians(SunAngularRadiusDegrees)), false, 0);

	if (APostProcessVolume* PostProcess = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params))
	{
		PostProcess->bUnbound = true;
		FPostProcessSettings& Settings = PostProcess->Settings;
		// Fixed exposure with the physical camera term disabled: an emissive value
		// of 1.0 is white before tone mapping. With the physical term left on,
		// manual exposure sits near EV 10 and every unlit material renders black.
		Settings.bOverride_AutoExposureMethod = true;
		Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		Settings.AutoExposureApplyPhysicalCameraExposure = false;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 0.0f;
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.55f;
		Settings.bOverride_BloomThreshold = true;
		Settings.BloomThreshold = 0.9f;
		Settings.bOverride_LensFlareIntensity = true;
		Settings.LensFlareIntensity = 0.0f;
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = 0.28f;
		Settings.bOverride_MotionBlurAmount = true;
		Settings.MotionBlurAmount = 0.0f;
		Settings.bOverride_SceneFringeIntensity = true;
		Settings.SceneFringeIntensity = 0.0f;
		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = 0.0f;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("EarthfallCapture")))
	{
		World->SpawnActor<AEarthfallCaptureDirector>(AEarthfallCaptureDirector::StaticClass(), FTransform::Identity, Params);
	}
}
