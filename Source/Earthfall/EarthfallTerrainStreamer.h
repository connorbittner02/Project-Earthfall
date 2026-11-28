#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallTerrainStreamer.generated.h"

class AEarthfallPlanet;
class AEarthOrbitPawn;
class UDecalComponent;
class USkyLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UInstancedStaticMeshComponent;

/**
 * Hands the view over from the orbital globe to streamed terrain in low orbit.
 *
 * With Cesium for Unreal present (EARTHFALL_WITH_CESIUM) this owns a
 * CesiumGeoreference and a Cesium World Terrain tileset, scaled so the WGS84
 * ellipsoid sits just under the 100-unit planet sphere. As the camera descends
 * from 1.10 R to 1.035 R the planet surface dissolves onto the tiles, the orbit
 * floor drops to the equivalent of 30,000 ft, and a planet-sized deferred decal
 * paints the destruction simulations onto the terrain by longitude and latitude.
 *
 * Below 1.05 R a second tileset, Cesium OSM Buildings, adds the real buildings of
 * every mapped city, shaded by M_OSMBuildings from the same simulation maps. Hub
 * cities that have outgrown their baseline get instanced megatowers and cranes.
 *
 * Nothing changes until tiles have actually loaded, so a build without the
 * plugin, or without a Cesium ion token, behaves exactly like the orbital game.
 */
UCLASS()
class EARTHFALL_API AEarthfallTerrainStreamer : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallTerrainStreamer();
	virtual void Tick(float DeltaSeconds) override;

	/** 0 = no streamed terrain on screen, 1 = terrain loaded and usable. */
	float GetTerrainReadiness() const { return TerrainReadiness; }
	/** 0 = orbital globe, 1 = fully handed over to streamed terrain. */
	float GetCrossFade() const { return CrossFade; }
	/** Building tiles on screen, megatower boxes and cranes standing: for logs and captures. */
	int32 GetBuildingTileCount() const { return BuildingTileCount; }
	int32 GetTowerCount() const { return TowerCount; }
	int32 GetCraneCount() const { return CraneCount; }

protected:
	virtual void BeginPlay() override;

private:
	void SpawnTerrain();
	bool HasLoadedTiles() const;
	/** Give every newly streamed building tile the simulation maps; returns the tile count. */
	int32 BindBuildingTiles();
	void UpdateArcologies();
	void RebuildArcologies();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	/** One decal the size of the planet: it shades whatever opaque terrain lies inside it. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDecalComponent> DestructionDecal;
	/** Sky fill for the lit terrain tiles and their water. Zero in orbit, where everything is unlit. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> TerrainSkyLight;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DestructionMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DestructionMID;
	/** The star backdrop, dimmed when the camera is deep in a daylit sky. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackdropMID;

	UPROPERTY(Transient)
	TObjectPtr<AEarthfallPlanet> Planet;
	/** ACesiumGeoreference and ACesium3DTileset, held as AActor so this header needs no Cesium types. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Georeference;
	UPROPERTY(Transient)
	TObjectPtr<AActor> Tileset;
	/** The Cesium OSM Buildings tileset. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Buildings;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BuildingMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ArcologyMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeaconMaterial;
	/** Megatower bodies, their spires, crane masts and jibs, and the cranes' amber beacons. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> TowerBoxes;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> TowerSpires;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> CraneBoxes;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> CraneBeacons;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TowerMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CraneMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeaconMID;

	/** Building tile materials that already have the simulation maps. */
	TSet<TWeakObjectPtr<UMaterialInstanceDynamic>> BoundBuildingMaterials;
	FVector BoundSun = FVector::ZeroVector;
	/** Terrain height in metres above the ellipsoid at each hub city, once sampled. */
	TMap<FString, double> CityHeights;
	TSet<FString> PendingCityHeights;
	uint32 ArcologySignature = 0;
	float BuildingBindTimer = 0.0f;
	float ArcologyTimer = 0.0f;
	float BeaconClock = 0.0f;
	int32 BuildingTileCount = 0;
	int32 TowerCount = 0;
	int32 CraneCount = 0;
	bool bBuildingsShown = false;

	float TerrainRadius = 0.0f;
	float TerrainReadiness = 0.0f;
	float CrossFade = 0.0f;
	bool bStreaming = false;
	float LogAccumulator = 0.0f;
};
