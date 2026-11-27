#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "EarthfallTypes.h"
#include "EarthfallPlanet.generated.h"

class UCanvas;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USphereComponent;
class UTexture2D;
class UTextureRenderTarget2D;

/**
 * The globe: a deformable surface, two procedural cloud shells and an atmosphere
 * shell, all shaded by unlit materials that are fed the sun vector, an impact
 * mask, a wind-advected ash field and up to four live shockwaves.
 */
UCLASS()
class EARTHFALL_API AEarthfallPlanet : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallPlanet();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Resolve one detonation: scorch, shockwave, ash, casualties, crater and FX. */
	void ApplyStrike(const FEarthfallStrike& Strike);

	UFUNCTION(BlueprintCallable, Category = "Earthfall|Impact")
	void ApplyImpact(FVector WorldLocation, float Radius, float Energy, int32 WeaponIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "Earthfall|Planet")
	void ResetPlanet();

	/** Outline and tint one nation (0 clears) for the Country Dossier. */
	void SetHighlightedCountry(int32 CountryId);

	/** Bind the live simulation render targets to another material, such as the terrain decal. */
	void BindSimulationTextures(UMaterialInstanceDynamic* Material) const;
	/** 1 draws the surface, 0 dissolves it completely onto streamed terrain beneath. */
	void SetSurfaceFade(float Fade);
	/** Fades each cloud deck as the camera nears it, and sizes the aim sphere to the visible ground. */
	void SetCameraDistance(float Distance, float AimRadius);

	/** A large ejecta fragment has landed: raise dust and, on land, light a fire. */
	void SeedSecondaryImpact(const FVector& LocalDirection);
	/** (atmosphere health 0..1 after soot, sunlight 0..1): drives haze colour on streamed terrain. */
	FVector GetAtmosphereState() const { return FVector(AtmosphereHealthNow, SunlightNow, WinterLevel); }
	/** 0 = today's ice, 1 = frost to the tropics. Advances while the climate is broken and retreats slowly. */
	float GetWinterLevel() const { return WinterLevel; }
	/** Pin the winter level for a capture or a test; negative releases it back to the climate. */
	void SetWinterOverride(float Level) { WinterOverride = Level; }
	/** Lift the sea surface at an open-water point, as an impact or sea-floor rupture does. Strength 0..1. */
	void TriggerTsunami(const FVector& LocalDirection, float Strength);
	bool IsTsunamiActive() const { return TsunamiActiveSeconds > 0.0f; }
	/** Planet-local directions where ejecta fragments have lit fires on land since the last reset. */
	const TArray<FVector>& GetSecondaryFireSites() const { return SecondaryFireSites; }
	/**
	 * Redraw the bio mask from the sandbox's zones. XYZ = planet-local direction,
	 * W = angular radius; colour R = infection, G = rage or insurgency.
	 */
	void SetBioZones(const TArray<FVector4>& Zones, const TArray<FLinearColor>& Colours);
	/** (planet-local direction, height in radii) of the tallest standing ash or steam column; height 0 if none. */
	FLinearColor GetColumnShadowParams() const;

	/** Unit vector from the planet toward the Sun, in world space. */
	FVector GetSunDirectionWorld() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Earthfall|Planet")
	float PlanetRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Earthfall|Planet")
	float RotationDegreesPerSecond = 0.0f;

	/** Shell radii as multiples of PlanetRadius. */
	static constexpr float LowCloudScale = 1.012f;
	static constexpr float HighCloudScale = 1.020f;
	static constexpr float AtmosphereScale = 1.045f;

private:
	static constexpr int32 ShockSlots = 4;
	static constexpr int32 MaxImpactStamps = 160;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> PlanetMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> AimCollisionProxy;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> LowCloudLayer;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> HighCloudLayer;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> AtmosphereShell;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CloudMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> AtmosphereMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> AshAdvectionMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TsunamiStepMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FireStepMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ImpactMaskRT;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> AshPlumeRTs;
	/** Ping-pong wave field: R height, G velocity, B coastal inundation. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> TsunamiRTs;
	/** Ping-pong wildfire automaton: R fire, G burn scar. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> FireRTs;
	/** Crater height field for parallax occlusion: R bowl depth, G rim and rays. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CraterRT;
	/** 512x256 RGBA8: R = infection, G = rage and insurgency. Redrawn from the sandbox's zones. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> PlagueRT;
	/** One byte per population cell: how each cell's settled population compares with its baseline. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CityGrowthTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> CraterStampTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FireRingStampTexture;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TsunamiStepMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FireStepMID;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ScorchStampTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> EmpStampTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ClearStampTexture;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AshStampTexture;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LowCloudMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HighCloudMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AshAdvectionMID;

	/** One persistent mark in ImpactMaskRT. Angles are radians on the unit sphere. */
	struct FImpactStamp
	{
		FVector Direction = FVector::ForwardVector;
		float Radius = 0.1f;
		float Age = 0.0f;
		bool bNuclear = false;
		/** An EMP leaves only the blackout plateau: no scorch, no cleared cloud. */
		bool bEmpOnly = false;
		float HeatSeconds = 8.0f;
		/** The cloud-clear disc grows behind the shock front, holds, then heals. */
		float ClearMaxRadius = 0.2f;
		float ClearSpeed = 0.1f;
		float ClearHoldSeconds = 22.0f;
		float ClearFadeSeconds = 35.0f;

		bool IsLive() const { return Age < FMath::Max(ClearHoldSeconds + ClearFadeSeconds, HeatSeconds * 6.0f); }
	};

	/** One expanding shockwave fed to the materials as ShockA/B/C vector parameters. */
	struct FShock
	{
		bool bActive = false;
		bool bNuclear = false;
		FVector Direction = FVector::ForwardVector;
		float Age = 0.0f;
		float MaxRadius = 0.5f;
		float Speed = 0.1f;
		float FlashPeak = 1.0f;
		float FlashRadius = 0.2f;
		FLinearColor FlashColor = FLinearColor::White;
		/** A column stands here and casts a shadow for as long as the plume actor lives. */
		bool bColumn = false;
		bool bOceanColumn = false;
	};

	TArray<FImpactStamp> ImpactStamps;
	FShock Shocks[ShockSlots];
	bool bShockParametersDirty = true;
	bool bImpactMaskDirty = true;

	TArray<FVector> BaseVertices;
	TArray<FVector> DeformedVertices;
	TArray<int32> Triangles;
	TArray<FVector> SphereNormals;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	FVector SunDirectionLocal = FVector::ForwardVector;
	float SunUpdateAccumulator = 0.0f;
	float MaskRedrawAccumulator = 0.0f;
	float AshStepAccumulator = 0.0f;
	float AshActiveSeconds = 0.0f;
	float EnvironmentAccumulator = 0.0f;
	int32 AshReadIndex = 0;
	float SurfaceFade = 1.0f;
	float WinterLevel = 0.0f;
	float CityGrowthTimer = 10.0f;
	TArray<FColor> CityGrowthBytes;
	void UpdateCityGrowthTexture();
	TArray<FVector> SecondaryFireSites;
	float WinterOverride = -1.0f;
	float FamineTimer = 0.0f;
	float FamineNewsTimer = 0.0f;
	double FamineDeadSinceNews = 0.0;
	void UpdateWinter(float DeltaSeconds);
	float AtmosphereHealthNow = 1.0f;
	float SunlightNow = 1.0f;
	/** Strength 0..1 of the strongest tsunami still running; sets wave speed and run-up. */
	float TsunamiDrive = 0.0f;
	int32 LastShockSlot = 0;
	float LowCloudProximity = 1.0f;
	float HighCloudProximity = 1.0f;
	int32 TsunamiReadIndex = 0;
	int32 FireReadIndex = 0;
	float TsunamiStepAccumulator = 0.0f;
	float TsunamiActiveSeconds = 0.0f;
	float FireStepAccumulator = 0.0f;
	float FireActiveSeconds = 0.0f;

	void CreateMaterialInstances();
	void BuildSphereTopology();
	void BuildPlanetMesh();
	void BuildShell(UProceduralMeshComponent* Shell, float RadiusScale, UMaterialInstanceDynamic* Material, int32 SortPriority);
	void BuildImpactMaskResources();
	void BuildAshPlumeResources();
	/** Evaluate receives (distance from centre 0..1+, angle in radians). */
	UTexture2D* CreateStampTexture(TFunctionRef<FColor(float, float)> Evaluate, int32 Size = 128) const;
	UTextureRenderTarget2D* CreateFloatTarget(const TCHAR* Name, int32 Width, int32 Height, bool bFullFloat = false);
	void BuildWaveFireAndCraterResources();
	void StampInto(UTextureRenderTarget2D* Target, UTexture2D* Texture, const FVector& LocalDirection, float AngularRadius, const FLinearColor& Tint);
	void BindSimulationTextures();
	void StepTsunami();
	void StepFire(float StepSeconds);
	void DrawStamp(UCanvas* Canvas, const FVector2D& CanvasSize, UTexture2D* Texture,
		const FVector& Direction, float AngularRadius, const FLinearColor& Tint) const;
	void RedrawImpactMask();
	void StampAsh(const FVector& LocalDirection, float AngularRadius, float Soot, float Fallout);
	void AdvanceAsh(float StepSeconds);
	void BindAshTexture();
	void StartShock(const FVector& LocalDirection, float BlastAngle, const FEarthfallStrike& Strike, float& OutMaxRadius, float& OutSpeed);
	void UpdateShocks(float DeltaSeconds);
	void PushShockParameters();
	void UpdateSunDirection();
	void UpdateEnvironment();
	void DeformCrater(const FVector& LocalDirection, const FEarthfallStrike& Strike);
	void ForEachShadedMaterial(TFunctionRef<void(UMaterialInstanceDynamic*)> Visit) const;
};
