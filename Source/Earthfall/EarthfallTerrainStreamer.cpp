#include "EarthfallTerrainStreamer.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
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
	Tiles->FinishSpawning(PlanetTransform);

	UCesiumIonRasterOverlay* Imagery = NewObject<UCesiumIonRasterOverlay>(Tiles, TEXT("BingAerialImagery"));
	Imagery->IonAssetID = BingAerialAsset;
	Imagery->MaterialLayerKey = TEXT("Overlay0");
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
	if (!Tiles || Tiles->GetLoadProgress() < 75.0f) return false;
	// Without a valid ion token the tileset never creates a single tile component.
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Tiles);
	if (Primitives.Num() > 0) return true;
	const USceneComponent* TilesRoot = Tiles->GetRootComponent();
	return TilesRoot && TilesRoot->GetNumChildrenComponents() > 0;
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

	CrossFade = TerrainReadiness * (1.0f - FMath::SmoothStep(Radius * FadeEndScale, Radius * FadeStartScale, Distance));
	Planet->SetSurfaceFade(1.0f - CrossFade);
	Planet->SetCameraDistance(Distance, CrossFade > 0.5f ? TerrainRadius : Radius);

	// The orbit floor follows readiness, so losing the tiles lifts the camera
	// back to the procedural floor instead of leaving it inside the globe.
	const float FloorAltitude = static_cast<float>(FloorMeters / Wgs84EquatorMeters) * TerrainRadius;
	Pawn->SetTerrainFloor(TerrainReadiness > 0.99f ? TerrainRadius : 0.0f,
		FMath::Lerp(Radius * FadeEndScale, TerrainRadius + FloorAltitude, TerrainReadiness));

	const bool bShowDecal = DestructionMID != nullptr && CrossFade > 0.01f;
	if (DestructionDecal->IsVisible() != bShowDecal) DestructionDecal->SetVisibility(bShowDecal);
	if (bShowDecal)
	{
		Planet->BindSimulationTextures(DestructionMID);
		const FVector Camera = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		const FVector Local = Planet->GetActorTransform().InverseTransformPosition(Camera);
		DestructionMID->SetVectorParameterValue(TEXT("CameraPos"), FLinearColor(Local.X, Local.Y, Local.Z, 0.0f));
		DestructionMID->SetVectorParameterValue(TEXT("Terrain"), FLinearColor(CrossFade, 0.0f, 0.0f, 0.0f));
	}
}
