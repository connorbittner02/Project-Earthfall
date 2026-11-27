#include "EarthfallEjecta.h"
#include "EarthfallPlanet.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
/** Over the clouds, the plume and the fireball. */
constexpr int32 EjectaSortPriority = 22;
/** The engine sphere is 100 units across. */
constexpr float SphereRadius = 50.0f;
/** Fragments glow from launch for about this long before they go dark. */
constexpr float CoolSeconds = 5.0f;
/** Re-entry heating starts this far above the surface, as a fraction of the radius. */
constexpr float ReentryAltitude = 0.07f;
const FTransform HiddenTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
}

AEarthfallEjecta::AEarthfallEjecta()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Scene);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Earthfall/Materials/M_FXEjecta.M_FXEjecta"));
	EjectaMaterial = Material.Succeeded() ? Material.Object : nullptr;

	auto MakeBatch = [this, Scene](const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Batch = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Batch->SetupAttachment(Scene);
		Batch->SetMobility(EComponentMobility::Movable);
		if (Sphere.Succeeded()) Batch->SetStaticMesh(Sphere.Object);
		Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Batch->SetGenerateOverlapEvents(false);
		Batch->SetCastShadow(false);
		Batch->SetReceivesDecals(false);
		Batch->SetTranslucentSortPriority(EjectaSortPriority);
		// The batch spans the whole sky; never cull it as one object.
		Batch->SetBoundsScale(1000.0f);
		return Batch;
	};
	Rocks = MakeBatch(TEXT("Rocks"));
	Streaks = MakeBatch(TEXT("Streaks"));
}

void AEarthfallEjecta::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AEarthfallPlanet> It(GetWorld()); It; ++It)
	{
		Planet = *It;
		break;
	}
	Fragments.SetNum(MaxFragments);
	RockTransforms.Init(HiddenTransform, MaxFragments);
	StreakTransforms.Init(HiddenTransform, MaxFragments);
	if (EjectaMaterial)
	{
		// Shape = (falloff exponent, hot-core boost, noise scale, turbulence), as in fx.hlsl.
		RockMID = UMaterialInstanceDynamic::Create(EjectaMaterial, this);
		RockMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.0f, 0.34f, 0.07f, 4.0f));
		RockMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(1.6f, 1.2f, 2.0f, 0.0f));
		Rocks->SetMaterial(0, RockMID);
		StreakMID = UMaterialInstanceDynamic::Create(EjectaMaterial, this);
		StreakMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.0f, 0.78f, 0.42f, 9.0f));
		StreakMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(1.4f, 2.0f, 2.0f, 0.0f));
		Streaks->SetMaterial(0, StreakMID);
	}
}

void AEarthfallEjecta::Reset()
{
	for (FFragment& Fragment : Fragments) Fragment.bAlive = false;
	AirborneCount = 0;
}

void AEarthfallEjecta::Launch(const FVector& LocalDirection, int32 Count, float SpeedScale)
{
	if (!Planet || Fragments.Num() != MaxFragments) return;
	const FTransform& PlanetTransform = Planet->GetActorTransform();
	const FVector Up = PlanetTransform.TransformVectorNoScale(LocalDirection).GetSafeNormal();
	const FVector Origin = Planet->GetActorLocation() + Up * Planet->PlanetRadius * 1.002f;
	FVector TangentA;
	FVector TangentB;
	Up.FindBestAxisVectors(TangentA, TangentB);

	FRandomStream Random(FMath::Rand());
	Count = FMath::Clamp(Count, 0, MaxFragments);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FFragment& Fragment = Fragments[NextSlot];
		NextSlot = (NextSlot + 1) % MaxFragments;

		// 45-75 degrees above the horizon, all the way round. The square on the
		// random speed leaves most fragments close and a few thrown far.
		const float Elevation = FMath::DegreesToRadians(Random.FRandRange(45.0f, 75.0f));
		const float Azimuth = Random.FRandRange(0.0f, 2.0f * PI);
		const float Speed = SpeedScale * FMath::Lerp(4.5f, 13.5f, FMath::Square(Random.FRand()));
		const FVector Outward = TangentA * FMath::Cos(Azimuth) + TangentB * FMath::Sin(Azimuth);
		Fragment.Position = Origin + Outward * Random.FRandRange(0.0f, 0.6f);
		Fragment.Velocity = (Up * FMath::Sin(Elevation) + Outward * FMath::Cos(Elevation)) * Speed;
		Fragment.Age = -Random.FRandRange(0.0f, 0.6f);
		Fragment.Size = Random.FRandRange(0.6f, 1.5f);
		Fragment.bMajor = Random.FRand() < 0.015f;
		if (Fragment.bMajor) Fragment.Size *= 1.8f;
		Fragment.bAlive = true;
	}
}

void AEarthfallEjecta::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Planet || !RockMID || Fragments.Num() != MaxFragments) return;

	if (!bInstancesBuilt)
	{
		Rocks->AddInstances(RockTransforms, false, true);
		Streaks->AddInstances(StreakTransforms, false, true);
		bInstancesBuilt = true;
	}

	const FVector Center = Planet->GetActorLocation();
	const float Radius = Planet->PlanetRadius;
	const float Step = FMath::Min(DeltaSeconds, 1.0f / 30.0f);
	const float GravityTerm = SurfaceGravity * Radius * Radius;
	int32 Alive = 0;
	int32 Seeds = 0;
	// Fragments keep a readable size on screen: they grow with distance from the
	// camera, so the curtain shows from high orbit without swamping a close view.
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : Center + FVector(Radius * 4.0f, 0.0f, 0.0f);

	for (int32 Index = 0; Index < MaxFragments; ++Index)
	{
		FFragment& Fragment = Fragments[Index];
		if (!Fragment.bAlive)
		{
			RockTransforms[Index] = HiddenTransform;
			StreakTransforms[Index] = HiddenTransform;
			continue;
		}
		Fragment.Age += Step;
		if (Fragment.Age < 0.0f)
		{
			// Staggered launch: not every fragment leaves in the same frame.
			RockTransforms[Index] = HiddenTransform;
			StreakTransforms[Index] = HiddenTransform;
			++Alive;
			continue;
		}

		// Spherical gravity: straight toward the centre, falling off with the
		// square of the distance. Semi-implicit Euler keeps the arcs stable.
		FVector FromCenter = Fragment.Position - Center;
		const double DistanceSquared = FromCenter.SizeSquared();
		const double Distance = FMath::Sqrt(DistanceSquared);
		Fragment.Velocity -= FromCenter * (GravityTerm / (DistanceSquared * Distance)) * Step;
		Fragment.Position += Fragment.Velocity * Step;
		FromCenter = Fragment.Position - Center;
		const float Altitude = static_cast<float>(FromCenter.Size()) - Radius;

		if (Altitude <= 0.0f)
		{
			Fragment.bAlive = false;
			// Only a few landings a frame may touch the simulation targets.
			if (Fragment.bMajor && Seeds < 3)
			{
				++Seeds;
				Planet->SeedSecondaryImpact(Planet->GetActorTransform().InverseTransformVectorNoScale(FromCenter).GetSafeNormal());
			}
			RockTransforms[Index] = HiddenTransform;
			StreakTransforms[Index] = HiddenTransform;
			continue;
		}
		++Alive;

		const float Speed = static_cast<float>(Fragment.Velocity.Size());
		const FVector Heading = Speed > KINDA_SMALL_NUMBER ? Fragment.Velocity / Speed : FVector::UpVector;
		const FQuat Facing = FRotationMatrix::MakeFromX(Heading).ToQuat();
		const bool bFalling = FVector::DotProduct(Fragment.Velocity, FromCenter) < 0.0;
		// Re-entry: a falling fragment lights up as it meets the air again.
		const float Reentry = bFalling ? 1.0f - FMath::SmoothStep(0.0f, Radius * ReentryAltitude, Altitude) : 0.0f;
		// Launch glow fades as the rock cools, but never to nothing: debris in
		// sunlight still reads as a faint dust curtain.
		const float Cooling = FMath::Max(FMath::Exp(-Fragment.Age / CoolSeconds), 0.18f);

		const float Apparent = FMath::Clamp(static_cast<float>(FVector::Dist(Fragment.Position, Eye)) / 110.0f, 0.45f, 4.5f);
		const float RockRadius = 0.075f * Apparent * Fragment.Size * Cooling * (1.0f - Reentry);
		RockTransforms[Index] = RockRadius > 0.004f
			? FTransform(Facing, Fragment.Position, FVector(RockRadius * (1.0f + Speed * 0.22f), RockRadius, RockRadius) / SphereRadius)
			: HiddenTransform;

		if (Reentry > 0.02f)
		{
			const float StreakRadius = 0.055f * Apparent * Fragment.Size * Reentry;
			const float StreakLength = (0.9f + Speed * 0.40f) * Reentry * Fragment.Size * FMath::Sqrt(Apparent);
			// The streak trails behind the fragment.
			StreakTransforms[Index] = FTransform(Facing, Fragment.Position - Heading * StreakLength * 0.5f,
				FVector(StreakLength * 0.5f, StreakRadius, StreakRadius) / SphereRadius);
		}
		else
		{
			StreakTransforms[Index] = HiddenTransform;
		}
	}

	AirborneCount = Alive;
	// One last upload after the sky empties hides whatever was still drawn.
	if (Alive > 0 || bDrewLastFrame)
	{
		Rocks->BatchUpdateInstancesTransforms(0, RockTransforms, true, true, false);
		Streaks->BatchUpdateInstancesTransforms(0, StreakTransforms, true, true, false);
	}
	bDrewLastFrame = Alive > 0;
}
