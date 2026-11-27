#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
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

protected:
    virtual void BeginPlay() override;
    void OrbitYawInput(float Value);
    void OrbitPitchInput(float Value);
    void ZoomInput(float Value);
    void FireImpact();
    void SelectWeapon1();
    void SelectWeapon2();
    void SelectWeapon3();
    void ResetSimulation();
    void SelectWeapon(int32 Index);
    void UpdateAimState();

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UPointLightComponent> PlanetFillLight;

    float Yaw = 0.0f;
    float Pitch = -10.0f;
    float Distance = 340.0f;
    FVector PendingOrbit = FVector::ZeroVector;
    int32 WeaponIndex = 0;
    int32 ImpactCount = 0;
    float CooldownRemaining = 0.0f;
    float CooldownDuration = 0.35f;
    float MessageTimer = 0.0f;
    bool bAimOnPlanet = false;
    FString StatusMessage = TEXT("EARTHFALL READY");
    friend class AEarthfallHUD;
};
