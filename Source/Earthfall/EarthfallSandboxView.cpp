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
	FLinearColor(0.75f, 0.88f, 1.00f, 3.0f),  // strike groups: cold white
	FLinearColor(0.45f, 0.95f, 1.00f, 9.0f),  // interceptors: bright cyan
	FLinearColor(0.95f, 0.72f, 0.48f, 3.4f),  // debris: sunlit metal
	FLinearColor(0.55f, 0.85f, 1.00f, 5.0f),  // satellites
	FLinearColor(0.80f, 0.92f, 1.00f, 3.6f),  // air traffic: cool white, to stand apart from city lights
	FLinearColor(0.55f, 1.00f, 0.80f, 2.8f),  // shipping: sea green
	FLinearColor(1.00f, 0.22f, 0.12f, 4.0f),  // combat and unrest: red
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
	GlowMaterial = Material.Succeeded() ? Material.Object : nullptr;

	for (int32 Kind = 0; Kind < MarkerKindCount; ++Kind)
	{
		UInstancedStaticMeshComponent* Batch = CreateDefaultSubobject<UInstancedStaticMeshComponent>(*FString::Printf(TEXT("Markers%d"), Kind));
		Batch->SetupAttachment(Scene);
		Batch->SetMobility(EComponentMobility::Movable);
		if (Sphere.Succeeded()) Batch->SetStaticMesh(Sphere.Object);
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
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		MID->SetVectorParameterValue(TEXT("Tint"), MarkerTints[Kind]);
		// Shape = (falloff exponent, hot-core boost, noise scale, turbulence), as in fx.hlsl.
		MID->SetVectorParameterValue(TEXT("Shape"), FLinearColor(2.4f, 1.0f, 2.0f, 0.0f));
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
