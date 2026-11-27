#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallTerrainStreamer.generated.h"

class AEarthfallPlanet;
class AEarthOrbitPawn;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

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

protected:
	virtual void BeginPlay() override;

private:
	void SpawnTerrain();
	bool HasLoadedTiles() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	/** One decal the size of the planet: it shades whatever opaque terrain lies inside it. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDecalComponent> DestructionDecal;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DestructionMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DestructionMID;

	UPROPERTY(Transient)
	TObjectPtr<AEarthfallPlanet> Planet;
	/** ACesiumGeoreference and ACesium3DTileset, held as AActor so this header needs no Cesium types. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> Georeference;
	UPROPERTY(Transient)
	TObjectPtr<AActor> Tileset;

	float TerrainRadius = 0.0f;
	float TerrainReadiness = 0.0f;
	float CrossFade = 0.0f;
	bool bStreaming = false;
};
