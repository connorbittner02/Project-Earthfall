#include "EarthOrbitPawn.h"
#include "EarthfallHUD.h"
#include "EarthfallPlanet.h"
#include "EarthfallPopulationSubsystem.h"
#include "EarthfallProjectile.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float MinOrbitDistanceScale = 1.06f;
constexpr float MaxOrbitDistance = 1200.0f;
constexpr float DefaultOrbitDistance = 475.0f;
/** The preview ring rides above the high cloud deck so weather never hides it. */
constexpr float PreviewAltitudeScale = 1.023f;
}

AEarthOrbitPawn::AEarthOrbitPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	// A long lens keeps the limb a true circle; wider angles egg the planet.
	Camera->SetFieldOfView(45.0f);

	TargetPreviewRing = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TargetPreviewRing"));
	TargetPreviewRing->SetupAttachment(Root);
	TargetPreviewRing->SetUsingAbsoluteLocation(true);
	TargetPreviewRing->SetUsingAbsoluteRotation(true);
	TargetPreviewRing->SetUsingAbsoluteScale(true);
	TargetPreviewRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TargetPreviewRing->SetGenerateOverlapEvents(false);
	TargetPreviewRing->SetCastShadow(false);
	TargetPreviewRing->SetTranslucentSortPriority(30);
	TargetPreviewRing->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Preview(TEXT("/Game/Earthfall/Materials/M_FXTrail.M_FXTrail"));
	PreviewMaterial = Preview.Succeeded() ? Preview.Object : nullptr;

	for (int32& Index : MultiplierIndices) Index = EarthfallMultipliers::DefaultIndex;
	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AEarthOrbitPawn::BeginPlay()
{
	Super::BeginPlay();
	// Open on the sunlit hemisphere, turned a little so the terminator and the
	// night side's city lights are both in frame.
	const FRotator SunRotation = EarthfallSolar::ComputeToSun().Rotation();
	Yaw = SunRotation.Yaw + 32.0f;
	Pitch = FMath::Clamp(SunRotation.Pitch + 12.0f, -80.0f, 80.0f);
	Distance = DefaultOrbitDistance;
	BuildTargetPreviewRing();

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

	Yaw += PendingOrbit.X;
	Pitch = FMath::Clamp(Pitch + PendingOrbit.Y, -80.0f, 80.0f);
	PendingOrbit = FVector2D::ZeroVector;

	// The camera sits at (Yaw, Pitch) over the planet and looks at its centre.
	FVector CameraLocation = FRotator(Pitch, Yaw, 0.0f).Vector() * Distance;
	FRotator CameraRotation = (-CameraLocation).Rotation();

	ShakeTimeRemaining = FMath::Max(0.0f, ShakeTimeRemaining - DeltaSeconds);
	ShakePhase += DeltaSeconds;
	if (ShakeTimeRemaining > 0.0f)
	{
		const float Fade = ShakeTimeRemaining / FMath::Max(ShakeDuration, KINDA_SMALL_NUMBER);
		const float Strength = ShakeStrength * Fade * Fade;
		// Translation scales with orbit distance so the jolt covers the same
		// fraction of the screen whether zoomed in or out.
		const float Sway = Strength * Distance / DefaultOrbitDistance;
		CameraLocation += FVector(FMath::Sin(ShakePhase * 37.0f), FMath::Sin(ShakePhase * 53.0f + 1.7f), FMath::Cos(ShakePhase * 29.0f) * 0.55f) * Sway;
		CameraRotation.Pitch += FMath::Sin(ShakePhase * 31.0f) * Strength * 0.12f;
		CameraRotation.Yaw += FMath::Cos(ShakePhase * 43.0f) * Strength * 0.12f;
		CameraRotation.Roll += FMath::Sin(ShakePhase * 23.0f + 0.6f) * Strength * 0.08f;
	}
	Camera->SetWorldLocation(CameraLocation);
	Camera->SetWorldRotation(CameraRotation);
	UpdateAimState();
	UpdateTargetPreviewRing();
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
	PlayerInputComponent->BindAction(TEXT("Weapon4"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon4);
	PlayerInputComponent->BindAction(TEXT("Weapon5"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon5);
	PlayerInputComponent->BindAction(TEXT("Weapon6"), IE_Pressed, this, &AEarthOrbitPawn::SelectWeapon6);
	PlayerInputComponent->BindAction(TEXT("ResetSimulation"), IE_Pressed, this, &AEarthOrbitPawn::ResetSimulation);
	// Weapon multipliers: Q/E yield, A/D blast radius, Z/C fallout.
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AEarthOrbitPawn::YieldDown);
	PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &AEarthOrbitPawn::YieldUp);
	PlayerInputComponent->BindKey(EKeys::A, IE_Pressed, this, &AEarthOrbitPawn::RadiusDown);
	PlayerInputComponent->BindKey(EKeys::D, IE_Pressed, this, &AEarthOrbitPawn::RadiusUp);
	PlayerInputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AEarthOrbitPawn::FalloutDown);
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &AEarthOrbitPawn::FalloutUp);
	// H cycles Full HUD -> ticker-only cinema mode -> hidden. Tab or MMB toggles inspect mode.
	PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &AEarthOrbitPawn::CycleHudMode);
	PlayerInputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AEarthOrbitPawn::ToggleInspectMode);
	PlayerInputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &AEarthOrbitPawn::ToggleInspectMode);
}

void AEarthOrbitPawn::OrbitYawInput(float Value)
{
	if (bIsOrbiting && GetWorld()) PendingOrbit.X += Value * 90.0f * GetWorld()->GetDeltaSeconds();
}

void AEarthOrbitPawn::OrbitPitchInput(float Value)
{
	if (bIsOrbiting && GetWorld()) PendingOrbit.Y += Value * 90.0f * GetWorld()->GetDeltaSeconds();
}

void AEarthOrbitPawn::BeginOrbit()
{
	bIsOrbiting = true;
}

void AEarthOrbitPawn::EndOrbit()
{
	bIsOrbiting = false;
	PendingOrbit = FVector2D::ZeroVector;
}

void AEarthOrbitPawn::ZoomInput(float Value)
{
	const AEarthfallPlanet* Planet = FindPlanet();
	const float Radius = Planet ? Planet->PlanetRadius : 100.0f;
	const float Altitude = FMath::Max(0.0f, Distance - Radius);
	const float ZoomStep = FMath::Clamp(Altitude * 0.11f, 0.65f, 24.0f);
	Distance = FMath::Clamp(Distance - Value * ZoomStep, Radius * MinOrbitDistanceScale, MaxOrbitDistance);
}

void AEarthOrbitPawn::SetOrbitView(float LongitudeDegrees, float LatitudeDegrees, float InDistance)
{
	const AEarthfallPlanet* Planet = FindPlanet();
	const float Radius = Planet ? Planet->PlanetRadius : 100.0f;
	// East is -Y in Unreal's left-handed world, so longitude runs against yaw.
	Yaw = -LongitudeDegrees;
	Pitch = FMath::Clamp(LatitudeDegrees, -80.0f, 80.0f);
	Distance = FMath::Clamp(InDistance, Radius * MinOrbitDistanceScale, MaxOrbitDistance);
}

AEarthfallPlanet* AEarthOrbitPawn::FindPlanet() const
{
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AEarthfallPlanet> It(World); It; ++It) return *It;
	}
	return nullptr;
}

void AEarthOrbitPawn::SetStatus(const FString& Message, float Seconds)
{
	StatusMessage = Message;
	MessageTimer = Seconds;
}

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------
void AEarthOrbitPawn::SelectWeapon(int32 Index)
{
	WeaponIndex = FMath::Clamp(Index, 0, EarthfallWeapons::Count - 1);
	SetStatus(FString::Printf(TEXT("WEAPON SELECTED  //  %s"), EarthfallWeapons::Get(WeaponIndex).Name), 1.0f);
}

float AEarthOrbitPawn::GetMultiplier(EEarthfallMultiplier Kind) const
{
	const TConstArrayView<float> Steps = EarthfallMultipliers::Steps(Kind);
	return Steps[FMath::Clamp(MultiplierIndices[static_cast<int32>(Kind)], 0, Steps.Num() - 1)];
}

void AEarthOrbitPawn::SetMultiplierIndex(EEarthfallMultiplier Kind, int32 Index)
{
	const TConstArrayView<float> Steps = EarthfallMultipliers::Steps(Kind);
	MultiplierIndices[static_cast<int32>(Kind)] = FMath::Clamp(Index, 0, Steps.Num() - 1);
}

void AEarthOrbitPawn::AdjustMultiplier(EEarthfallMultiplier Kind, int32 Delta)
{
	SetMultiplierIndex(Kind, MultiplierIndices[static_cast<int32>(Kind)] + Delta);
	SetStatus(FString::Printf(TEXT("%s MULTIPLIER  //  x%g"), EarthfallMultipliers::Label(Kind), GetMultiplier(Kind)), 1.0f);
}

float AEarthOrbitPawn::GetCurrentBlastRadius() const
{
	return FEarthfallStrike::BlastRadius(WeaponIndex,
		GetMultiplier(EEarthfallMultiplier::Yield), GetMultiplier(EEarthfallMultiplier::BlastRadius));
}

float AEarthOrbitPawn::GetCooldownRatio() const
{
	return CooldownDuration > 0.0f ? CooldownRemaining / CooldownDuration : 0.0f;
}

void AEarthOrbitPawn::FireImpact()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Camera) return;

	// A click on a HUD control adjusts it instead of firing through it.
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (PC->GetMousePosition(MouseX, MouseY))
	{
		if (AEarthfallHUD* HUD = Cast<AEarthfallHUD>(PC->GetHUD()))
		{
			if (HUD->HandleClick(FVector2D(MouseX, MouseY), this)) return;
		}
	}

	FVector Start;
	FVector Direction;
	if (!PC->DeprojectMousePositionToWorld(Start, Direction)) return;
	FHitResult Hit;
	AEarthfallPlanet* Planet = FindPlanet();
	if (bInspectMode)
	{
		if (Planet && GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Direction * 5000.0f, ECC_Visibility))
		{
			InspectDirection(Planet->GetActorTransform().InverseTransformVectorNoScale(Hit.ImpactPoint - Planet->GetActorLocation()));
		}
		return;
	}
	if (CooldownRemaining > 0.0f)
	{
		SetStatus(TEXT("WEAPON RECHARGING"), 0.7f);
		return;
	}
	if (Planet && GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Direction * 5000.0f, ECC_Visibility))
	{
		LaunchStrike(Planet, Hit.ImpactPoint);
		CooldownDuration = EarthfallWeapons::Get(WeaponIndex).Cooldown;
		CooldownRemaining = CooldownDuration;
	}
	else
	{
		SetStatus(TEXT("NO PLANETARY CONTACT"), 0.8f);
	}
}

bool AEarthOrbitPawn::FireAtDirection(const FVector& PlanetLocalDirection)
{
	AEarthfallPlanet* Planet = FindPlanet();
	if (!Planet) return false;
	const FVector WorldDirection = Planet->GetActorTransform().TransformVectorNoScale(PlanetLocalDirection.GetSafeNormal());
	LaunchStrike(Planet, Planet->GetActorLocation() + WorldDirection * Planet->PlanetRadius);
	return true;
}

void AEarthOrbitPawn::LaunchStrike(AEarthfallPlanet* Planet, const FVector& ImpactPoint)
{
	if (!Planet || !GetWorld() || !Camera) return;
	const FEarthfallStrike Strike = FEarthfallStrike::Make(ImpactPoint, WeaponIndex,
		GetMultiplier(EEarthfallMultiplier::Yield), GetMultiplier(EEarthfallMultiplier::BlastRadius),
		GetMultiplier(EEarthfallMultiplier::Fallout));

	// Munitions leave from just off-axis of the player's view, alternating sides,
	// so each launch is seen streaking away toward the target.
	const FVector Muzzle = Camera->GetComponentLocation() + Camera->GetForwardVector() * 16.0f
		+ Camera->GetRightVector() * (bLaunchFromRight ? 6.0f : -6.0f) - Camera->GetUpVector() * 3.6f;
	bLaunchFromRight = !bLaunchFromRight;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AEarthfallProjectile* Projectile = GetWorld()->SpawnActor<AEarthfallProjectile>(
		AEarthfallProjectile::StaticClass(), Muzzle, FRotator::ZeroRotator, Params))
	{
		Projectile->Launch(Planet, this, Muzzle, Strike);
		++LaunchCount;
		SetStatus(FString::Printf(TEXT("LAUNCH  //  %s"), EarthfallWeapons::Get(WeaponIndex).Name), 1.2f);
	}
}

void AEarthOrbitPawn::NotifyStrikeLanded(const FEarthfallStrike& Strike)
{
	++ImpactCount;
	TriggerImpactShake(Strike);
	SetStatus(FString::Printf(TEXT("IMPACT CONFIRMED  //  #%04d"), ImpactCount), 1.4f);
}

void AEarthOrbitPawn::TriggerImpactShake(const FEarthfallStrike& Strike)
{
	if (!Camera) return;
	const FEarthfallWeaponSpec& Spec = EarthfallWeapons::Get(Strike.WeaponIndex);
	// Closer detonations and bigger yields hit harder.
	const float Range = FVector::Dist(Camera->GetComponentLocation(), Strike.WorldLocation);
	const float Proximity = FMath::Clamp(DefaultOrbitDistance * 0.8f / FMath::Max(Range, 1.0f), 0.35f, 3.0f);
	const float YieldGain = FMath::Pow(FMath::Max(Strike.YieldMultiplier, 0.01f), 0.25f);
	const float Strength = Spec.ShakeStrength * Proximity * YieldGain;
	// A stronger jolt already in progress is never replaced by a weaker one.
	if (ShakeTimeRemaining > 0.0f && ShakeStrength * (ShakeTimeRemaining / FMath::Max(ShakeDuration, KINDA_SMALL_NUMBER)) > Strength) return;
	ShakeStrength = FMath::Min(Strength, 14.0f);
	ShakeDuration = Spec.ShakeDuration * (0.8f + 0.2f * YieldGain);
	ShakeTimeRemaining = ShakeDuration;
}

void AEarthOrbitPawn::CycleHudMode()
{
	HudMode = HudMode == EEarthfallHudMode::Full ? EEarthfallHudMode::TickerOnly
		: (HudMode == EEarthfallHudMode::TickerOnly ? EEarthfallHudMode::Hidden : EEarthfallHudMode::Full);
}

void AEarthOrbitPawn::ToggleInspectMode()
{
	bInspectMode = !bInspectMode;
	if (!bInspectMode)
	{
		InspectedCountry = 0;
		if (AEarthfallPlanet* Planet = FindPlanet()) Planet->SetHighlightedCountry(0);
	}
	SetStatus(bInspectMode ? TEXT("INSPECT MODE  //  CLICK A COUNTRY") : TEXT("WEAPONS LIVE"), 1.2f);
}

void AEarthOrbitPawn::InspectDirection(const FVector& PlanetLocalDirection)
{
	UEarthfallPopulationSubsystem* Population = GetWorld() ? GetWorld()->GetSubsystem<UEarthfallPopulationSubsystem>() : nullptr;
	if (!Population) return;
	bInspectMode = true;
	InspectedCountry = Population->GetCountryAtDirection(PlanetLocalDirection);
	if (AEarthfallPlanet* Planet = FindPlanet()) Planet->SetHighlightedCountry(InspectedCountry);
	if (InspectedCountry > 0) Population->AnnounceCountry(InspectedCountry);
	else SetStatus(TEXT("INTERNATIONAL WATERS"), 1.0f);
}

void AEarthOrbitPawn::ResetSimulation()
{
	if (!GetWorld()) return;
	for (TActorIterator<AEarthfallProjectile> It(GetWorld()); It; ++It) It->Destroy();
	if (AEarthfallPlanet* Planet = FindPlanet()) Planet->ResetPlanet();
	ImpactCount = 0;
	LaunchCount = 0;
	CooldownRemaining = 0.0f;
	ShakeTimeRemaining = 0.0f;
	SetStatus(TEXT("PLANET RESTORED"), 1.2f);
}

// ---------------------------------------------------------------------------
// Aiming
// ---------------------------------------------------------------------------
void AEarthOrbitPawn::UpdateAimState()
{
	bAimOnPlanet = false;
	AimHit = FHitResult();
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !GetWorld()) return;
	FVector Start;
	FVector Direction;
	if (!PC->DeprojectMousePositionToWorld(Start, Direction)) return;
	bAimOnPlanet = GetWorld()->LineTraceSingleByChannel(AimHit, Start, Start + Direction * 5000.0f, ECC_Visibility);
}

void AEarthOrbitPawn::BuildTargetPreviewRing()
{
	if (!TargetPreviewRing || !PreviewMaterial) return;
	const int32 VertexCount = (PreviewSegments + 1) * 2;
	TArray<FVector> Vertices;
	TArray<FVector> RingNormals;
	TArray<FVector2D> RingUVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> RingTangents;
	TArray<int32> Indices;
	Vertices.Init(FVector::ZeroVector, VertexCount);
	RingNormals.Init(FVector::UpVector, VertexCount);
	Colors.Init(FLinearColor::White, VertexCount);
	RingTangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), VertexCount);
	RingUVs.SetNum(VertexCount);
	for (int32 Segment = 0; Segment <= PreviewSegments; ++Segment)
	{
		const float U = static_cast<float>(Segment) / PreviewSegments;
		RingUVs[Segment * 2] = FVector2D(U, 0.0f);
		RingUVs[Segment * 2 + 1] = FVector2D(U, 1.0f);
	}
	for (int32 Segment = 0; Segment < PreviewSegments; ++Segment)
	{
		const int32 A = Segment * 2;
		Indices.Append({A, A + 1, A + 2, A + 2, A + 1, A + 3});
	}
	TargetPreviewRing->SetWorldTransform(FTransform::Identity);
	TargetPreviewRing->CreateMeshSection_LinearColor(0, Vertices, Indices, RingNormals, RingUVs, Colors, RingTangents, false);
	PreviewMID = UMaterialInstanceDynamic::Create(PreviewMaterial, this);
	TargetPreviewRing->SetMaterial(0, PreviewMID);
}

void AEarthOrbitPawn::UpdateTargetPreviewRing()
{
	if (!TargetPreviewRing || !PreviewMID || !TargetPreviewRing->GetProcMeshSection(0)) return;
	const AEarthfallPlanet* Planet = FindPlanet();
	if (!bAimOnPlanet || !Planet)
	{
		TargetPreviewRing->SetVisibility(false);
		return;
	}

	// The preview is a band drawn on the sphere at the blast's true angular
	// radius, so it stays honest even when a multiplier makes it continental.
	const FVector Center = Planet->GetActorLocation();
	const FVector Normal = (AimHit.ImpactPoint - Center).GetSafeNormal();
	const float Angle = FMath::Clamp(GetCurrentBlastRadius() / Planet->PlanetRadius, 0.01f, 2.6f);
	const float Altitude = Planet->PlanetRadius * PreviewAltitudeScale;
	// Constant on-screen thickness regardless of zoom.
	const float HalfWidth = FMath::Clamp((Distance - Planet->PlanetRadius) * 0.0042f / Planet->PlanetRadius, 0.0025f, 0.03f);
	FVector TangentA;
	FVector TangentB;
	Normal.FindBestAxisVectors(TangentA, TangentB);

	const int32 VertexCount = (PreviewSegments + 1) * 2;
	TArray<FVector> Vertices;
	TArray<FVector> RingNormals;
	TArray<FVector2D> RingUVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> RingTangents;
	Vertices.SetNumUninitialized(VertexCount);
	RingNormals.SetNumUninitialized(VertexCount);
	RingUVs.SetNumUninitialized(VertexCount);
	Colors.Init(FLinearColor::White, VertexCount);
	RingTangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), VertexCount);
	for (int32 Segment = 0; Segment <= PreviewSegments; ++Segment)
	{
		const float U = static_cast<float>(Segment) / PreviewSegments;
		const float Azimuth = U * 2.0f * PI;
		const FVector Radial = TangentA * FMath::Cos(Azimuth) + TangentB * FMath::Sin(Azimuth);
		const FVector Inner = Normal * FMath::Cos(Angle - HalfWidth) + Radial * FMath::Sin(Angle - HalfWidth);
		const FVector Outer = Normal * FMath::Cos(Angle + HalfWidth) + Radial * FMath::Sin(Angle + HalfWidth);
		Vertices[Segment * 2] = Center + Inner * Altitude;
		Vertices[Segment * 2 + 1] = Center + Outer * Altitude;
		RingNormals[Segment * 2] = Inner;
		RingNormals[Segment * 2 + 1] = Outer;
		RingUVs[Segment * 2] = FVector2D(U, 0.0f);
		RingUVs[Segment * 2 + 1] = FVector2D(U, 1.0f);
	}
	TargetPreviewRing->UpdateMeshSection_LinearColor(0, Vertices, RingNormals, RingUVs, Colors, RingTangents);
	TargetPreviewRing->SetVisibility(true);

	const FLinearColor Tone = EarthfallWeapons::Get(WeaponIndex).Color;
	const float Ready = CooldownRemaining > 0.0f ? 0.9f : 2.4f;
	PreviewMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(Tone.R, Tone.G, Tone.B, Ready));
}
