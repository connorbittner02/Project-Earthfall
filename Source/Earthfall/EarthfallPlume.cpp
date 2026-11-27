#include "EarthfallPlume.h"
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
/** The engine's basic shapes are 100 units across and, for the cylinder and cone, 100 tall. */
constexpr float ShapeHalfExtent = 50.0f;
const FLinearColor AshColor(0.105f, 0.085f, 0.070f, 1.0f);
const FLinearColor SteamColor(0.62f, 0.66f, 0.70f, 1.0f);
const FLinearColor SprayColor(0.80f, 0.92f, 1.00f, 1.0f);
}

AEarthfallPlume::AEarthfallPlume()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Scene);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Spray(TEXT("/Game/Earthfall/Materials/M_FXSpray.M_FXSpray"));
	SprayMaterial = Spray.Succeeded() ? Spray.Object : nullptr;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Volume(TEXT("/Game/Earthfall/Materials/M_FXVolume.M_FXVolume"));
	VolumeMaterial = Volume.Succeeded() ? Volume.Object : nullptr;

	auto MakePart = [this, Scene](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Scene);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCastShadow(false);
		Component->SetReceivesDecals(false);
		Component->SetTranslucentSortPriority(PlumeSortPriority);
		Component->SetVisibility(false);
		return Component;
	};
	Stem = MakePart(TEXT("Stem"), Cylinder.Succeeded() ? Cylinder.Object : nullptr);
	Skirt = MakePart(TEXT("Skirt"), Sphere.Succeeded() ? Sphere.Object : nullptr);
	Cap = MakePart(TEXT("Cap"), Sphere.Succeeded() ? Sphere.Object : nullptr);
	Crown = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Crown"));
	Crown->SetupAttachment(Scene);
	Crown->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Crown->SetGenerateOverlapEvents(false);
	Crown->SetCastShadow(false);
	Crown->SetTranslucentSortPriority(PlumeSortPriority + 1);
	Crown->SetVisibility(false);
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
	if (!VolumeMaterial) return;

	// Shape = (edge softness exponent, noise contrast, noise scale, scroll speed).
	auto MakeMID = [this](UStaticMeshComponent* Part, const FLinearColor& Shape)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(VolumeMaterial, this);
		MID->SetVectorParameterValue(TEXT("Shape"), Shape);
		MID->SetVectorParameterValue(TEXT("SunDirection"),
			FLinearColor(Params.SunDirection.X, Params.SunDirection.Y, Params.SunDirection.Z, 0.0f));
		Part->SetMaterial(0, MID);
		Part->SetVisibility(true);
		return MID;
	};
	StemMID = MakeMID(Stem, FLinearColor(2.1f, 1.7f, 5.0f, 0.80f));
	SkirtMID = MakeMID(Skirt, FLinearColor(2.3f, 1.6f, 3.5f, 0.12f));
	CapMID = MakeMID(Cap, FLinearColor(2.0f, 1.7f, 3.0f, 0.10f));
	if (Params.bOcean && SprayMaterial)
	{
		BuildCrownMesh();
		// Shape = (taper exponent toward the top, streak contrast).
		CrownMID = UMaterialInstanceDynamic::Create(SprayMaterial, this);
		CrownMID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(1.4f, 0.6f, 0.0f, 0.0f));
		Crown->SetMaterial(0, CrownMID);
		Crown->SetVisibility(true);
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
	auto Tint = [&Body, Day](float Opacity)
	{
		return FLinearColor(Body.R * Day, Body.G * Day, Body.B * Day, Opacity);
	};

	// Stem: a slightly sunken cylinder from the ground to the column top.
	Stem->SetRelativeLocation(FVector(0.0f, 0.0f, Height * 0.5f));
	Stem->SetRelativeScale3D(FVector(Girth / ShapeHalfExtent, Girth / ShapeHalfExtent, Height * 1.30f / (2.0f * ShapeHalfExtent)));
	if (StemMID) StemMID->SetVectorParameterValue(TEXT("Tint"), Tint(0.95f * Fade));

	// Skirt: where the column punches through the cloud decks it spreads sideways.
	const float SkirtAltitude = Radius * 0.022f;
	const float SkirtGrow = FMath::SmoothStep(SkirtAltitude * 0.8f, SkirtAltitude * 2.2f, Height);
	const float SkirtRadius = Girth * (1.6f + 1.6f * SkirtGrow);
	Skirt->SetRelativeLocation(FVector(0.0f, 0.0f, FMath::Min(SkirtAltitude, Height)));
	Skirt->SetRelativeScale3D(FVector(SkirtRadius / ShapeHalfExtent, SkirtRadius / ShapeHalfExtent, Girth * 0.95f / ShapeHalfExtent));
	if (SkirtMID) SkirtMID->SetVectorParameterValue(TEXT("Tint"), Tint(0.50f * Fade * SkirtGrow));

	// Cap: rides the top of the stem and keeps spreading after the rise stops.
	const float Spread = FMath::Clamp(Age / (RiseSeconds * 4.0f), 0.0f, 1.0f);
	const float CapRadius = Girth * (1.4f + 2.6f * Rise + 2.2f * Spread);
	const float CapThickness = Girth * (0.9f + 0.7f * Rise);
	Cap->SetRelativeLocation(FVector(0.0f, 0.0f, Height));
	Cap->SetRelativeScale3D(FVector(CapRadius / ShapeHalfExtent, CapRadius / ShapeHalfExtent, CapThickness / ShapeHalfExtent));
	if (CapMID) CapMID->SetVectorParameterValue(TEXT("Tint"), Tint(0.92f * Fade));

	if (CrownMID)
	{
		// The crown is a cone stood on its point: thrown up in a second and a
		// half, it hangs, then collapses back into the sea.
		const float Up = 1.0f - FMath::Square(1.0f - FMath::Clamp(Age / 1.5f, 0.0f, 1.0f));
		const float Collapse = FMath::SmoothStep(5.0f, 11.0f, Age);
		const float CrownHeight = Radius * (0.016f + 0.022f * Params.Strength) * Up * (1.0f - 0.75f * Collapse);
		const float CrownRadius = FMath::Clamp(Params.BlastRadius * 0.34f, 1.2f, 7.0f) * (0.45f + 0.55f * Up + 0.35f * Collapse);
		Crown->SetRelativeLocation(FVector::ZeroVector);
		Crown->SetRelativeScale3D(FVector(CrownRadius, CrownRadius, FMath::Max(CrownHeight, 0.02f)));
		// Additive spray: Tint.a is the HDR level.
		CrownMID->SetVectorParameterValue(TEXT("Tint"),
			FLinearColor(SprayColor.R, SprayColor.G, SprayColor.B, 1.5f * Day * (1.0f - Collapse)));
		Crown->SetVisibility(Collapse < 0.999f);
	}
}

void AEarthfallPlume::BuildCrownMesh()
{
	// Two nested curtains in unit space: radius 1 at the sea, flaring outward, one
	// unit tall, with a ragged top so it reads as thrown water, not a cup.
	constexpr int32 Segments = 72;
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	FRandomStream Random(GetUniqueID());
	for (int32 Curtain = 0; Curtain < 2; ++Curtain)
	{
		const float BaseRadius = Curtain == 0 ? 1.0f : 0.55f;
		const float Flare = Curtain == 0 ? 1.55f : 1.25f;
		const float Tall = Curtain == 0 ? 1.0f : 0.7f;
		const int32 First = Vertices.Num();
		for (int32 Segment = 0; Segment <= Segments; ++Segment)
		{
			// The last column repeats the first so the seam closes.
			const int32 Wrapped = Segment % Segments;
			FRandomStream Column(Random.GetInitialSeed() + Curtain * 1000 + Wrapped);
			const float Angle = 2.0f * PI * Wrapped / Segments;
			const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
			// Broad lobes with a little per-column raggedness: neighbouring columns stay
			// close in height, so the sheet between them never folds into a flat flap.
			const float Lobes = 0.5f + 0.5f * FMath::Sin(Angle * 7.0f + Curtain * 1.9f) * FMath::Cos(Angle * 3.0f + 0.7f);
			const float Height = Tall * (0.62f + 0.30f * Lobes + Column.FRandRange(0.0f, 0.08f));
			const float Fade = Column.FRandRange(0.55f, 1.0f);
			Vertices.Add(Outward * BaseRadius);
			Vertices.Add(Outward * BaseRadius * FMath::Lerp(1.0f, Flare, Height) + FVector(0.0f, 0.0f, Height));
			for (int32 End = 0; End < 2; ++End)
			{
				Normals.Add(Outward);
				UVs.Add(FVector2D(static_cast<float>(Segment) / Segments, End == 0 ? 0.0f : 1.0f));
				Colors.Add(FLinearColor(1.0f, 1.0f, 1.0f, Fade));
				Tangents.Add(FProcMeshTangent(-Outward.Y, Outward.X, 0.0f));
			}
		}
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 A = First + Segment * 2;
			Triangles.Append({A, A + 1, A + 2, A + 1, A + 3, A + 2});
		}
	}
	Crown->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false);
}
