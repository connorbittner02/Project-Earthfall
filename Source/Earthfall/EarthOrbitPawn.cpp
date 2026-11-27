#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"

AEarthOrbitPawn::AEarthOrbitPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Root);
    PlanetFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PlanetFillLight"));
    PlanetFillLight->SetupAttachment(Camera);
    // The directional sun supplies the main key light; this low camera fill
    // keeps the night-facing half readable without washing out the polar cap.
    PlanetFillLight->SetIntensity(2800.0f);
    PlanetFillLight->SetAttenuationRadius(1400.0f);
    PlanetFillLight->SetLightColor(FLinearColor(0.30f, 0.52f, 1.0f));
    PlanetFillLight->SetCastShadows(false);
    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AEarthOrbitPawn::BeginPlay()
{
    Super::BeginPlay();
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->bShowMouseCursor = true;
        PC->bEnableClickEvents = true;
        FInputModeGameAndUI InputMode;
        InputMode.SetHideCursorDuringCapture(false);
        InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(InputMode);
    }
}

void AEarthOrbitPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    CooldownRemaining = FMath::Max(0.0f, CooldownRemaining - DeltaSeconds);
    MessageTimer = FMath::Max(0.0f, MessageTimer - DeltaSeconds);
    if (bIsOrbiting)
    {
        const APlayerController* PC = Cast<APlayerController>(GetController());
        float MouseX = 0.0f;
        float MouseY = 0.0f;
        const bool bMouseInViewport = PC && PC->GetMousePosition(MouseX, MouseY);
        const bool bApplicationActive = FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive();
        if (!PC || !PC->IsInputKeyDown(EKeys::RightMouseButton) || !bMouseInViewport || !bApplicationActive) EndOrbit();
    }
    ShakeTimeRemaining = FMath::Max(0.0f, ShakeTimeRemaining - DeltaSeconds);
    ShakePhase += DeltaSeconds;
    Yaw += PendingOrbit.X;
    Pitch = FMath::Clamp(Pitch + PendingOrbit.Y, -80.0f, 80.0f);
    PendingOrbit = FVector::ZeroVector;
    const FRotator OrbitRotation(Pitch, Yaw, 0.0f);
    FVector CameraLocation = OrbitRotation.Vector() * -Distance;
    FRotator CameraRotation = (-CameraLocation).Rotation();
    if (ShakeTimeRemaining > 0.0f)
    {
        const float Fade = ShakeTimeRemaining / FMath::Max(ShakeDuration, KINDA_SMALL_NUMBER);
        const float Strength = ShakeStrength * Fade;
        CameraLocation += FVector(
            FMath::Sin(ShakePhase * 37.0f) * Strength,
            FMath::Sin(ShakePhase * 53.0f + 1.7f) * Strength,
            FMath::Cos(ShakePhase * 29.0f) * Strength * 0.55f);
        CameraRotation.Pitch += FMath::Sin(ShakePhase * 31.0f) * Strength * 0.12f;
        CameraRotation.Yaw += FMath::Cos(ShakePhase * 43.0f) * Strength * 0.12f;
    }
    Camera->SetWorldLocation(CameraLocation);
    Camera->SetWorldRotation(CameraRotation);
    UpdateAimState();
}

void AEarthOrbitPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("OrbitYaw"), this, &AEarthOrbitPawn::OrbitYawInput);
    PlayerInputComponent->BindAxis(TEXT("OrbitPitch"), this, &AEarthOrbitPawn::OrbitPitchInput);
    PlayerInputComponent->BindAction(TEXT("OrbitHold"), IE_Pressed, this, &AEarthOrbitPawn::BeginOrbit);
    PlayerInputComponent->BindAction(TEXT("OrbitHold"), IE_Released, this, &AEarthOrbitPawn::EndOrbit);
    PlayerInputComponent->BindAxis(TEXT("Zoom"), this, &AEarthOrbitPawn::ZoomInput);
    PlayerInputComponent->BindAction(TEXT("Impact"), IE_Pressed, this, &AEarthOrbitPawn::FireImpact);
    PlayerInputComponent->BindAction(TEXT("Weapon1"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon1);
    PlayerInputComponent->BindAction(TEXT("Weapon2"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon2);
    PlayerInputComponent->BindAction(TEXT("Weapon3"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon3);
    PlayerInputComponent->BindAction(TEXT("ResetSimulation"), IE_Pressed, this, &AEarthOrbitPawn::ResetSimulation);
}

void AEarthOrbitPawn::OrbitYawInput(float Value)
{
    if (!bIsOrbiting) return;
    PendingOrbit.X += Value * 90.0f * GetWorld()->GetDeltaSeconds();
}

void AEarthOrbitPawn::OrbitPitchInput(float Value)
{
    if (!bIsOrbiting) return;
    PendingOrbit.Y += Value * 90.0f * GetWorld()->GetDeltaSeconds();
}

void AEarthOrbitPawn::BeginOrbit()
{
    bIsOrbiting = true;
}

void AEarthOrbitPawn::EndOrbit()
{
    bIsOrbiting = false;
    PendingOrbit = FVector::ZeroVector;
}

void AEarthOrbitPawn::ZoomInput(float Value)
{
    Distance = FMath::Clamp(Distance - Value * 25.0f, 135.0f, 1200.0f);
}

void AEarthOrbitPawn::FireImpact()
{
    if (!Camera) return;
    if (CooldownRemaining > 0.0f)
    {
        StatusMessage = TEXT("WEAPON RECHARGING");
        MessageTimer = 0.7f;
        return;
    }
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    FVector Start;
    FVector Direction;
    if (!PC->DeprojectMousePositionToWorld(Start, Direction)) return;
    const FVector End = Start + Direction * 5000.0f;
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility))
    {
        for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It)
        {
            static const float Radii[] = {8.0f, 15.0f, 25.0f};
            static const float Energies[] = {50.0f, 100.0f, 180.0f};
            It->ApplyImpact(Hit.ImpactPoint, Radii[WeaponIndex], Energies[WeaponIndex], WeaponIndex);
            TriggerImpactShake();
            ++ImpactCount;
            CooldownDuration = WeaponIndex == 0 ? 0.35f : (WeaponIndex == 1 ? 0.8f : 1.5f);
            CooldownRemaining = CooldownDuration;
            StatusMessage = FString::Printf(TEXT("IMPACT CONFIRMED  //  #%04d"), ImpactCount);
            MessageTimer = 1.2f;
            break;
        }
    }
    else
    {
        StatusMessage = TEXT("NO PLANETARY CONTACT");
        MessageTimer = 0.8f;
    }
}

float AEarthOrbitPawn::GetCooldownRatio() const
{
    return CooldownDuration > 0.0f ? CooldownRemaining / CooldownDuration : 0.0f;
}

void AEarthOrbitPawn::UpdateAimState()
{
    bAimOnPlanet = false;
    AimHit = FHitResult();
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!PC) return;
    FVector Start;
    FVector Direction;
    if (!PC->DeprojectMousePositionToWorld(Start, Direction)) return;
    bAimOnPlanet = GetWorld()->LineTraceSingleByChannel(
        AimHit, Start, Start + Direction * 5000.0f, ECC_Visibility);
}

float AEarthOrbitPawn::GetCurrentBlastRadius() const
{
    static const float Radii[] = {8.0f, 15.0f, 25.0f};
    return Radii[WeaponIndex];
}

void AEarthOrbitPawn::TriggerImpactShake()
{
    static const float Strengths[] = {0.8f, 1.4f, 3.0f};
    static const float Durations[] = {0.22f, 0.34f, 0.72f};
    ShakeStrength = Strengths[WeaponIndex];
    ShakeDuration = Durations[WeaponIndex];
    ShakeTimeRemaining = ShakeDuration;
}

void AEarthOrbitPawn::SelectWeapon(int32 Index)
{
    WeaponIndex = FMath::Clamp(Index, 0, 2);
    static const TCHAR* Names[] = {TEXT("KINETIC ROD"), TEXT("PLASMA LANCE"), TEXT("MANTLE CHARGE")};
    StatusMessage = FString::Printf(TEXT("WEAPON SELECTED  //  %s"), Names[WeaponIndex]);
    MessageTimer = 1.0f;
}

void AEarthOrbitPawn::SelectWeapon1() { SelectWeapon(0); }
void AEarthOrbitPawn::SelectWeapon2() { SelectWeapon(1); }
void AEarthOrbitPawn::SelectWeapon3() { SelectWeapon(2); }

void AEarthOrbitPawn::ResetSimulation()
{
    for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It) It->ResetPlanet();
    ImpactCount = 0;
    CooldownRemaining = 0.0f;
    StatusMessage = TEXT("PLANET RESTORED");
    MessageTimer = 1.2f;
}
