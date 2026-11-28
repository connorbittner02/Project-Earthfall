#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallPlume.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UStaticMeshComponent;

/** What a strike tells its plume. Lengths are world units on the planet's scale. */
struct FEarthfallPlumeParams
{
	float PlanetRadius = 100.0f;
	/** Blast radius in world units; sets the girth of the column. */
	float BlastRadius = 8.0f;
	/** True for an ocean impact: spray, foam and a white steam column. False: an ash column. */
	bool bOcean = false;
	/** 0..1 from the displaced water volume or the strike energy; scales the spray. */
	float Strength = 0.5f;
	/** World-space unit vector toward the Sun. */
	FVector SunDirection = FVector::ForwardVector;
	/** World-space wind over ground zero; the column shears along it with altitude. */
	FVector WindDirection = FVector::ZeroVector;
	/** 0..1 thermal buoyancy: how far the anvil spreads and how hard the top is sheared. */
	float Loft = 0.4f;
};

/**
 * The tall, slow part of an impact, built entirely from overlapping soft cloud
 * puffs so no part of it has a hard outline.
 *
 *   Land:  a stack of ash puffs to 1.08 R, a ring of them where the column
 *          punches through the cloud decks, a lobed mushroom cap, and a dull
 *          ember glow under the cap.
 *   Ocean: a ring of billowing sea-spray puffs to about 1.035 R, a churning
 *          foam ring on the water, and a steam column to 1.06 R.
 *
 * The actor's +Z is the surface normal, so every part is laid out along local Z.
 * The shadow the column casts on clouds and ground is drawn by the planet's
 * shaders from the same position and height.
 */
UCLASS()
class EARTHFALL_API AEarthfallPlume : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallPlume();
	void InitPlume(const FEarthfallPlumeParams& InParams);
	virtual void Tick(float DeltaSeconds) override;

	/** Seconds a column stands before it has faded out. */
	static constexpr float LifeSeconds = 70.0f;
	/** Seconds for the column to reach full height. */
	static constexpr float RiseSeconds = 7.0f;
	/** Top of an ash column and of a steam column, as fractions of the planet radius above the surface. */
	static constexpr float AshTop = 0.08f;
	static constexpr float SteamTop = 0.06f;

	/** Column height above the surface right now, as a fraction of the planet radius. */
	static float HeightFraction(float Age, bool bOcean);

private:
	static constexpr int32 StemPuffs = 11;
	static constexpr int32 SkirtPuffs = 7;
	static constexpr int32 CapLobes = 7;
	static constexpr int32 ColumnPuffs = StemPuffs + SkirtPuffs + CapLobes;
	static constexpr int32 OuterSprayPuffs = 11;
	static constexpr int32 InnerSprayPuffs = 6;
	static constexpr int32 SprayPuffs = OuterSprayPuffs + InnerSprayPuffs;

	/** Stem, skirt and cap lobes: one instanced batch of soft puffs. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> Column;
	/** The body of the mushroom cap. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Cap;
	/** Embers lighting the underside of an ash cap. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> EmberGlow;
	/** Sea spray thrown up round an ocean impact. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> Spray;
	/** A flat, soft-edged ring of churning foam on the water. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> FoamRing;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> VolumeMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PuffMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FoamMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ColumnMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CapMID;
	/** The anvil: the flat top that spreads at the tropopause and streams downwind. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Anvil;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> AnvilVeil;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AnvilMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AnvilVeilMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SprayMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FoamMID;

	FEarthfallPlumeParams Params;
	/** Fixed per-puff variation: x, y in -1..1 and z in 0..1. */
	TArray<FVector> Jitter;
	TArray<FQuat> Spin;
	TArray<FTransform> ColumnTransforms;
	TArray<FTransform> SprayTransforms;
	float Age = 0.0f;
	bool bInitialised = false;

	void UpdateShape();
	void BuildFoamRing();
};
