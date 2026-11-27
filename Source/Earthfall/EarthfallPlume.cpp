#include "EarthfallPlume.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
/** After the cloud and atmosphere shells (10-12), before the fireball and ring (20). */
constexpr int32 PlumeSortPriority = 14;
/** The engine sphere is 100 units across. */
constexpr float PlumeShapeHalfExtent = 50.0f;
const FLinearColor AshColor(0.105f, 0.085f, 0.070f, 1.0f);
const FLinearColor SteamColor(0.62f, 0.66f, 0.70f, 1.0f);
const FLinearColor SprayColor(0.78f, 0.86f, 0.92f, 1.0f);
const FLinearColor FoamColor(0.82f, 0.93f, 1.00f, 1.0f);
const FTransform PlumeHiddenTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);

/** A puff: a sphere scaled to (wide, wide, tall) world units, in the actor's space. */
FTransform Puff(const FQuat& Spin, const FVector& Location, float Wide, float Tall)
{
	if (Wide < 0.01f || Tall < 0.01f) return PlumeHiddenTransform;
	return FTransform(Spin, Location, FVector(Wide, Wide, Tall) / PlumeShapeHalfExtent);
}
}

AEarthfallPlume::AEarthfallPlume()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Scene);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Volume(TEXT("/Game/Earthfall/Materials/M_FXVolume.M_FXVolume"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Puffs(TEXT("/Game/Earthfall/Materials/M_FXVolumeInstanced.M_FXVolumeInstanced"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Foam(TEXT("/Game/Earthfall/Materials/M_FXSpray.M_FXSpray"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Earthfall/Materials/M_FXGlow.M_FXGlow"));
	VolumeMaterial = Volume.Succeeded() ? Volume.Object : nullptr;
	PuffMaterial = Puffs.Succeeded() ? Puffs.Object : nullptr;
	FoamMaterial = Foam.Succeeded() ? Foam.Object : nullptr;
	GlowMaterial = Glow.Succeeded() ? Glow.Object : nullptr;
	UStaticMesh* SphereMesh = Sphere.Succeeded() ? Sphere.Object : nullptr;

	auto Configure = [Scene](UPrimitiveComponent* Component, int32 SortPriority)
	{
		Component->SetupAttachment(Scene);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);
		Component->SetReceivesDecals(false);
		Component->SetTranslucentSortPriority(SortPriority);
		Component->SetVisibility(false);
	};

	Column = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Column"));
	Column->SetStaticMesh(SphereMesh);
	Configure(Column, PlumeSortPriority);
	Cap = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cap"));
	Cap->SetStaticMesh(SphereMesh);
	Configure(Cap, PlumeSortPriority);
	EmberGlow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EmberGlow"));
	EmberGlow->SetStaticMesh(SphereMesh);
	Configure(EmberGlow, PlumeSortPriority - 1);
	Spray = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Spray"));
	Spray->SetStaticMesh(SphereMesh);
	Configure(Spray, PlumeSortPriority + 1);
	FoamRing = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FoamRing"));
	Configure(FoamRing, PlumeSortPriority - 1);
}

float AEarthfallPlume::HeightFraction(float Age, bool bOcean)
{
	// Fast at first, then easing into the stratosphere.
	const float Rise = 1.0f - FMath::Square(1.0f - FMath::Clamp(Age / RiseSeconds, 0.0f, 1.0f));
	return (bOcean ? SteamTop : AshTop) * Rise;
}

void AEarthfallPlume::InitPlume(const FEarthfallPlumeParams& InParams)
{
	Params = InParams;
	Age = 0.0f;
	SetLifeSpan(LifeSeconds + 0.5f);
	if (!VolumeMaterial || !PuffMaterial) return;

	// Every puff keeps the same offsets and orientation for its whole life.
	FRandomStream Random(GetUniqueID());
	const int32 PuffCount = ColumnPuffs + SprayPuffs;
	Jitter.Reserve(PuffCount);
	Spin.Reserve(PuffCount);
	for (int32 Index = 0; Index < PuffCount; ++Index)
	{
		Jitter.Add(FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-1.0f, 1.0f), Random.FRand()));
		// Yaw only: the squash stays along the column while the noise still differs.
		Spin.Add(FRotator(0.0f, Random.FRandRange(-180.0f, 180.0f), 0.0f).Quaternion());
	}

	const FLinearColor Sun(Params.SunDirection.X, Params.SunDirection.Y, Params.SunDirection.Z, 0.0f);
	// Shape = (edge softness exponent, noise contrast, noise scale, scroll speed).
	auto MakeMID = [this, &Sun](UMaterialInterface* Material, const FLinearColor& Shape)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
		MID->SetVectorParameterValue(TEXT("Shape"), Shape);
		MID->SetVectorParameterValue(TEXT("SunDirection"), Sun);
		return MID;
	};

	ColumnMID = MakeMID(PuffMaterial, FLinearColor(1.3f, 0.95f, 5.5f, 0.22f));
	Column->SetMaterial(0, ColumnMID);
	ColumnTransforms.Init(PlumeHiddenTransform, ColumnPuffs);
	Column->AddInstances(ColumnTransforms, false, false);
	Column->SetVisibility(true);

	CapMID = MakeMID(VolumeMaterial, FLinearColor(1.5f, 0.9f, 3.0f, 0.10f));
	Cap->SetMaterial(0, CapMID);
	Cap->SetVisibility(true);

	if (!Params.bOcean && GlowMaterial)
	{
		// Shape = (falloff exponent, hot-core boost, noise scale, turbulence), as in fx.hlsl.
		GlowMID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		GlowMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(3.2f, 0.0f, 2.4f, 1.0f));
		EmberGlow->SetMaterial(0, GlowMID);
		EmberGlow->SetVisibility(true);
	}

	if (Params.bOcean)
	{
		SprayMID = MakeMID(PuffMaterial, FLinearColor(2.6f, 1.9f, 6.5f, 0.55f));
		Spray->SetMaterial(0, SprayMID);
		SprayTransforms.Init(PlumeHiddenTransform, SprayPuffs);
		Spray->AddInstances(SprayTransforms, false, false);
		Spray->SetVisibility(true);
		if (FoamMaterial)
		{
			BuildFoamRing();
			// Shape = (taper exponent toward the outer edge, streak contrast).
			FoamMID = UMaterialInstanceDynamic::Create(FoamMaterial, this);
			FoamMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(1.1f, 0.4f, 9.0f, 3.5f));
			FoamRing->SetMaterial(0, FoamMID);
			FoamRing->SetVisibility(true);
		}
	}

	bInitialised = true;
	UpdateShape();
}

void AEarthfallPlume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bInitialised) return;
	Age += DeltaSeconds;
	UpdateShape();
}

void AEarthfallPlume::UpdateShape()
{
	const float Radius = Params.PlanetRadius;
	const float Height = FMath::Max(HeightFraction(Age, Params.bOcean) * Radius, 0.02f);
	const float Rise = FMath::Clamp(Age / RiseSeconds, 0.0f, 1.0f);
	// The whole plume thins out over its last third.
	const float Fade = 1.0f - FMath::SmoothStep(LifeSeconds * 0.65f, LifeSeconds, Age);
	// Daylight at ground zero: a column on the night side is a silhouette.
	const float Day = 0.10f + 0.90f * FMath::SmoothStep(-0.15f, 0.25f,
		static_cast<float>(FVector::DotProduct(GetActorUpVector(), Params.SunDirection)));

	const float Girth = FMath::Clamp(Params.BlastRadius * 0.10f, 0.45f, 2.2f) * (Params.bOcean ? 0.75f : 1.0f);
	const FLinearColor Body = Params.bOcean ? SteamColor : AshColor;
	auto Tint = [Day](const FLinearColor& Colour, float Opacity)
	{
		return FLinearColor(Colour.R * Day, Colour.G * Day, Colour.B * Day, Opacity);
	};

	// --- Stem: a stack of overlapping puffs, fatter toward the top, wandering a
	// little off the axis so the column never reads as a cylinder.
	int32 Slot = 0;
	for (int32 Index = 0; Index < StemPuffs; ++Index, ++Slot)
	{
		const float Along = (Index + 0.5f) / StemPuffs;
		const FVector& Offset = Jitter[Slot];
		const float Wide = FMath::Min(Girth * (1.05f + 0.55f * Offset.Z) * (0.80f + 0.45f * Along), Height * 0.75f + 0.25f);
		const FVector Location(Offset.X * Girth * 0.38f, Offset.Y * Girth * 0.38f, Along * Height);
		ColumnTransforms[Slot] = Puff(Spin[Slot], Location, Wide, Wide * 1.20f);
	}

	// --- Skirt: where the column punches through the cloud decks it spreads sideways.
	const float SkirtAltitude = Radius * 0.022f;
	const float SkirtGrow = FMath::SmoothStep(SkirtAltitude * 0.8f, SkirtAltitude * 2.2f, Height);
	const float SkirtRadius = Girth * (1.3f + 1.5f * SkirtGrow);
	for (int32 Index = 0; Index < SkirtPuffs; ++Index, ++Slot)
	{
		const float Angle = 2.0f * PI * (Index + 0.35f * Jitter[Slot].X) / SkirtPuffs;
		const float Wide = Girth * (1.0f + 0.5f * Jitter[Slot].Z) * SkirtGrow;
		const FVector Location(FMath::Cos(Angle) * SkirtRadius, FMath::Sin(Angle) * SkirtRadius,
			FMath::Min(SkirtAltitude, Height) + Jitter[Slot].Y * Girth * 0.25f);
		ColumnTransforms[Slot] = Puff(Spin[Slot], Location, Wide, Wide * 0.65f);
	}

	// --- Cap: one broad body with lobes round its rim, still spreading after the rise stops.
	const float Spread = FMath::Clamp(Age / (RiseSeconds * 4.0f), 0.0f, 1.0f);
	const float CapRadius = Girth * (1.4f + 2.6f * Rise + 2.2f * Spread);
	const float CapThickness = Girth * (0.9f + 0.7f * Rise);
	Cap->SetRelativeLocation(FVector(0.0f, 0.0f, Height));
	Cap->SetRelativeScale3D(FVector(CapRadius, CapRadius, CapThickness) / PlumeShapeHalfExtent);
	if (CapMID) CapMID->SetVectorParameterValue(TEXT("Tint"), Tint(Body, 1.0f * Fade));
	for (int32 Index = 0; Index < CapLobes; ++Index, ++Slot)
	{
		const float Angle = 2.0f * PI * (Index + 0.35f * Jitter[Slot].X) / CapLobes;
		const float Wide = CapRadius * (0.42f + 0.16f * Jitter[Slot].Z);
		const FVector Location(FMath::Cos(Angle) * CapRadius * 0.62f, FMath::Sin(Angle) * CapRadius * 0.62f,
			Height - CapThickness * (0.10f + 0.30f * Jitter[Slot].Z));
		ColumnTransforms[Slot] = Puff(Spin[Slot], Location, Wide, CapThickness * 0.85f);
	}
	Column->BatchUpdateInstancesTransforms(0, ColumnTransforms, false, true, false);
	if (ColumnMID) ColumnMID->SetVectorParameterValue(TEXT("Tint"), Tint(Body, 1.0f * Fade));

	if (GlowMID)
	{
		// Embers under the cap: bright while the column is young, a dull red after.
		const float Heat = (0.25f + 0.75f * FMath::Exp(-Age / 14.0f)) * Fade;
		EmberGlow->SetRelativeLocation(FVector(0.0f, 0.0f, FMath::Max(Height - CapThickness * 0.75f, Height * 0.5f)));
		EmberGlow->SetRelativeScale3D(FVector(Girth * 1.15f, Girth * 1.15f, CapThickness * 0.9f) / PlumeShapeHalfExtent);
		GlowMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.0f, 0.42f, 0.08f, 0.30f * Heat * Day));
	}

	if (!Params.bOcean) return;

	// --- Sea spray: thrown up in a second and a half, hangs, then sinks back.
	const float Up = 1.0f - FMath::Square(1.0f - FMath::Clamp(Age / 1.5f, 0.0f, 1.0f));
	const float Collapse = FMath::SmoothStep(5.0f, 12.0f, Age);
	const float SprayHeight = Radius * (0.016f + 0.022f * Params.Strength) * Up * (1.0f - 0.7f * Collapse);
	const float SprayRadius = FMath::Clamp(Params.BlastRadius * 0.34f, 1.2f, 7.0f) * (0.45f + 0.55f * Up + 0.35f * Collapse);
	int32 SpraySlot = 0;
	for (int32 Index = 0; Index < SprayPuffs; ++Index, ++SpraySlot)
	{
		const bool bOuter = Index < OuterSprayPuffs;
		const int32 Ring = bOuter ? OuterSprayPuffs : InnerSprayPuffs;
		const FVector& Offset = Jitter[ColumnPuffs + SpraySlot];
		const float Angle = 2.0f * PI * ((bOuter ? Index : Index - OuterSprayPuffs) + 0.4f * Offset.X) / Ring;
		const float Distance = SprayRadius * (bOuter ? 0.95f : 0.48f) * (1.0f + 0.12f * Offset.Y);
		const float Tall = SprayHeight * (bOuter ? 0.55f : 0.80f) * (0.75f + 0.5f * Offset.Z);
		const float Wide = SprayRadius * (bOuter ? 0.34f : 0.30f) * (0.85f + 0.3f * Offset.Z);
		SprayTransforms[SpraySlot] = Puff(Spin[ColumnPuffs + SpraySlot],
			FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, Tall * 0.55f), Wide, Tall);
	}
	Spray->BatchUpdateInstancesTransforms(0, SprayTransforms, false, true, false);
	if (SprayMID) SprayMID->SetVectorParameterValue(TEXT("Tint"), Tint(SprayColor, 0.72f * Up * (1.0f - Collapse)));
	Spray->SetVisibility(Collapse < 0.999f);

	if (FoamMID)
	{
		// The foam ring keeps widening and churning long after the spray has fallen.
		const float Widen = FMath::SmoothStep(0.0f, 14.0f, Age);
		const float FoamRadius = SprayRadius * (1.15f + 0.95f * Widen);
		FoamRing->SetRelativeLocation(FVector(0.0f, 0.0f, 0.12f));
		FoamRing->SetRelativeScale3D(FVector(FoamRadius, FoamRadius, 1.0f));
		const float FoamFade = Up * (1.0f - FMath::SmoothStep(14.0f, 30.0f, Age));
		FoamMID->SetVectorParameterValue(TEXT("Tint"), FLinearColor(FoamColor.R, FoamColor.G, FoamColor.B, 1.9f * Day * FoamFade));
		FoamRing->SetVisibility(FoamFade > 0.001f);
	}
}

void AEarthfallPlume::BuildFoamRing()
{
	// A flat annulus in unit space: UV.x runs round it and UV.y from the inner
	// edge to the outer, which is where the spray material fades it to nothing.
	constexpr int32 Segments = 96;
	constexpr float InnerRadius = 0.30f;
	constexpr float OuterRadius = 1.60f;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	for (int32 Segment = 0; Segment <= Segments; ++Segment)
	{
		const float Angle = 2.0f * PI * (Segment % Segments) / Segments;
		const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
		for (int32 End = 0; End < 2; ++End)
		{
			Vertices.Add(Outward * (End == 0 ? InnerRadius : OuterRadius));
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(static_cast<float>(Segment) / Segments, End == 0 ? 0.0f : 1.0f));
			Colors.Add(FLinearColor::White);
			Tangents.Add(FProcMeshTangent(-Outward.Y, Outward.X, 0.0f));
		}
	}
	for (int32 Segment = 0; Segment < Segments; ++Segment)
	{
		const int32 A = Segment * 2;
		Triangles.Append({A, A + 2, A + 1, A + 1, A + 2, A + 3});
	}
	FoamRing->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
}
