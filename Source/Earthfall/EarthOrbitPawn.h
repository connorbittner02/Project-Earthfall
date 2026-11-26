#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "EarthOrbitPawn.generated.h"

UCLASS()
class EARTHFALL_API AEarthOrbitPawn : public APawn
{
    GENERATED_BODY()

public:
    AEarthOrbitPawn();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
    virtual void BeginPlay() override;
    void OrbitYawInput(float Value);
    void OrbitPitchInput(float Value);
    void ZoomInput(float Value);
    void FireImpact();

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCameraComponent> Camera;

    float Yaw = 0.0f;
    float Pitch = -10.0f;
    float Distance = 360.0f;
    FVector PendingOrbit = FVector::ZeroVector;
};
