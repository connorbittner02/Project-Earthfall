#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"

AEarthOrbitPawn::AEarthOrbitPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Root);
    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AEarthOrbitPawn::BeginPlay()
{
    Super::BeginPlay();
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->bShowMouseCursor = true;
        PC->bEnableClickEvents = true;
        PC->SetInputMode(FInputModeGameAndUI());
    }
}

void AEarthOrbitPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Yaw += PendingOrbit.X;
    Pitch = FMath::Clamp(Pitch + PendingOrbit.Y, -80.0f, 80.0f);
    PendingOrbit = FVector::ZeroVector;
    const FRotator OrbitRotation(Pitch, Yaw, 0.0f);
    const FVector CameraLocation = OrbitRotation.Vector() * -Distance;
    Camera->SetWorldLocation(CameraLocation);
    Camera->SetWorldRotation((-CameraLocation).Rotation());
}

void AEarthOrbitPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("OrbitYaw"), this, &AEarthOrbitPawn::OrbitYawInput);
    PlayerInputComponent->BindAxis(TEXT("OrbitPitch"), this, &AEarthOrbitPawn::OrbitPitchInput);
    PlayerInputComponent->BindAxis(TEXT("Zoom"), this, &AEarthOrbitPawn::ZoomInput);
    PlayerInputComponent->BindAction(TEXT("Impact"), IE_Pressed, this, &AEarthOrbitPawn::FireImpact);
}

void AEarthOrbitPawn::OrbitYawInput(float Value)
{
    PendingOrbit.X += Value * 90.0f * GetWorld()->GetDeltaSeconds();
}

void AEarthOrbitPawn::OrbitPitchInput(float Value)
{
    PendingOrbit.Y += Value * 90.0f * GetWorld()->GetDeltaSeconds();
}

void AEarthOrbitPawn::ZoomInput(float Value)
{
    Distance = FMath::Clamp(Distance - Value * 25.0f, 135.0f, 1200.0f);
}

void AEarthOrbitPawn::FireImpact()
{
    if (!Camera) return;
    const FVector Start = Camera->GetComponentLocation();
    const FVector End = Start + Camera->GetForwardVector() * 5000.0f;
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility))
    {
        for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It)
        {
            It->ApplyImpact(Hit.ImpactPoint, 8.0f, 50.0f);
            break;
        }
    }
}
