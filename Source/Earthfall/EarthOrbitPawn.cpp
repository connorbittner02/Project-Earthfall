#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Components/InputComponent.h"
#include "DrawDebugHelpers.h"

AEarthOrbitPawn::AEarthOrbitPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Root);
    PlanetFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PlanetFillLight"));
    PlanetFillLight->SetupAttachment(Camera);
    PlanetFillLight->SetIntensity(9000.0f);
    PlanetFillLight->SetAttenuationRadius(1800.0f);
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
        PC->SetInputMode(FInputModeGameAndUI());
    }
}

void AEarthOrbitPawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    CooldownRemaining = FMath::Max(0.0f, CooldownRemaining - DeltaSeconds);
    MessageTimer = FMath::Max(0.0f, MessageTimer - DeltaSeconds);
    UpdateAimState();
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
    PlayerInputComponent->BindAction(TEXT("Weapon1"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon1);
    PlayerInputComponent->BindAction(TEXT("Weapon2"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon2);
    PlayerInputComponent->BindAction(TEXT("Weapon3"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon3);
    PlayerInputComponent->BindAction(TEXT("ResetSimulation"), IE_Pressed, this, &AEarthOrbitPawn::ResetSimulation);
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
    if (CooldownRemaining > 0.0f)
    {
        StatusMessage = TEXT("WEAPON RECHARGING");
        MessageTimer = 0.7f;
        return;
    }
    const FVector Start = Camera->GetComponentLocation();
    const FVector End = Start + Camera->GetForwardVector() * 5000.0f;
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility))
    {
        for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It)
        {
            static const float Radii[] = {8.0f, 15.0f, 25.0f};
            static const float Energies[] = {50.0f, 100.0f, 180.0f};
            It->ApplyImpact(Hit.ImpactPoint, Radii[WeaponIndex], Energies[WeaponIndex]);
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
    if (!Camera) return;
    const FVector Start = Camera->GetComponentLocation();
    FHitResult Hit;
    bAimOnPlanet = GetWorld()->LineTraceSingleByChannel(
        Hit, Start, Start + Camera->GetForwardVector() * 5000.0f, ECC_Visibility);
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
