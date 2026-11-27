#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EarthfallPlanet.generated.h"

UCLASS()
class EARTHFALL_API AEarthfallPlanet : public AActor
{
    GENERATED_BODY()

public:
    AEarthfallPlanet();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UFUNCTION(BlueprintCallable, Category="Earthfall|Impact")
    void ApplyImpact(FVector WorldLocation, float Radius, float Energy, int32 WeaponIndex = 0);

    UFUNCTION(BlueprintCallable, Category="Earthfall|Planet")
    void ResetPlanet();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Earthfall|Planet")
    float PlanetRadius = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Earthfall|Planet")
    float RotationDegreesPerSecond = 0.0f;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UProceduralMeshComponent> PlanetMesh;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class USphereComponent> AimCollisionProxy;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UStaticMeshComponent> AtmosphereShell;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UProceduralMeshComponent> CloudLayer;
    UPROPERTY(Transient)
    TObjectPtr<class UTextureRenderTarget2D> ImpactMaskRT;
    UPROPERTY(Transient)
    TObjectPtr<class UTexture2D> ImpactStampTexture;
    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInstanceDynamic> SurfaceMaterialInstance;
    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInstanceDynamic> AtmosphereMaterialInstance;
    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInstanceDynamic> CloudMaterialInstance;
    struct FImpactMaskStamp
    {
        FVector Direction = FVector::ForwardVector;
        float Radius = 1.0f;
        float Age = 0.0f;
        bool bNuclear = false;
    };
    TArray<FImpactMaskStamp> ImpactMaskStamps;
    TArray<FVector> BaseVertices;
    TArray<FVector> DeformedVertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FLinearColor> BaseVertexColors;
    TArray<FVector> CloudVertices;
    TArray<FLinearColor> CloudColors;
    TArray<FProcMeshTangent> Tangents;
    float ImpactMaskUpdateAccumulator = 0.0f;
    float SunDirectionUpdateAccumulator = 0.0f;

    void BuildPlanetMesh();
    void RefreshSurfaceVertexColors();
    void BuildAtmosphereAndCloudShells();
    void BuildImpactMaskResources();
    void UpdateImpactMask(float DeltaSeconds, bool bAdvanceAge);
    void StampImpactMask(const FVector& LocalDirection, float Radius, bool bNuclear = false);
    void UpdateSunDirection();
};
