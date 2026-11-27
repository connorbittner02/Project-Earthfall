#include "EarthfallProjectile.h"
#include "EarthOrbitPawn.h"
#include "EarthfallPlanet.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float EngineShapeRadius = 50.0f;
/** Effects draw after the planet's cloud and atmosphere shells (priorities 10-12). */
constexpr int32 EffectSortPriority = 21;

struct FProjectileLook
{
	/** Trail width and head-glow radius as a fraction of camera distance: constant on screen. */
	float TrailWidth;
	float GlowRadius;
	float TrailSeconds;
	float TrailIntensity;
	float GlowIntensity;
	FVector HullScale;
	FLinearColor HullColor;
	FLinearColor HullEmissive;
};

const FProjectileLook& LookFor(EEarthfallProjectile Style)
{
	static const FProjectileLook Looks[] = {
		// Rod: thin tungsten needle.
		{0.0042f, 0.0080f, 0.9f, 5.0f, 5.0f, FVector(0.005f, 0.005f, 0.060f), FLinearColor(0.20f, 0.20f, 0.23f), FLinearColor(0.9f, 0.35f, 0.08f)},
		// Beam: no hull, just the lance.
		{0.0075f, 0.0140f, 0.75f, 9.0f, 8.0f, FVector(0.001f), FLinearColor::Black, FLinearColor::Black},
		// Penetrator: heavy drilling warhead.
		{0.0070f, 0.0110f, 1.4f, 5.5f, 6.0f, FVector(0.012f, 0.012f, 0.050f), FLinearColor(0.22f, 0.10f, 0.07f), FLinearColor(1.0f, 0.22f, 0.04f)},
		// Singularity: a dark core.
		{0.0060f, 0.0150f, 1.6f, 4.0f, 7.0f, FVector(0.030f), FLinearColor(0.0f, 0.0f, 0.0f), FLinearColor(0.02f, 0.0f, 0.05f)},
		// Meteor: icy bolide with a long tail.
		{0.0090f, 0.0130f, 2.2f, 5.0f, 6.0f, FVector(0.035f), FLinearColor(0.50f, 0.72f, 0.88f), FLinearColor(0.10f, 0.25f, 0.35f)},
		// Missile: conical warhead on a bright exhaust plume.
		{0.0080f, 0.0120f, 2.0f, 6.5f, 9.0f, FVector(0.014f, 0.014f, 0.070f), FLinearColor(0.78f, 0.80f, 0.84f), FLinearColor(0.15f, 0.12f, 0.10f)},
	};
	return Looks[static_cast<int32>(Style)];
}
}

AEarthfallProjectile::AEarthfallProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Scene);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HullAsset(TEXT("/Game/Earthfall/Materials/M_ProjectileBody.M_ProjectileBody"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowAsset(TEXT("/Game/Earthfall/Materials/M_FXGlow.M_FXGlow"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TrailAsset(TEXT("/Game/Earthfall/Materials/M_FXTrail.M_FXTrail"));
	SphereMesh = Sphere.Succeeded() ? Sphere.Object : nullptr;
	CylinderMesh = Cylinder.Succeeded() ? Cylinder.Object : nullptr;
	ConeMesh = Cone.Succeeded() ? Cone.Object : nullptr;
	HullMaterial = HullAsset.Succeeded() ? HullAsset.Object : nullptr;
	GlowMaterial = GlowAsset.Succeeded() ? GlowAsset.Object : nullptr;
	TrailMaterial = TrailAsset.Succeeded() ? TrailAsset.Object : nullptr;

	auto MakeMesh = [this, Scene](const TCHAR* Name)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Scene);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);
		Component->SetVisibility(false);
		return Component;
	};
	Hull = MakeMesh(TEXT("Hull"));
	HeadGlow = MakeMesh(TEXT("HeadGlow"));
	HeadGlow->SetTranslucentSortPriority(EffectSortPriority);

	Trail = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Trail"));
	Trail->SetupAttachment(Scene);
	// The ribbon is rebuilt in world space every frame.
	Trail->SetUsingAbsoluteLocation(true);
	Trail->SetUsingAbsoluteRotation(true);
	Trail->SetUsingAbsoluteScale(true);
	Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trail->SetGenerateOverlapEvents(false);
	Trail->SetCastShadow(false);
	Trail->SetTranslucentSortPriority(EffectSortPriority);
}

void AEarthfallProjectile::Launch(AEarthfallPlanet* InPlanet, AEarthOrbitPawn* InOwnerPawn, const FVector& InStart, const FEarthfallStrike& InStrike)
{
	Planet = InPlanet;
	OwnerPawn = InOwnerPawn;
	Strike = InStrike;
	Start = InStart;
	const FEarthfallWeaponSpec& Spec = EarthfallWeapons::Get(Strike.WeaponIndex);
	Style = Spec.Projectile;
	Accent = Spec.Color;
	TravelSeconds = FMath::Max(Spec.TravelSeconds, 0.05f);
	PlanetCenter = InPlanet ? InPlanet->GetActorLocation() : FVector::ZeroVector;
	PlanetRadius = InPlanet ? InPlanet->PlanetRadius : 100.0f;
	const FProjectileLook& Look = LookFor(Style);
	TrailSeconds = Look.TrailSeconds;

	// A shallow outward arc reads as a ballistic approach rather than a laser line.
	const FVector Middle = (Start + Strike.WorldLocation) * 0.5f;
	const float Span = FVector::Dist(Start, Strike.WorldLocation);
	Control = Style == EEarthfallProjectile::Beam
		? Middle
		: Middle + (Middle - PlanetCenter).GetSafeNormal() * Span * 0.10f;

	if (Style != EEarthfallProjectile::Beam)
	{
		UStaticMesh* HullMesh = Style == EEarthfallProjectile::Missile ? ConeMesh.Get()
			: ((Style == EEarthfallProjectile::Singularity || Style == EEarthfallProjectile::Meteor) ? SphereMesh.Get() : CylinderMesh.Get());
		Hull->SetStaticMesh(HullMesh);
		Hull->SetWorldScale3D(Look.HullScale);
		if (HullMaterial)
		{
			HullMID = UMaterialInstanceDynamic::Create(HullMaterial, this);
			HullMID->SetVectorParameterValue(TEXT("BaseColor"), Look.HullColor);
			HullMID->SetVectorParameterValue(TEXT("Emissive"), Look.HullEmissive);
			Hull->SetMaterial(0, HullMID);
		}
		Hull->SetVisibility(HullMesh != nullptr);
	}
	if (GlowMaterial && SphereMesh)
	{
		HeadGlow->SetStaticMesh(SphereMesh);
		GlowMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		HeadGlow->SetMaterial(0, GlowMID);
		HeadGlow->SetVisibility(Style != EEarthfallProjectile::Beam);
	}
	if (TrailMaterial)
	{
		TrailMID = UMaterialInstanceDynamic::Create(TrailMaterial, this);
		TrailMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.0f, 1.0f, 1.0f, Look.TrailIntensity));
		BuildTrailTopology();
		Trail->SetMaterial(0, TrailMID);
	}

	History.Reset();
	if (Style == EEarthfallProjectile::Beam)
	{
		// The lance connects instantly; only its brightness has a lifetime.
		for (int32 Index = 0; Index < TrailPoints; ++Index)
		{
			FTrailPoint& Point = History.AddDefaulted_GetRef();
			Point.Position = FMath::Lerp(Start, Strike.WorldLocation, static_cast<float>(Index) / (TrailPoints - 1));
		}
	}
	Age = 0.0f;
	bDetonated = false;
	bLaunched = true;
	SetActorLocation(Start);
	Tick(0.0f);
}

FVector AEarthfallProjectile::PositionAt(float Alpha) const
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	const FVector A = FMath::Lerp(Start, Control, T);
	const FVector B = FMath::Lerp(Control, Strike.WorldLocation, T);
	return FMath::Lerp(A, B, T);
}

FVector AEarthfallProjectile::CameraLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* Controller = World->GetFirstPlayerController())
		{
			if (Controller->PlayerCameraManager) return Controller->PlayerCameraManager->GetCameraLocation();
		}
	}
	return Start;
}

void AEarthfallProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bLaunched) return;
	Age += DeltaSeconds;

	if (!bDetonated)
	{
		// Launch slow, arrive fast.
		const float Alpha = FMath::Clamp(Age / TravelSeconds, 0.0f, 1.0f);
		const float Eased = FMath::Lerp(Alpha, Alpha * Alpha, 0.65f);
		const FVector Position = PositionAt(Eased);
		const FVector Heading = (PositionAt(Eased + 0.01f) - PositionAt(Eased - 0.01f)).GetSafeNormal();
		SetActorLocation(Position);

		if (Style != EEarthfallProjectile::Beam)
		{
			Hull->SetWorldRotation(FRotationMatrix::MakeFromZ(Heading).Rotator());
			if (History.Num() >= TrailPoints) History.RemoveAt(0);
			FTrailPoint& Point = History.AddDefaulted_GetRef();
			Point.Position = Position;
			Point.BirthAge = Age;

			// Air friction: the head flares as it drops into the atmosphere.
			const float Altitude = FVector::Dist(Position, PlanetCenter) - PlanetRadius;
			const float Reentry = 1.0f - FMath::Clamp(Altitude / (PlanetRadius * 0.12f), 0.0f, 1.0f);
			const FProjectileLook& Look = LookFor(Style);
			const float Distance = FVector::Dist(Position, CameraLocation());
			const float Radius = FMath::Clamp(Distance * Look.GlowRadius, 0.12f, 9.0f) * (1.0f + 1.3f * Reentry);
			HeadGlow->SetWorldScale3D(FVector(Radius / EngineShapeRadius));
			if (GlowMID)
			{
				const FLinearColor Tone = FMath::Lerp(Accent, FLinearColor(1.0f, 0.80f, 0.55f), Reentry * 0.8f);
				GlowMID->SetVectorParameterValue(TEXT("Tint"),
					FLinearColor(Tone.R, Tone.G, Tone.B, Look.GlowIntensity * (1.0f + 2.5f * Reentry)));
			}
		}
		if (Alpha >= 1.0f) Detonate();
	}
	else if (Age - AgeAtImpact > TrailSeconds)
	{
		Destroy();
		return;
	}
	UpdateTrail();
}

void AEarthfallProjectile::Detonate()
{
	bDetonated = true;
	AgeAtImpact = Age;
	Hull->SetVisibility(false);
	HeadGlow->SetVisibility(false);
	if (AEarthfallPlanet* Target = Planet.Get()) Target->ApplyStrike(Strike);
	if (AEarthOrbitPawn* Pawn = OwnerPawn.Get()) Pawn->NotifyStrikeLanded(Strike);
}

void AEarthfallProjectile::BuildTrailTopology()
{
	const int32 VertexCount = TrailPoints * 2;
	TArray<FVector> Vertices;
	TArray<FVector> TrailNormals;
	TArray<FVector2D> TrailUVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> TrailTangents;
	TArray<int32> Indices;
	Vertices.Init(Start, VertexCount);
	TrailNormals.Init(FVector::UpVector, VertexCount);
	Colors.Init(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f), VertexCount);
	TrailTangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), VertexCount);
	TrailUVs.SetNum(VertexCount);
	for (int32 Index = 0; Index < TrailPoints; ++Index)
	{
		const float U = static_cast<float>(Index) / (TrailPoints - 1);
		TrailUVs[Index * 2] = FVector2D(U, 0.0f);
		TrailUVs[Index * 2 + 1] = FVector2D(U, 1.0f);
	}
	for (int32 Index = 0; Index < TrailPoints - 1; ++Index)
	{
		const int32 A = Index * 2;
		Indices.Append({A, A + 1, A + 2, A + 2, A + 1, A + 3});
	}
	Trail->SetWorldTransform(FTransform::Identity);
	Trail->CreateMeshSection_LinearColor(0, Vertices, Indices, TrailNormals, TrailUVs, Colors, TrailTangents, false);
}

void AEarthfallProjectile::UpdateTrail()
{
	if (!TrailMID || !Trail->GetProcMeshSection(0) || History.IsEmpty()) return;
	const FProjectileLook& Look = LookFor(Style);
	const FVector Camera = CameraLocation();
	const int32 VertexCount = TrailPoints * 2;
	TArray<FVector> Vertices;
	TArray<FVector> TrailNormals;
	TArray<FVector2D> TrailUVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> TrailTangents;
	Vertices.SetNumUninitialized(VertexCount);
	Colors.SetNumUninitialized(VertexCount);
	TrailUVs.SetNumUninitialized(VertexCount);
	TrailNormals.Init(FVector::UpVector, VertexCount);
	TrailTangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), VertexCount);

	const bool bBeam = Style == EEarthfallProjectile::Beam;
	const int32 Last = History.Num() - 1;
	for (int32 Index = 0; Index < TrailPoints; ++Index)
	{
		// Unused slots collapse onto the newest point as zero-area triangles.
		const int32 Sample = FMath::Min(Index, Last);
		const FVector Position = History[Sample].Position;
		const FVector Along = (History[FMath::Min(Sample + 1, Last)].Position - History[FMath::Max(Sample - 1, 0)].Position).GetSafeNormal();
		const FVector ToCamera = (Camera - Position).GetSafeNormal();
		FVector Side = FVector::CrossProduct(Along, ToCamera).GetSafeNormal();
		if (Side.IsNearlyZero()) Side = FVector::CrossProduct(ToCamera, FVector::UpVector).GetSafeNormal();

		const float Fade = bBeam
			? FMath::Clamp(1.0f - Age / TrailSeconds, 0.0f, 1.0f)
			: FMath::Clamp(1.0f - (Age - History[Sample].BirthAge) / TrailSeconds, 0.0f, 1.0f);
		const float Altitude = FVector::Dist(Position, PlanetCenter) - PlanetRadius;
		const float Reentry = 1.0f - FMath::Clamp(Altitude / (PlanetRadius * 0.12f), 0.0f, 1.0f);
		const float Distance = FVector::Dist(Position, Camera);
		const float Pulse = bBeam ? 0.75f + 0.25f * FMath::Sin(Age * 60.0f + Index * 0.6f) : 1.0f;
		const float HalfWidth = 0.5f * FMath::Clamp(Distance * Look.TrailWidth, 0.05f, 5.0f)
			* (bBeam ? Pulse : (0.25f + 0.75f * Fade)) * (1.0f + 0.9f * Reentry);

		const FLinearColor Tone = FMath::Lerp(Accent, FLinearColor(1.0f, 0.86f, 0.62f), FMath::Max(Fade * Fade * 0.6f, Reentry * 0.8f));
		const float Alpha = Index > Last ? 0.0f : Fade * Fade * (0.55f + 0.45f * Reentry) * Pulse;
		const float U = static_cast<float>(Index) / (TrailPoints - 1);
		Vertices[Index * 2] = Position - Side * HalfWidth;
		Vertices[Index * 2 + 1] = Position + Side * HalfWidth;
		Colors[Index * 2] = Colors[Index * 2 + 1] = FLinearColor(Tone.R, Tone.G, Tone.B, Alpha);
		TrailUVs[Index * 2] = FVector2D(U, 0.0f);
		TrailUVs[Index * 2 + 1] = FVector2D(U, 1.0f);
	}
	Trail->UpdateMeshSection_LinearColor(0, Vertices, TrailNormals, TrailUVs, Colors, TrailTangents);
}
