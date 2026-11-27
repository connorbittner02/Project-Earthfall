#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/PointLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EarthImpactFeedback.generated.h"

UCLASS()
class EARTHFALL_API AEarthImpactFeedback : public AActor
{
    GENERATED_BODY()
public:
    AEarthImpactFeedback();
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UPointLightComponent> Flash;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UProceduralMeshComponent> Ring;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UStaticMeshComponent> OrbitalBeam;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UStaticMeshComponent> ThermalPlume;
    float Age = 0.0f;
    float Lifetime = 1.1f;
    float PeakFlashIntensity = 22000.0f;
    float ExpansionRadius = 90.0f;
    float ExpansionRate = 1.0f;
    FLinearColor AccentColor = FLinearColor(1.0f, 0.3f, 0.06f);
    bool bImplosion = false;
    bool bNuclear = false;
    void InitFeedback(float ImpactRadius, FLinearColor InAccentColor, float InLifetime, bool bShowBeam, float FlashIntensity, bool bInImplosion = false, bool bInNuclear = false);
    virtual void Tick(float DeltaSeconds) override;
private:
    void BuildRing();
    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInstanceDynamic> RingMaterial;
    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInstanceDynamic> BeamMaterial;
};
