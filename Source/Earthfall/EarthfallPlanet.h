#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
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
    float RotationDegreesPerSecond = 1.5f;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UProceduralMeshComponent> PlanetMesh;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;
    TArray<FVector> BaseVertices;
    TArray<FVector> DeformedVertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FLinearColor> BaseVertexColors;
    TArray<float> HeatValues;
    TArray<FProcMeshTangent> Tangents;
    float HeatUpdateAccumulator = 0.0f;

    void BuildPlanetMesh();
};
