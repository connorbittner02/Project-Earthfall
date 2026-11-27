#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallEjecta.generated.h"

class AEarthfallPlanet;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Sub-orbital ejecta. Every large strike throws thousands of molten fragments out
 * in a 45-75 degree cone. Each one flies a true ballistic arc under inverse-square
 * gravity toward the planet's centre, so the curtain climbs into black space,
 * hangs over the horizon and falls back as incandescent streaks hundreds of
 * kilometres away. The largest fragments light secondary fires and raise dust
 * where they land.
 *
 * The fragments are integrated on the CPU (a few thousand cheap steps a frame)
 * and drawn as two GPU-instanced batches: cooling rock, and re-entry streaks.
 * A Niagara GPU emitter would do the same job, but Niagara systems cannot be
 * authored headlessly, and this project builds every asset from scripts.
 */
UCLASS()
class EARTHFALL_API AEarthfallEjecta : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallEjecta();
	virtual void Tick(float DeltaSeconds) override;

	/** Throw Count fragments from a planet-local unit direction. SpeedScale 1 reaches about 1.25 R. */
	void Launch(const FVector& LocalDirection, int32 Count, float SpeedScale);
	void Reset();

	int32 GetAirborneCount() const { return AirborneCount; }

	/** Ring buffer size: the most fragments alive at once. */
	static constexpr int32 MaxFragments = 6000;
	/** Surface gravity in world units per second squared, tuned for 5-12 second arcs on a 100-unit planet. */
	static constexpr float SurfaceGravity = 2.5f;

protected:
	virtual void BeginPlay() override;

private:
	struct FFragment
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.0f;
		float Size = 1.0f;
		bool bAlive = false;
		/** Big enough to start a fire or raise dust where it lands. */
		bool bMajor = false;
	};

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> Rocks;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> Streaks;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> EjectaMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RockMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StreakMID;
	UPROPERTY(Transient)
	TObjectPtr<AEarthfallPlanet> Planet;

	TArray<FFragment> Fragments;
	TArray<FTransform> RockTransforms;
	TArray<FTransform> StreakTransforms;
	int32 NextSlot = 0;
	int32 AirborneCount = 0;
	bool bInstancesBuilt = false;
	bool bDrewLastFrame = false;
};
