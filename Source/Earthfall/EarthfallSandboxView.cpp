#include "EarthfallSandboxView.h"
#include "EarthfallSandboxSubsystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
/** Over the clouds and plumes, under the fireballs. */
constexpr int32 MarkerSortPriority = 18;
constexpr float MarkerSphereRadius = 50.0f;

/** Colour and HDR level per marker kind, indexed by EarthfallMarkers. */
const FLinearColor MarkerTints[] = {
	FLinearColor(0.10f, 0.55f, 1.00f, 2.2f),  // naval fleets: cyan-blue hulls (kept under the level that blooms to white)
	FLinearColor(0.45f, 0.95f, 1.00f, 9.0f),  // interceptors: bright cyan
	FLinearColor(0.95f, 0.72f, 0.48f, 3.4f),  // debris: sunlit metal
	FLinearColor(0.55f, 0.85f, 1.00f, 5.0f),  // satellites
	FLinearColor(0.80f, 0.92f, 1.00f, 3.6f),  // air traffic: cool white, to stand apart from city lights
	FLinearColor(0.55f, 1.00f, 0.80f, 2.8f),  // shipping: sea green
	FLinearColor(1.00f, 0.34f, 0.06f, 2.4f),  // combat and unrest: ember orange
	FLinearColor(1.00f, 0.90f, 0.50f, 2.6f),  // air strike wings: white-gold deltas
	FLinearColor(0.30f, 0.90f, 1.00f, 0.42f), // fusion shield: a faint cyan dome
	FLinearColor(1.00f, 0.42f, 0.03f, 2.0f),  // ground armies: amber wedges
	FLinearColor(0.050f, 0.045f, 0.040f, 0.80f), // smoke over fighting (translucent volume, not a glow)
};
constexpr int32 MarkerKindCount = UE_ARRAY_COUNT(MarkerTints);
}

AEarthfallSandboxView::AEarthfallSandboxView()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the subsystem has moved everything for this frame.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	USceneComponent* Scene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Scene);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Earthfall/Materials/M_FXEjecta.M_FXEjecta"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Smoke(TEXT("/Game/Earthfall/Materials/M_FXVolumeInstanced.M_FXVolumeInstanced"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	SmokeMaterial = Smoke.Succeeded() ? Smoke.Object : nullptr;
	GlowMaterial = Material.Succeeded() ? Material.Object : nullptr;

	for (int32 Kind = 0; Kind < MarkerKindCount; ++Kind)
	{
		UInstancedStaticMeshComponent* Batch = CreateDefaultSubobject<UInstancedStaticMeshComponent>(*FString::Printf(TEXT("Markers%d"), Kind));
		Batch->SetupAttachment(Scene);
		Batch->SetMobility(EComponentMobility::Movable);
		// Armies and air wings are cones, tip forward; everything else is a sphere.
		const bool bCone = Kind == EarthfallMarkers::Army || Kind == EarthfallMarkers::AirWing;
		if (bCone && Cone.Succeeded()) Batch->SetStaticMesh(Cone.Object);
		else if (Sphere.Succeeded()) Batch->SetStaticMesh(Sphere.Object);
		Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Batch->SetGenerateOverlapEvents(false);
		Batch->SetCastShadow(false);
		Batch->SetReceivesDecals(false);
		Batch->SetTranslucentSortPriority(MarkerSortPriority);
		// Markers span the whole sky; never cull a batch as one object.
		Batch->SetBoundsScale(1000.0f);
		Batches.Add(Batch);
	}
}

void AEarthfallSandboxView::BeginPlay()
{
	Super::BeginPlay();
	Transforms.SetNum(MarkerKindCount);
	InstanceCounts.Init(0, MarkerKindCount);
	if (!GlowMaterial) return;
	for (int32 Kind = 0; Kind < MarkerKindCount; ++Kind)
	{
		const bool bSmoke = Kind == EarthfallMarkers::Smoke && SmokeMaterial;
		const bool bUnit = Kind == EarthfallMarkers::Unit || Kind == EarthfallMarkers::Army || Kind == EarthfallMarkers::AirWing;
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(bSmoke ? SmokeMaterial.Get() : GlowMaterial.Get(), this);
		MID->SetVectorParameterValue(TEXT("Tint"), MarkerTints[Kind]);
		// Glow Shape = (falloff exponent, hot-core boost, noise scale, turbulence), as in fx.hlsl.
		// Units use a low exponent so the whole shape is lit and its outline reads; lights stay soft.
		// Smoke Shape = (edge softness, noise contrast, noise scale, scroll), as in fx_volume.hlsl.
		MID->SetVectorParameterValue(TEXT("Shape"), bSmoke ? FLinearColor(1.6f, 1.4f, 4.0f, 0.5f)
			: (bUnit ? FLinearColor(0.45f, 0.0f, 2.0f, 0.0f) : FLinearColor(2.4f, 1.0f, 2.0f, 0.0f)));
		Batches[Kind]->SetMaterial(0, MID);
		BatchMIDs.Add(MID);
	}
}

void AEarthfallSandboxView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UEarthfallSandboxSubsystem* Sandbox = GetWorld() ? GetWorld()->GetSubsystem<UEarthfallSandboxSubsystem>() : nullptr;
	if (!Sandbox || BatchMIDs.Num() != MarkerKindCount) return;

	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector Eye = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector(400.0f, 0.0f, 0.0f);

	for (TArray<FTransform>& List : Transforms) List.Reset();
	for (const FEarthfallMarker& Marker : Sandbox->GetMarkers())
	{
		if (Marker.Kind >= MarkerKindCount) continue;
		// Markers keep a readable size on screen from any distance, like the ejecta.
		const float Apparent = FMath::Clamp(static_cast<float>(FVector::Dist(Marker.Position, Eye)) / 140.0f, 0.35f, 4.0f);
		const float Size = Marker.Size * Apparent;
		if (Marker.Kind == EarthfallMarkers::Army || Marker.Kind == EarthfallMarkers::AirWing)
		{
			// The cone's tip (+Z) points along the heading and its Y axis stands up from
			// the ground, so X is the span. An army is a squat wedge; an air wing is a
			// broad, paper-thin delta.
			const bool bWing = Marker.Kind == EarthfallMarkers::AirWing;
			const FVector Radial = Marker.Position.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
			Transforms[Marker.Kind].Emplace(FRotationMatrix::MakeFromZY(Marker.Forward, Radial).ToQuat(), Marker.Position,
				FVector(Size * (bWing ? 2.3f : 1.25f), Size * (bWing ? 0.18f : 0.55f), Size * Marker.Stretch) / MarkerSphereRadius);
			continue;
		}
		Transforms[Marker.Kind].Emplace(FRotationMatrix::MakeFromX(Marker.Forward).ToQuat(), Marker.Position,
			FVector(Size * Marker.Stretch, Size, Size) / MarkerSphereRadius);
	}

	for (int32 Kind = 0; Kind < MarkerKindCount; ++Kind)
	{
		UInstancedStaticMeshComponent* Batch = Batches[Kind];
		const TArray<FTransform>& List = Transforms[Kind];
		if (List.Num() != InstanceCounts[Kind])
		{
			// The count changed: rebuild. This is a few hundred instances at most.
			Batch->ClearInstances();
			if (List.Num() > 0) Batch->AddInstances(List, false, true);
			InstanceCounts[Kind] = List.Num();
		}
		else if (List.Num() > 0)
		{
			Batch->BatchUpdateInstancesTransforms(0, List, true, true, false);
		}
	}
}
