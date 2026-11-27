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

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ImpactMaskRT;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> AshPlumeRTs;
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

	void CreateMaterialInstances();
	void BuildSphereTopology();
	void BuildPlanetMesh();
	void BuildShell(UProceduralMeshComponent* Shell, float RadiusScale, UMaterialInstanceDynamic* Material, int32 SortPriority);
	void BuildImpactMaskResources();
	void BuildAshPlumeResources();
	UTexture2D* CreateStampTexture(TFunctionRef<FColor(float)> Evaluate) const;
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
