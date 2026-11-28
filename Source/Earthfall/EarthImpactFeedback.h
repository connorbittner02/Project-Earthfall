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
 * One detonation's transient visuals. Thermal weapons (kinetic rod, mantle charge,
 * thermonuclear) run a three-stage sequence:
 *   1. 0 to 0.25 s: a double flash, a ground flash and a Wilson condensation dome;
 *   2. 0.25 to 4 s: a black-body fireball cooling from 4000 K through 2200 K to
 *      1100 K, turbulent, with a soot jacket closing over it as it cools;
 *   3. from 2 s: the incandescent cap lifts into the mushroom, where AEarthfallPlume
 *      builds the stem and the sheared anvil.
 * The exotic weapons keep their own coloured fireball. All of them get the shock
 * ring that hugs the globe. Lighting of the ground and cloud deck comes from the
 * planet's shock slots, not from a light component.
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
	/** Stage 1: the Wilson condensation dome and the flash on the ground. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WilsonDome;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> GroundFlash;
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
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FireballMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> WilsonMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WilsonMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMID;
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
	/** Kinetic, mantle and thermonuclear strikes: the three-stage black-body detonation. */
	bool bThermal = false;
	bool bInitialised = false;

	/** Opt-in for the stock Niagara template copies under Content/Earthfall/FX. */
	UPROPERTY(EditAnywhere, Category = "Earthfall|FX")
	bool bUseNiagaraTemplates = false;

	void BuildRingTopology();
	void UpdateFireball();
	void UpdateDetonation();
	void UpdateShockRing();
};
