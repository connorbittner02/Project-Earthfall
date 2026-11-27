#include "EarthfallTerrainStreamer.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#if EARTHFALL_WITH_CESIUM
#include "Cesium3DTileset.h"
#include "CesiumGeoreference.h"
#include "CesiumIonRasterOverlay.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogEarthfallTerrain, Log, All);

namespace
{
constexpr double Wgs84EquatorMeters = 6378137.0;
/** 30,000 ft: the lowest the camera may fly over streamed terrain. */
constexpr double FloorMeters = 9144.0;
/**
 * The streamed ellipsoid sits 0.15% under the planet sphere. That hides every
 * tile, Everest included, beneath the orbital globe, so the globe can dissolve
 * onto the terrain instead of fighting it for depth.
 */
constexpr float TerrainRadiusScale = 0.9985f;
/** The hand-over runs between these orbit distances, in planet radii. */
constexpr float FadeStartScale = 1.10f;
constexpr float FadeEndScale = 1.035f;
/** Tiles start loading here, well before they are shown. */
constexpr float StreamStartScale = 1.30f;
/** Cesium ion asset IDs: Cesium World Terrain, and Bing Maps Aerial imagery. */
constexpr int64 WorldTerrainAsset = 1;
constexpr int64 BingAerialAsset = 2;
/** Sky fill on streamed terrain at full hand-over in daylight. */
constexpr float SkyLightIntensity = 1.0f;
}

AEarthfallTerrainStreamer::AEarthfallTerrainStreamer()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the pawn has moved the camera for this frame.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	DestructionDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("DestructionDecal"));
	DestructionDecal->SetupAttachment(Root);
	DestructionDecal->SetVisibility(false);
	DestructionDecal->FadeScreenSize = 0.0f;

	TerrainSkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("TerrainSkyLight"));
	TerrainSkyLight->SetupAttachment(Root);
	TerrainSkyLight->SetMobility(EComponentMobility::Movable);
	TerrainSkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
	TerrainSkyLight->bLowerHemisphereIsBlack = false;
	TerrainSkyLight->SetCastShadows(false);
	TerrainSkyLight->Intensity = 0.0f;
	static ConstructorHelpers::FObjectFinder<UTextureCube> Sky(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
	if (Sky.Succeeded()) TerrainSkyLight->Cubemap = Sky.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Decal(TEXT("/Game/Earth/Materials/M_TerrainDestruction.M_TerrainDestruction"));
	DestructionMaterial = Decal.Succeeded() ? Decal.Object : nullptr;
}

void AEarthfallTerrainStreamer::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}
	if (!Planet) return;

	TerrainRadius = Planet->PlanetRadius * TerrainRadiusScale;
	SetActorLocationAndRotation(Planet->GetActorLocation(), Planet->GetActorRotation());
	if (DestructionMaterial)
	{
		DestructionMID = UMaterialInstanceDynamic::Create(DestructionMaterial, this);
		DestructionDecal->SetDecalMaterial(DestructionMID);
		DestructionDecal->DecalSize = FVector(Planet->PlanetRadius * 1.05f);
	}
	SpawnTerrain();
}

void AEarthfallTerrainStreamer::SpawnTerrain()
{
#if EARTHFALL_WITH_CESIUM
	UWorld* World = GetWorld();
	const FTransform PlanetTransform(Planet->GetActorRotation(), Planet->GetActorLocation());

	// True origin puts the Earth's centre on the planet's centre. Cesium maps
	// ECEF (X, Y, Z) to Unreal (X, -Y, Z), which is the same east = -Y convention
	// the globe, the shaders and the simulations already use.
	ACesiumGeoreference* Geo = World->SpawnActorDeferred<ACesiumGeoreference>(ACesiumGeoreference::StaticClass(), PlanetTransform);
	if (!Geo) return;
	Geo->SetOriginPlacement(EOriginPlacement::TrueOrigin);
	// Scale is a percentage of true size, and true size is 100 Unreal units per metre.
	Geo->SetScale(static_cast<double>(TerrainRadius) / Wgs84EquatorMeters);
	Geo->FinishSpawning(PlanetTransform);
	Georeference = Geo;

	ACesium3DTileset* Tiles = World->SpawnActorDeferred<ACesium3DTileset>(ACesium3DTileset::StaticClass(), PlanetTransform);
	if (!Tiles) return;
	Tiles->SetGeoreference(Geo);
	Tiles->SetTilesetSource(ETilesetSource::FromCesiumIon);
	Tiles->SetIonAssetID(WorldTerrainAsset);
	// Aiming uses the planet's own collision sphere; tile physics would only cost load time.
	Tiles->SetCreatePhysicsMeshes(false);
	// The tiles start out hidden beneath the opaque globe. With occlusion culling
	// on, Cesium would treat them as unseen and never refine them.
	Tiles->SetEnableOcclusionCulling(false);
	Tiles->MaximumSimultaneousTileLoads = 96;
	// Show a coarser parent until its children arrive, rather than a hole into space.
	Tiles->ForbidHoles = true;
	// From low orbit the horizon is thousands of kilometres off; fog culling
	// would drop those tiles and leave a blank band under the limb.
	Tiles->EnableFogCulling = false;
	// No Cesium water mask: its mirror surface reflects the sky light's stock
	// clouds. The destruction decal shades open water with the orbital ocean model.
	Tiles->FinishSpawning(PlanetTransform);

	UCesiumIonRasterOverlay* Imagery = NewObject<UCesiumIonRasterOverlay>(Tiles, TEXT("BingAerialImagery"));
	Imagery->IonAssetID = BingAerialAsset;
	Imagery->MaterialLayerKey = TEXT("Overlay0");
	Imagery->SetMaximumSimultaneousTileLoads(64);
	Tiles->AddInstanceComponent(Imagery);
	Imagery->RegisterComponent();

	Tiles->SetActorHiddenInGame(true);
	Tileset = Tiles;
	UE_LOG(LogEarthfallTerrain, Display, TEXT("EARTHFALL_TERRAIN Cesium tileset spawned: radius %.3f, georeference scale %.9f%%"),
		TerrainRadius, Geo->GetScale());
#else
	UE_LOG(LogEarthfallTerrain, Display, TEXT("EARTHFALL_TERRAIN built without Cesium for Unreal; low orbit stays procedural."));
#endif
}

bool AEarthfallTerrainStreamer::HasLoadedTiles() const
{
#if EARTHFALL_WITH_CESIUM
	const ACesium3DTileset* Tiles = Cast<ACesium3DTileset>(Tileset);
	if (!Tiles) return false;
	// Streaming never "finishes" while the camera moves, so readiness is having
	// enough tiles to cover the view, not a load percentage. Coarse tiles show
	// first and sharpen in place. Without a valid ion token the count stays zero.
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Tiles);
	return Primitives.Num() >= 12;
#else
	return false;
#endif
}

void AEarthfallTerrainStreamer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Planet) return;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	AEarthOrbitPawn* Pawn = PC ? Cast<AEarthOrbitPawn>(PC->GetPawn()) : nullptr;
	if (!Pawn) return;

	const float Radius = Planet->PlanetRadius;
	const float Distance = Pawn->GetOrbitDistance();

	// Stream only when the camera is low enough to need it. Hidden tilesets
	// stop refining, so this also bounds the network and memory cost in orbit.
	const bool bWantStreaming = Tileset != nullptr && Distance < Radius * StreamStartScale;
	if (bWantStreaming != bStreaming)
	{
		bStreaming = bWantStreaming;
		if (Tileset) Tileset->SetActorHiddenInGame(!bStreaming);
	}

	const float ReadyTarget = (bStreaming && HasLoadedTiles()) ? 1.0f : 0.0f;
	TerrainReadiness = FMath::FInterpConstantTo(TerrainReadiness, ReadyTarget, DeltaSeconds, 1.5f);

	LogAccumulator += DeltaSeconds;
	if (bStreaming && LogAccumulator >= 1.0f)
	{
		LogAccumulator = 0.0f;
		int32 PrimitiveCount = 0;
		float Progress = 0.0f;
#if EARTHFALL_WITH_CESIUM
		if (const ACesium3DTileset* Tiles = Cast<ACesium3DTileset>(Tileset))
		{
			TInlineComponentArray<UPrimitiveComponent*> Primitives(Tiles);
			PrimitiveCount = Primitives.Num();
			Progress = Tiles->GetLoadProgress();
		}
#endif
		UE_LOG(LogEarthfallTerrain, Display, TEXT("EARTHFALL_TERRAIN distance=%.3f progress=%.1f primitives=%d readiness=%.2f crossfade=%.2f"),
			Distance, Progress, PrimitiveCount, TerrainReadiness, CrossFade);
	}

	CrossFade = TerrainReadiness * (1.0f - FMath::SmoothStep(Radius * FadeEndScale, Radius * FadeStartScale, Distance));
	Planet->SetSurfaceFade(1.0f - CrossFade);
	Planet->SetCameraDistance(Distance, CrossFade > 0.5f ? TerrainRadius : Radius);

	// The orbit floor follows readiness, so losing the tiles lifts the camera
	// back to the procedural floor instead of leaving it inside the globe.
	const float FloorAltitude = static_cast<float>(FloorMeters / Wgs84EquatorMeters) * TerrainRadius;
	Pawn->SetTerrainFloor(TerrainReadiness > 0.99f ? TerrainRadius : 0.0f,
		FMath::Lerp(Radius * FadeEndScale, TerrainRadius + FloorAltitude, TerrainReadiness));

	// Daylight under the camera decides how much sky there is to reflect.
	const FVector CameraPosition = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	const FVector Up = (CameraPosition - Planet->GetActorLocation()).GetSafeNormal();
	const float Daylight = FMath::SmoothStep(-0.10f, 0.25f, static_cast<float>(FVector::DotProduct(Up, Planet->GetSunDirectionWorld())));
	TerrainSkyLight->SetIntensity(CrossFade * Daylight * SkyLightIntensity);

	// Deep in a daylit atmosphere the sky outshines the stars.
	if (!BackdropMID)
	{
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			if (It->GetWorld() == GetWorld() && It->GetFName() == FName(TEXT("EarthfallSpaceBackdrop")))
			{
				BackdropMID = It->CreateDynamicMaterialInstance(0);
				break;
			}
		}
	}
	if (BackdropMID)
	{
		const float Depth = 1.0f - FMath::SmoothStep(0.3f, 5.0f, Distance - Radius * TerrainRadiusScale);
		const float Stars = 1.0f - 0.97f * Depth * Daylight;
		// Tuning = (panorama gain, star gain, black floor, unused), as built in build_earth_assets.py.
		BackdropMID->SetVectorParameterValue(TEXT("Tuning"), FLinearColor(2.3f * Stars, 0.62f * Stars, 0.075f, 0.0f));
	}

	const bool bShowDecal = DestructionMID != nullptr && CrossFade > 0.01f;
	if (DestructionDecal->IsVisible() != bShowDecal) DestructionDecal->SetVisibility(bShowDecal);
	if (bShowDecal)
	{
		Planet->BindSimulationTextures(DestructionMID);
		const FVector Camera = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		const FVector Local = Planet->GetActorTransform().InverseTransformPosition(Camera);
		DestructionMID->SetVectorParameterValue(TEXT("CameraPos"), FLinearColor(Local.X, Local.Y, Local.Z, 0.0f));
		const FVector SunLocal = Planet->GetActorTransform().InverseTransformVectorNoScale(Planet->GetSunDirectionWorld());
		DestructionMID->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(SunLocal.X, SunLocal.Y, SunLocal.Z, 0.0f));
		const FVector2D Atmosphere = Planet->GetAtmosphereState();
		DestructionMID->SetVectorParameterValue(TEXT("Atmosphere"), FLinearColor(Atmosphere.X, Atmosphere.Y, 0.0f, 0.0f));
		DestructionMID->SetVectorParameterValue(TEXT("Column"), Planet->GetColumnShadowParams());
		DestructionMID->SetVectorParameterValue(TEXT("Terrain"), FLinearColor(CrossFade, 0.0f, 0.0f, 0.0f));
	}
}
