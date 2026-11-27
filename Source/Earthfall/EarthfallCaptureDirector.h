#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EarthfallCaptureDirector.generated.h"

class AEarthOrbitPawn;

/**
 * Scripted runtime capture, spawned only when the game is launched with
 * -EarthfallCapture. It waits for shaders and textures to finish compiling, flies
 * the orbit camera through a fixed shot list, fires real strikes through the same
 * launch path the player uses, writes numbered screenshots to
 * Saved/Screenshots/Capture, logs the measured frame rate and quits.
 *
 *   UnrealEditor.exe Earthfall.uproject -game -windowed -ResX=2560 -ResY=1440
 *       -EarthfallCapture -EarthfallSunLon=-60 -EarthfallSunLat=10
 */
UCLASS()
class EARTHFALL_API AEarthfallCaptureDirector : public AActor
{
	GENERATED_BODY()

public:
	AEarthfallCaptureDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FStep
	{
		float Time = 0.0f;
		TFunction<void()> Action;
	};

	TArray<FStep> Steps;
	int32 NextStep = 0;
	FString OutputDirectory;
	float WarmupSeconds = 0.0f;
	float QuietSeconds = 0.0f;
	float ScriptSeconds = 0.0f;
	/** Added to every step scheduled after it is set. */
	float TimeOffset = 0.0f;
	float NextProgressLog = 0.0f;
	double FrameSecondsTotal = 0.0;
	double WorstFrameSeconds = 0.0;
	int32 FrameCount = 0;
	int32 ShotCount = 0;
	bool bReady = false;
	bool bFinished = false;

	AEarthOrbitPawn* Pawn() const;
	void At(float Time, TFunction<void()> Action);
	void Shot(const TCHAR* Name);
	void BuildScript();
	void Finish();
};
