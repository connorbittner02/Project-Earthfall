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
    float Age = 0.0f;
    float Lifetime = 1.1f;
    float PeakFlashIntensity = 22000.0f;
    float ExpansionRadius = 90.0f;
    FLinearColor AccentColor = FLinearColor(1.0f, 0.3f, 0.06f);
    UPROPERTY()
    TObjectPtr<class UMaterialInstanceDynamic> RingMaterial;
    UPROPERTY()
    TObjectPtr<class UMaterialInstanceDynamic> BeamMaterial;
    void InitFeedback(float ImpactRadius, FLinearColor InAccentColor, float InLifetime, bool bShowBeam, float FlashIntensity);
    virtual void Tick(float DeltaSeconds) override;
private:
    void BuildRing();
};
