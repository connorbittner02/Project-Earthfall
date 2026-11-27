#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallPlume.generated.h"

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
	/** True for an ocean impact: a splash crown and a white steam column. False: an ash column. */
	bool bOcean = false;
	/** 0..1 from the displaced water volume or the strike energy; scales the crown. */
	float Strength = 0.5f;
	/** World-space unit vector toward the Sun. */
	FVector SunDirection = FVector::ForwardVector;
};

/**
 * The tall, slow part of an impact: a column that climbs from the surface through
 * both cloud decks and spreads into a layered cap, and for ocean strikes a splash
 * crown thrown up around ground zero.
 *
 *   Land:  ash stem to 1.08 R with a skirt at the cloud tops and a mushroom cap.
 *   Ocean: splash crown to about 1.035 R and a steam column to 1.06 R.
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
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Stem;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Skirt;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Cap;
	/** The splash crown: an open, flaring, jagged curtain of spray built in unit space and scaled. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> Crown;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SprayMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> VolumeMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StemMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkirtMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CapMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CrownMID;

	FEarthfallPlumeParams Params;
	float Age = 0.0f;
	bool bInitialised = false;

	void UpdateShape();
	void BuildCrownMesh();
};
