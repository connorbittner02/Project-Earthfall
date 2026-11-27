#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "EarthOrbitPawn.generated.h"

UCLASS()
class EARTHFALL_API AEarthOrbitPawn : public APawn
{
    GENERATED_BODY()

public:
    AEarthOrbitPawn();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    int32 GetImpactCount() const { return ImpactCount; }
    int32 GetWeaponIndex() const { return WeaponIndex; }
    float GetCooldownRatio() const;
    bool IsAimOnPlanet() const { return bAimOnPlanet; }
    float GetCurrentBlastRadius() const;
    const FHitResult& GetAimHit() const { return AimHit; }

protected:
    virtual void BeginPlay() override;
    void OrbitYawInput(float Value);
    void OrbitPitchInput(float Value);
    void BeginOrbit();
    void EndOrbit();
    void ZoomInput(float Value);
    void FireImpact();
    void SelectWeapon1();
    void SelectWeapon2();
    void SelectWeapon3();
    void SelectWeapon4();
    void SelectWeapon5();
    void SelectWeapon6();
    void ResetSimulation();
    void SelectWeapon(int32 Index);
    void UpdateAimState();
    void TriggerImpactShake();
    void BuildTargetPreviewRing();
    void UpdateTargetPreviewRing();

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPointLightComponent> PlanetFillLight;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> TargetPreviewRing;

    float Yaw = 0.0f;
    float Pitch = -10.0f;
    float Distance = 340.0f;
    FVector PendingOrbit = FVector::ZeroVector;
    FHitResult AimHit;
    bool bIsOrbiting = false;
    int32 WeaponIndex = 0;
    int32 ImpactCount = 0;
    float CooldownRemaining = 0.0f;
    float CooldownDuration = 0.35f;
    float MessageTimer = 0.0f;
    float ShakeStrength = 0.0f;
    float ShakeDuration = 0.0f;
    float ShakeTimeRemaining = 0.0f;
    float ShakePhase = 0.0f;
    bool bAimOnPlanet = false;
    FString StatusMessage = TEXT("EARTHFALL READY");
    friend class AEarthfallHUD;
};
