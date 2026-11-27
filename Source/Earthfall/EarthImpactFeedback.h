#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthImpactFeedback.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UProceduralMeshComponent;
class UStaticMeshComponent;

/** What the planet tells a detonation about itself. Angles are radians. */
struct FEarthfallImpactFX
{
	FVector PlanetCenter = FVector::ZeroVector;
	float PlanetRadius = 100.0f;
	/** World-space surface normal at ground zero. */
	FVector Normal = FVector::UpVector;
	int32 WeaponIndex = 0;
	/** Blast radius in world units. */
	float BlastRadius = 8.0f;
	/** The planet's shock front, so the visible ring tracks the cloud displacement. */
	float ShockMaxRadius = 0.4f;
	float ShockSpeed = 0.1f;
	float YieldMultiplier = 1.0f;
};

/**
 * One detonation's transient visuals: a white-hot fireball dome that cools through
 * orange to dull red, a rising cap, a shock ring that hugs the globe, and the
 * Niagara burst, smoke and spark systems. Lighting of the ground and cloud deck
 * comes from the planet's shock slots, not from a light component.
 */
UCLASS()
class EARTHFALL_API AEarthImpactFeedback : public AActor
{
	GENERATED_BODY()

public:
	AEarthImpactFeedback();
	void InitFeedback(const FEarthfallImpactFX& InParams);
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Fireball;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> RisingCap;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> ShockRing;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> ImpactFireballFX;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> SmokePlumeFX;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> NukePlumeFX;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> NukeShockwaveFX;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> PlasmaBeamFX;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UNiagaraComponent> PlasmaSparksFX;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FireballMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CapMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMID;

	FEarthfallImpactFX Params;
	FLinearColor Accent = FLinearColor(1.0f, 0.3f, 0.06f);
	float Age = 0.0f;
	float FireballSeconds = 1.6f;
	float Lifetime = 4.0f;
	float FireballRadius = 4.0f;
	bool bNuclear = false;
	bool bImplosion = false;
	bool bInitialised = false;

	/** Opt-in for the stock Niagara template copies under Content/Earthfall/FX. */
	UPROPERTY(EditAnywhere, Category = "Earthfall|FX")
	bool bUseNiagaraTemplates = false;

	void BuildRingTopology();
	void UpdateFireball();
	void UpdateShockRing();
};
