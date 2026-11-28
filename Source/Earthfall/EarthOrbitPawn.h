#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EarthfallTypes.h"
#include "EarthOrbitPawn.generated.h"

class AEarthfallPlanet;
class UEarthfallAudioComponent;

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
	bool IsAimEnabled() const { return bAimEnabled; }
	int32 GetInspectedCountry() const { return InspectedCountry; }

	// --- Driven by input, HUD buttons and the capture director.
	void SelectWeapon(int32 Index);
	/** The command dock: which tab is open and which tool is armed on each. */
	int32 GetToolTab() const { return ToolTab; }
	int32 GetToolSlot(int32 Tab) const { return ToolSlot[FMath::Clamp(Tab, 0, EarthfallTools::TabCount - 1)]; }
	void SelectTab(int32 Tab);
	void CycleTab() { SelectTab((ToolTab + 1) % EarthfallTools::TabCount); }
	void SelectTool(int32 Tab, int32 Slot);
	/** Launch a specific weapon at a planet-local direction, whatever is selected, ignoring cooldown. */
	void LaunchWeaponAt(const FVector& PlanetLocalDirection, int32 Weapon, float Yield, float RadiusMultiplier, float Fallout);
	UEarthfallAudioComponent* GetAudio() const { return Audio; }
	/** The shell loaded for the next launch: one of EarthfallMunitions. */
	int32 GetMunition() const { return MunitionIndex; }
	void SelectMunition(int32 Index);
	void CycleMunition() { SelectMunition((MunitionIndex + 1) % EarthfallMunitions::Count); }
	void AdjustMultiplier(EEarthfallMultiplier Kind, int32 Delta);
	void SetMultiplierIndex(EEarthfallMultiplier Kind, int32 Index);
	void ResetSimulation();
	/** Scripted captures turn the mouse aim off so a stray cursor draws no reticle or ring. */
	void SetAimEnabled(bool bEnabled) { bAimEnabled = bEnabled; }
	void CycleHudMode();
	void SetHudMode(EEarthfallHudMode Mode) { HudMode = Mode; }
	/** In inspect mode a click opens a nation's dossier instead of launching. */
	void ToggleInspectMode();
	/** Open the dossier for whichever nation lies under a planet-local direction. */
	void InspectDirection(const FVector& PlanetLocalDirection);
	/** Point the camera at a longitude/latitude from a given distance from the planet centre. */
	void SetOrbitView(float LongitudeDegrees, float LatitudeDegrees, float InDistance);
	/** Streamed terrain lowers the ground reference and the orbit floor; zeros restore the planet defaults. */
	void SetTerrainFloor(float InTerrainRadius, float InMinDistance) { TerrainRadius = InTerrainRadius; MinOrbitDistance = InMinDistance; }
	/** Fix the low-orbit camera pitch in degrees up from straight down; negative restores the automatic tilt. */
	void SetTiltOverride(float Degrees) { TiltOverride = Degrees; }
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
	void SelectWeapon1() { SelectTool(ToolTab, 0); }
	void SelectWeapon2() { SelectTool(ToolTab, 1); }
	void SelectWeapon3() { SelectTool(ToolTab, 2); }
	void SelectWeapon4() { SelectTool(ToolTab, 3); }
	void SelectWeapon5() { SelectTool(ToolTab, 4); }
	void SelectWeapon6() { SelectTool(ToolTab, 5); }
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
	/** Where input wants the camera; Yaw, Pitch and Distance ease toward these. */
	float GoalYaw = 0.0f;
	float GoalPitch = 10.0f;
	float GoalDistance = 475.0f;
	/** Set by the terrain streamer: the ground radius altitude is measured from, and the orbit floor. Zero means the planet defaults. */
	float TerrainRadius = 0.0f;
	float MinOrbitDistance = 0.0f;
	float TiltOverride = -1.0f;
	FVector2D PendingOrbit = FVector2D::ZeroVector;
	FHitResult AimHit;
	bool bIsOrbiting = false;
	bool bAimOnPlanet = false;
	bool bLaunchFromRight = true;
	bool bInspectMode = false;
	bool bAimEnabled = true;
	int32 InspectedCountry = 0;
	EEarthfallHudMode HudMode = EEarthfallHudMode::Full;

	int32 WeaponIndex = 0;
	int32 ToolTab = 0;
	int32 MunitionIndex = 0;
	int32 ToolSlot[EarthfallTools::TabCount] = {0, 0, 0, 0};
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UEarthfallAudioComponent> Audio;
	/** The last headline heard, so each war or nuclear dispatch sounds its alert once. */
	FString LastDispatchHeard;
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
	void SpawnProjectile(AEarthfallPlanet* Planet, const FEarthfallStrike& Strike);
	void TriggerImpactShake(const FEarthfallStrike& Strike);
	void SetStatus(const FString& Message, float Seconds);
};
