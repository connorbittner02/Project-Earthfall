#include "EarthfallPlanet.h"
#include "EarthImpactFeedback.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AEarthfallPlanet::AEarthfallPlanet()
{
    PrimaryActorTick.bCanEverTick = true;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
    PlanetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Planet"));
    PlanetMesh->SetupAttachment(Root);
    PlanetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    PlanetMesh->SetCollisionObjectType(ECC_WorldStatic);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMesh.Succeeded()) PlanetMesh->SetStaticMesh(SphereMesh.Object);
    PlanetMesh->SetRelativeScale3D(FVector(PlanetRadius / 50.0f));
}

void AEarthfallPlanet::BeginPlay()
{
    Super::BeginPlay();
}

void AEarthfallPlanet::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    AddActorLocalRotation(FRotator(0.0f, RotationDegreesPerSecond * DeltaSeconds, 0.0f));
}

void AEarthfallPlanet::ApplyImpact(FVector WorldLocation, float Radius, float Energy)
{
    const FVector LocalDirection = GetActorTransform().InverseTransformVectorNoScale(WorldLocation - GetActorLocation()).GetSafeNormal();
    const FVector SurfacePoint = GetActorLocation() + GetActorTransform().TransformVectorNoScale(LocalDirection * PlanetRadius);
    const float SafeRadius = FMath::Max(1.0f, Radius);
    const float Strength = FMath::Clamp(Energy / 100.0f, 0.05f, 2.0f);

    // Prototype feedback only. Runtime spherical SDF carving is a later milestone.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<AEarthImpactFeedback>(
        AEarthImpactFeedback::StaticClass(), SurfacePoint, FRotationMatrix::MakeFromZ(LocalDirection).Rotator(), Params);
}
