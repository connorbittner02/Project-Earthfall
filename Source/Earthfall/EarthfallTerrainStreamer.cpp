#include "EarthfallTerrainStreamer.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "EarthfallPopulationSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
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
#include "CesiumSampleHeightResult.h"
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
/** Cesium OSM Buildings: every OpenStreetMap building footprint, extruded to its height. */
constexpr int64 OsmBuildingsAsset = 96188;
/** Buildings are streamed and shown below this orbit distance, in planet radii. */
constexpr float BuildingsStartScale = 1.05f;
/** Sky fill on streamed terrain at full hand-over in daylight. */
constexpr float SkyLightIntensity = 1.0f;
/** And at night, when the only sky fill is the glow of lit cities. */
constexpr float NightSkyLightIntensity = 3.0f;
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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BuildingFinder(TEXT("/Game/Earth/Materials/M_OSMBuildings.M_OSMBuildings"));
	BuildingMaterial = BuildingFinder.Succeeded() ? BuildingFinder.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ArcologyFinder(TEXT("/Game/Earth/Materials/M_Arcology.M_Arcology"));
	ArcologyMaterial = ArcologyFinder.Succeeded() ? ArcologyFinder.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BeaconFinder(TEXT("/Game/Earthfall/Materials/M_FXEjecta.M_FXEjecta"));
	BeaconMaterial = BeaconFinder.Succeeded() ? BeaconFinder.Object : nullptr;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto MakeBatch = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent* Batch = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Batch->SetupAttachment(Root);
		Batch->SetStaticMesh(Mesh);
		Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Batch->SetCastShadow(false);
		Batch->SetVisibility(false);
		return Batch;
	};
	TowerBoxes = MakeBatch(TEXT("TowerBoxes"), CubeMesh.Succeeded() ? CubeMesh.Object : nullptr);
	TowerSpires = MakeBatch(TEXT("TowerSpires"), ConeMesh.Succeeded() ? ConeMesh.Object : nullptr);
	CraneBoxes = MakeBatch(TEXT("CraneBoxes"), CubeMesh.Succeeded() ? CubeMesh.Object : nullptr);
	CraneBeacons = MakeBatch(TEXT("CraneBeacons"), SphereMesh.Succeeded() ? SphereMesh.Object : nullptr);
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
	if (ArcologyMaterial)
	{
		// Look = (kind, storey bands, lit level, 0), as in arcology.hlsl.
		TowerMID = UMaterialInstanceDynamic::Create(ArcologyMaterial, this);
		TowerMID->SetVectorParameterValue(TEXT("Look"), FLinearColor(0.0f, 16.0f, 1.0f, 0.0f));
		TowerBoxes->SetMaterial(0, TowerMID);
		TowerSpires->SetMaterial(0, TowerMID);
		CraneMID = UMaterialInstanceDynamic::Create(ArcologyMaterial, this);
		CraneMID->SetVectorParameterValue(TEXT("Look"), FLinearColor(1.0f, 26.0f, 1.0f, 0.0f));
		CraneBoxes->SetMaterial(0, CraneMID);
	}
	if (BeaconMaterial)
	{
		BeaconMID = UMaterialInstanceDynamic::Create(BeaconMaterial, this);
		BeaconMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(1.4f, 1.6f, 2.0f, 0.0f));
		CraneBeacons->SetMaterial(0, BeaconMID);
		CraneBeacons->SetTranslucentSortPriority(19);
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
	// Cesium World Terrain triangulates flat ground coarsely. Without smooth normals
	// every large triangle catches the sky light at its own angle and reads as a facet.
	Tiles->SetGenerateSmoothNormals(true);
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

	// The cities themselves: OpenStreetMap footprints extruded to their real
	// heights, on the same georeference, so they stand on the streamed terrain.
	if (BuildingMaterial)
	{
		if (ACesium3DTileset* Osm = World->SpawnActorDeferred<ACesium3DTileset>(ACesium3DTileset::StaticClass(), PlanetTransform))
		{
			Osm->SetGeoreference(Geo);
			Osm->SetTilesetSource(ETilesetSource::FromCesiumIon);
			Osm->SetIonAssetID(OsmBuildingsAsset);
			Osm->SetCreatePhysicsMeshes(false);
			Osm->SetEnableOcclusionCulling(false);
			Osm->SetMaterial(BuildingMaterial);
			// A city is tens of thousands of buildings. A coarser error than the
			// terrain's keeps the draw count inside the 4K frame budget.
			Osm->SetMaximumScreenSpaceError(24.0);
			Osm->MaximumSimultaneousTileLoads = 48;
			Osm->EnableFogCulling = false;
			Osm->FinishSpawning(PlanetTransform);
			Osm->SetActorHiddenInGame(true);
			Buildings = Osm;
		}
	}
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

	// Buildings only matter once the camera is nearly on the deck.
	const bool bWantBuildings = Buildings != nullptr && bStreaming && Distance < Radius * BuildingsStartScale;
	if (bWantBuildings != bBuildingsShown)
	{
		bBuildingsShown = bWantBuildings;
		Buildings->SetActorHiddenInGame(!bBuildingsShown);
	}
	BuildingBindTimer += DeltaSeconds;
	if (bBuildingsShown && BuildingBindTimer >= 0.25f)
	{
		BuildingBindTimer = 0.0f;
		BuildingTileCount = BindBuildingTiles();
	}
	else if (!bBuildingsShown)
	{
		BuildingTileCount = 0;
	}

	ArcologyTimer += DeltaSeconds;
	if (bStreaming && ArcologyTimer >= 1.0f)
	{
		ArcologyTimer = 0.0f;
		UpdateArcologies();
	}
	const bool bShowTowers = bStreaming && CrossFade > 0.5f;
	for (UInstancedStaticMeshComponent* Batch : {TowerBoxes.Get(), TowerSpires.Get(), CraneBoxes.Get(), CraneBeacons.Get()})
	{
		if (Batch && Batch->IsVisible() != bShowTowers) Batch->SetVisibility(bShowTowers);
	}
	if (bShowTowers)
	{
		const FVector Sun = Planet->GetSunDirectionWorld();
		const FLinearColor SunColour(Sun.X, Sun.Y, Sun.Z, 0.0f);
		const FLinearColor TowerParams(static_cast<float>(Wgs84EquatorMeters / TerrainRadius), 0.0f, 0.0f, 0.0f);
		for (UMaterialInstanceDynamic* Material : {TowerMID.Get(), CraneMID.Get()})
		{
			if (!Material) continue;
			Material->SetVectorParameterValue(TEXT("SunDirection"), SunColour);
			Material->SetVectorParameterValue(TEXT("Params"), TowerParams);
			Planet->BindSimulationTextures(Material);
			if (UTexture2D* Growth = Planet->GetCityGrowthTexture()) Material->SetTextureParameterValue(TEXT("CityGrowthObject"), Growth);
		}
		// Aviation beacons: a long amber flash every second and a half.
		BeaconClock += DeltaSeconds;
		const float Flash = FMath::Fmod(BeaconClock, 1.5f) < 0.6f ? 1.0f : 0.25f;
		if (BeaconMID) BeaconMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.0f, 0.50f, 0.04f, 16.0f * Flash));
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
		UE_LOG(LogEarthfallTerrain, Display, TEXT("EARTHFALL_TERRAIN distance=%.3f progress=%.1f primitives=%d readiness=%.2f crossfade=%.2f buildings=%d towers=%d cranes=%d"),
			Distance, Progress, PrimitiveCount, TerrainReadiness, CrossFade, BuildingTileCount, TowerCount, CraneCount);
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
	// By night the sky over a city is its own sodium glow. The decal covers
	// everything that is not a lit city, so this only ever shows on lit streets.
	TerrainSkyLight->SetIntensity(CrossFade * FMath::Lerp(NightSkyLightIntensity, SkyLightIntensity, Daylight));
	TerrainSkyLight->SetLightColor(FMath::Lerp(FLinearColor(1.0f, 0.70f, 0.40f), FLinearColor::White, Daylight));

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
		const FVector Atmosphere = Planet->GetAtmosphereState();
		DestructionMID->SetVectorParameterValue(TEXT("Atmosphere"), FLinearColor(Atmosphere.X, Atmosphere.Y, Atmosphere.Z, 0.0f));
		DestructionMID->SetVectorParameterValue(TEXT("Column"), Planet->GetColumnShadowParams());
		DestructionMID->SetVectorParameterValue(TEXT("Terrain"), FLinearColor(CrossFade, 0.0f, 0.0f, 0.0f));
	}
}

int32 AEarthfallTerrainStreamer::BindBuildingTiles()
{
	if (!Buildings || !Planet) return 0;
	const FVector Sun = Planet->GetSunDirectionWorld();
	const bool bSunMoved = !Sun.Equals(BoundSun, 1.0e-4);
	BoundSun = Sun;
	const FLinearColor SunColour(Sun.X, Sun.Y, Sun.Z, 0.0f);
	const FLinearColor Params(static_cast<float>(Wgs84EquatorMeters / TerrainRadius), 0.0f, 0.0f, 0.0f);

	// Cesium makes one dynamic material per tile from M_OSMBuildings. The
	// simulation maps are render targets that exist only at run time, so each
	// new tile is handed them once; the sun is pushed again only if it moves.
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Buildings);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Primitive->GetMaterial(0));
		if (!Material) continue;
		const bool bNew = !BoundBuildingMaterials.Contains(Material);
		if (bNew)
		{
			BoundBuildingMaterials.Add(Material);
			Planet->BindSimulationTextures(Material);
			if (UTexture2D* Growth = Planet->GetCityGrowthTexture()) Material->SetTextureParameterValue(TEXT("CityGrowthObject"), Growth);
			Material->SetVectorParameterValue(TEXT("Params"), Params);
			// The streamed imagery under the buildings has no shadows of theirs to match.
			Primitive->SetCastShadow(false);
		}
		if (bNew || bSunMoved) Material->SetVectorParameterValue(TEXT("SunDirection"), SunColour);
	}
	// Tiles unload as the camera moves on; drop their dead entries now and then.
	if (BoundBuildingMaterials.Num() > Primitives.Num() * 3 + 256)
	{
		for (auto It = BoundBuildingMaterials.CreateIterator(); It; ++It)
		{
			if (!It->IsValid()) It.RemoveCurrent();
		}
	}
	return Primitives.Num();
}

void AEarthfallTerrainStreamer::UpdateArcologies()
{
	const UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>();
	if (!Population || !Georeference) return;
	TArray<FEarthfallCityInfo> Cities;
	Population->GetCities(Cities);
	// Rebuild only when some city changes level, or a terrain height arrives.
	uint32 Signature = GetTypeHash(CityHeights.Num());
	for (const FEarthfallCityInfo& City : Cities)
	{
		const int32 Level = City.Tier > 0 ? City.Tier + 1 : (City.Growth >= 1.25f ? 1 : 0);
		Signature = HashCombine(Signature, GetTypeHash(Level));
	}
	if (Signature == ArcologySignature) return;
	ArcologySignature = Signature;
	RebuildArcologies();
}

void AEarthfallTerrainStreamer::RebuildArcologies()
{
#if EARTHFALL_WITH_CESIUM
	const UEarthfallPopulationSubsystem* Population = GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>();
	ACesiumGeoreference* Geo = Cast<ACesiumGeoreference>(Georeference);
	ACesium3DTileset* Terrain = Cast<ACesium3DTileset>(Tileset);
	if (!Population || !Geo || !Terrain) return;
	for (UInstancedStaticMeshComponent* Batch : {TowerBoxes.Get(), TowerSpires.Get(), CraneBoxes.Get(), CraneBeacons.Get()}) Batch->ClearInstances();
	TowerCount = 0;
	CraneCount = 0;

	const double MetersPerUnit = Wgs84EquatorMeters / TerrainRadius;
	const FVector Centre = Planet->GetActorLocation();
	TArray<FEarthfallCityInfo> Cities;
	Population->GetCities(Cities);
	TArray<FVector> WantHeights;
	TArray<FString> WantNames;

	for (const FEarthfallCityInfo& City : Cities)
	{
		const int32 Level = City.Tier > 0 ? City.Tier + 1 : (City.Growth >= 1.25f ? 1 : 0);
		if (Level == 0) continue;
		const double Latitude = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(City.Direction.Z, -1.0, 1.0)));
		const double Longitude = FMath::RadiansToDegrees(FMath::Atan2(-City.Direction.Y, City.Direction.X));
		const double* Ground = CityHeights.Find(City.Name);
		if (!Ground)
		{
			// Ask the terrain how high this city stands; the towers go up when it answers.
			if (!PendingCityHeights.Contains(City.Name))
			{
				PendingCityHeights.Add(City.Name);
				WantHeights.Add(FVector(Longitude, Latitude, 0.0));
				WantNames.Add(City.Name);
			}
			continue;
		}

		FRandomStream Random(GetTypeHash(City.Name));
		// A box (or cone, or sphere) of the given size in metres, standing on the
		// terrain East and North of the city centre with its base at Bottom.
		auto Place = [&](UInstancedStaticMeshComponent* Batch, double East, double North, double Bottom, double Height,
			double Width, double Depth, double YawDegrees)
		{
			const double Lat = Latitude + North / 111320.0;
			const double Lon = Longitude + East / (111320.0 * FMath::Max(0.2, FMath::Cos(FMath::DegreesToRadians(Latitude))));
			const FVector Base = Geo->TransformLongitudeLatitudeHeightPositionToUnreal(FVector(Lon, Lat, *Ground + Bottom + Height * 0.5));
			const FVector Up = (Base - Centre).GetSafeNormal();
			FVector NorthAxis = (FVector::UpVector - Up * FVector::DotProduct(FVector::UpVector, Up)).GetSafeNormal();
			if (NorthAxis.IsNearlyZero()) NorthAxis = FVector::ForwardVector;
			NorthAxis = NorthAxis.RotateAngleAxis(YawDegrees, Up);
			const FQuat Rotation = FRotationMatrix::MakeFromZX(Up, NorthAxis).ToQuat();
			// The basic shapes are 100 units across.
			const FVector Scale(Depth / MetersPerUnit / 100.0, Width / MetersPerUnit / 100.0, Height / MetersPerUnit / 100.0);
			Batch->AddInstance(FTransform(Rotation, Base, Scale), true);
		};
		// A setback tower: three stacked boxes and a spire. The base is sunk 200 m so no slope can undercut it.
		auto Tower = [&](double East, double North, double Height, double Width)
		{
			const double Yaw = Random.FRandRange(0.0f, 90.0f);
			Place(TowerBoxes, East, North, -200.0, Height * 0.55 + 200.0, Width, Width, Yaw);
			Place(TowerBoxes, East, North, Height * 0.55, Height * 0.30, Width * 0.72, Width * 0.72, Yaw);
			Place(TowerBoxes, East, North, Height * 0.85, Height * 0.15, Width * 0.46, Width * 0.46, Yaw);
			Place(TowerSpires, East, North, Height, Height * 0.20, Width * 0.26, Width * 0.26, Yaw);
			++TowerCount;
		};
		// A tower crane on megastructure scale: mast, jib, and amber beacons at the mast head and both jib ends.
		auto Crane = [&](double East, double North, double Height)
		{
			const double Yaw = Random.FRandRange(0.0f, 360.0f);
			const double Jib = Height * 0.75;
			Place(CraneBoxes, East, North, -200.0, Height + 200.0, 46.0, 46.0, Yaw);
			// The jib runs along the crane's local X (the instance's "depth" axis), offset so the mast sits a quarter of the way along it.
			const FVector Up0 = City.Direction.GetSafeNormal();
			const FVector North0 = (FVector::UpVector - Up0 * FVector::DotProduct(FVector::UpVector, Up0)).GetSafeNormal();
			const FVector Along = North0.RotateAngleAxis(Yaw, Up0);
			const double JibEast = FVector::DotProduct(Along, FVector::CrossProduct(Up0, North0)), JibNorth = FVector::DotProduct(Along, North0);
			Place(CraneBoxes, East + JibEast * Jib * 0.25, North + JibNorth * Jib * 0.25, Height, 34.0, 34.0, Jib, Yaw);
			Place(CraneBeacons, East, North, Height + 40.0, 150.0, 150.0, 150.0, 0.0);
			Place(CraneBeacons, East + JibEast * Jib * 0.75, North + JibNorth * Jib * 0.75, Height + 30.0, 120.0, 120.0, 120.0, 0.0);
			Place(CraneBeacons, East - JibEast * Jib * 0.25, North - JibNorth * Jib * 0.25, Height + 30.0, 120.0, 120.0, 120.0, 0.0);
			++CraneCount;
		};
		auto RingPoint = [&](double MinKm, double MaxKm, double& East, double& North)
		{
			const double Angle = Random.FRandRange(0.0f, 2.0f * PI);
			const double Range = Random.FRandRange(static_cast<float>(MinKm), static_cast<float>(MaxKm)) * 1000.0;
			East = FMath::Cos(Angle) * Range;
			North = FMath::Sin(Angle) * Range;
		};

		// Level 1: a city outgrowing its baseline, with cranes up and one tower rising.
		// Level 2: megacity. Level 3: arcology, around one tower two and a half kilometres tall.
		const int32 Towers = Level == 3 ? 14 : (Level == 2 ? 6 : 1);
		const int32 Cranes = Level == 3 ? 7 : (Level == 2 ? 4 : 3);
		double East = 0.0, North = 0.0;
		if (Level == 3) Tower(0.0, 0.0, 2500.0, 520.0);
		for (int32 Index = 0; Index < Towers; ++Index)
		{
			RingPoint(Level == 3 ? 1.6 : 0.8, Level == 3 ? 9.0 : 6.0, East, North);
			const double Height = Level == 3 ? Random.FRandRange(700.0f, 1600.0f) : (Level == 2 ? Random.FRandRange(450.0f, 900.0f) : 380.0);
			Tower(East, North, Height, Level == 3 ? Random.FRandRange(160.0f, 280.0f) : Random.FRandRange(110.0f, 170.0f));
		}
		for (int32 Index = 0; Index < Cranes; ++Index)
		{
			RingPoint(0.8, Level == 3 ? 10.0 : 7.0, East, North);
			Crane(East, North, Random.FRandRange(520.0f, 950.0f));
		}
	}

	if (!WantHeights.IsEmpty())
	{
		Terrain->SampleHeightMostDetailed(WantHeights, FCesiumSampleHeightMostDetailedCallback::CreateWeakLambda(this,
			[this, WantNames](ACesium3DTileset*, const TArray<FCesiumSampleHeightResult>& Results, const TArray<FString>&)
			{
				for (int32 Index = 0; Index < WantNames.Num(); ++Index)
				{
					PendingCityHeights.Remove(WantNames[Index]);
					// A failed sample falls back to the ellipsoid: sunk bases still reach the ground at sea level.
					CityHeights.Add(WantNames[Index], Results.IsValidIndex(Index) && Results[Index].SampleSuccess ? Results[Index].LongitudeLatitudeHeight.Z : 0.0);
				}
				// A new height changes the signature, so the next update rebuilds.
			}));
	}
	UE_LOG(LogEarthfallTerrain, Display, TEXT("EARTHFALL_ARCOLOGY towers=%d cranes=%d awaiting_heights=%d"), TowerCount, CraneCount, PendingCityHeights.Num());
#endif
}
