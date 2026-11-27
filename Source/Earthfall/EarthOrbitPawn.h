#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EarthfallTypes.h"
#include "EarthOrbitPawn.generated.h"

class AEarthfallPlanet;

/** How much of the HUD is drawn. H cycles through these. */
enum class EEarthfallHudMode : uint8
{
	Full,
	TickerOnly,
	Hidden
};
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * The player's orbital firing platform: an orbit camera, a surface-hugging blast
 * preview, the weapon and multiplier selection, and the launcher that sends a
 * visible projectile from this point of view to the targeted spot.
 */
UCLASS()
class EARTHFALL_API AEarthOrbitPawn : public APawn
{
	GENERATED_BODY()

public:
	AEarthOrbitPawn();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// --- Read by the HUD.
	int32 GetImpactCount() const { return ImpactCount; }
	int32 GetLaunchCount() const { return LaunchCount; }
	int32 GetWeaponIndex() const { return WeaponIndex; }
	float GetCooldownRatio() const;
	bool IsAimOnPlanet() const { return bAimOnPlanet; }
	/** Blast radius of the selected weapon in world units, with multipliers applied. */
	float GetCurrentBlastRadius() const;
	const FHitResult& GetAimHit() const { return AimHit; }
	float GetMultiplier(EEarthfallMultiplier Kind) const;
	const FString& GetStatusMessage() const { return StatusMessage; }
	float GetMessageTimer() const { return MessageTimer; }
	float GetOrbitDistance() const { return Distance; }
	EEarthfallHudMode GetHudMode() const { return HudMode; }
	bool IsInspectMode() const { return bInspectMode; }
	int32 GetInspectedCountry() const { return InspectedCountry; }

	// --- Driven by input, HUD buttons and the capture director.
	void SelectWeapon(int32 Index);
	void AdjustMultiplier(EEarthfallMultiplier Kind, int32 Delta);
	void SetMultiplierIndex(EEarthfallMultiplier Kind, int32 Index);
	void ResetSimulation();
	void CycleHudMode();
	void SetHudMode(EEarthfallHudMode Mode) { HudMode = Mode; }
	/** In inspect mode a click opens a nation's dossier instead of launching. */
	void ToggleInspectMode();
	/** Open the dossier for whichever nation lies under a planet-local direction. */
	void InspectDirection(const FVector& PlanetLocalDirection);
	/** Point the camera at a longitude/latitude from a given distance from the planet centre. */
	void SetOrbitView(float LongitudeDegrees, float LatitudeDegrees, float InDistance);
	/** Launch the selected weapon at a planet-local unit direction, ignoring cooldown. */
	bool FireAtDirection(const FVector& PlanetLocalDirection);

	/** Called by the projectile when its strike has been resolved on the planet. */
	void NotifyStrikeLanded(const FEarthfallStrike& Strike);

protected:
	virtual void BeginPlay() override;
	void OrbitYawInput(float Value);
	void OrbitPitchInput(float Value);
	void BeginOrbit();
	void EndOrbit();
	void ZoomInput(float Value);
	void FireImpact();
	void SelectWeapon1() { SelectWeapon(0); }
	void SelectWeapon2() { SelectWeapon(1); }
	void SelectWeapon3() { SelectWeapon(2); }
	void SelectWeapon4() { SelectWeapon(3); }
	void SelectWeapon5() { SelectWeapon(4); }
	void SelectWeapon6() { SelectWeapon(5); }
	void YieldDown() { AdjustMultiplier(EEarthfallMultiplier::Yield, -1); }
	void YieldUp() { AdjustMultiplier(EEarthfallMultiplier::Yield, 1); }
	void RadiusDown() { AdjustMultiplier(EEarthfallMultiplier::BlastRadius, -1); }
	void RadiusUp() { AdjustMultiplier(EEarthfallMultiplier::BlastRadius, 1); }
	void FalloutDown() { AdjustMultiplier(EEarthfallMultiplier::Fallout, -1); }
	void FalloutUp() { AdjustMultiplier(EEarthfallMultiplier::Fallout, 1); }

private:
	static constexpr int32 PreviewSegments = 96;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> TargetPreviewRing;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PreviewMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PreviewMID;

	/** Orbit angles are the camera's own longitude and latitude over the planet. */
	float Yaw = 0.0f;
	float Pitch = 10.0f;
	float Distance = 475.0f;
	FVector2D PendingOrbit = FVector2D::ZeroVector;
	FHitResult AimHit;
	bool bIsOrbiting = false;
	bool bAimOnPlanet = false;
	bool bLaunchFromRight = true;
	bool bInspectMode = false;
	int32 InspectedCountry = 0;
	EEarthfallHudMode HudMode = EEarthfallHudMode::Full;

	int32 WeaponIndex = 0;
	int32 MultiplierIndices[static_cast<int32>(EEarthfallMultiplier::Count)];
	int32 ImpactCount = 0;
	int32 LaunchCount = 0;
	float CooldownRemaining = 0.0f;
	float CooldownDuration = 0.35f;
	float MessageTimer = 0.0f;
	FString StatusMessage = TEXT("EARTHFALL READY");

	float ShakeStrength = 0.0f;
	float ShakeDuration = 0.0f;
	float ShakeTimeRemaining = 0.0f;
	float ShakePhase = 0.0f;

	AEarthfallPlanet* FindPlanet() const;
	void UpdateAimState();
	void BuildTargetPreviewRing();
	void UpdateTargetPreviewRing();
	void LaunchStrike(AEarthfallPlanet* Planet, const FVector& ImpactPoint);
	void TriggerImpactShake(const FEarthfallStrike& Strike);
	void SetStatus(const FString& Message, float Seconds);
};
