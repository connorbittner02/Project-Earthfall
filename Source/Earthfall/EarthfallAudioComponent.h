#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include <atomic>
#include "EarthfallAudioComponent.generated.h"

/**
 * Every sound in Earthfall, synthesised sample by sample with no audio files.
 *
 *   Ambience  a deep orbital drone that gives way to stratospheric wind as the
 *             camera descends, with firestorm crackle once the world is burning.
 *   Launch    a rising filtered-noise whoosh.
 *   Impact    a sub-bass boom, delayed by the caller for the distance to the flash.
 *   Alert     radio static and a two-tone emergency signal for war and nuclear news.
 *
 * The game thread only writes the atomics below; the audio render thread reads
 * them in OnGenerateAudio and owns all oscillator and filter state.
 *
 * The project enables UE's MetaSound plugin and links its modules, but these
 * sounds are a hand-written synth, not a MetaSound graph: MetaSound sources are
 * assets, and this project builds every asset from scripts with no editor GUI.
 */
UCLASS(ClassGroup = Earthfall)
class EARTHFALL_API UEarthfallAudioComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UEarthfallAudioComponent(const FObjectInitializer& ObjectInitializer);

	/** 0 = high orbit, 1 = down among the terrain. */
	void SetDescent(float Value) { Descent.store(FMath::Clamp(Value, 0.0f, 1.0f)); }
	/** 0 = calm world, 1 = continents on fire. */
	void SetFirestorm(float Value) { Firestorm.store(FMath::Clamp(Value, 0.0f, 1.0f)); }
	void TriggerLaunch() { LaunchRequests.fetch_add(1); }
	/** Strength 0..1; the boom starts DelaySeconds from now. */
	void TriggerBoom(float Strength, float DelaySeconds);
	void TriggerAlert() { AlertRequests.fetch_add(1); }
	/** 1 = normal; 0 mutes drone, wind and crackle so a single voice can be measured. */
	void SetAmbienceLevel(float Value) { AmbienceLevel.store(FMath::Clamp(Value, 0.0f, 1.0f)); }

	/** No sample ever leaves the synth louder than this. */
	static constexpr float OutputCeiling = 0.95f;
	/**
	 * Render every voice offline at 48 kHz, with no audio device, and log peak,
	 * RMS and edge levels. Returns true if nothing exceeds OutputCeiling, every
	 * voice starts and ends at silence, and the boom sits 6-24 dB over the alert.
	 */
	static bool RunSelfTest();

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	static constexpr int32 MaxBooms = 6;

	std::atomic<float> Descent{0.0f};
	std::atomic<float> Firestorm{0.0f};
	std::atomic<float> AmbienceLevel{1.0f};
	std::atomic<int32> LaunchRequests{0};
	std::atomic<int32> AlertRequests{0};
	/** Pending booms: strength, and samples until each starts. A negative delay is an empty slot. */
	std::atomic<float> BoomStrength[MaxBooms];
	std::atomic<int32> BoomDelaySamples[MaxBooms];

	// --- Audio render thread state.
	float Rate = 48000.0f;
	uint32 NoiseSeed = 0x1234567u;
	double DronePhaseA = 0.0;
	double DronePhaseB = 0.0;
	double DronePhaseC = 0.0;
	float DroneNoise = 0.0f;
	float WindLow = 0.0f;
	float WindBand = 0.0f;
	float WindGust = 0.0f;
	float CrackleEnvelope = 0.0f;
	float SmoothDescent = 0.0f;
	float SmoothFirestorm = 0.0f;
	float MasterGain = 0.0f;
	int32 LaunchesSeen = 0;
	int32 AlertsSeen = 0;
	float LaunchAge = -1.0f;
	float LaunchLow = 0.0f;
	float LaunchBand = 0.0f;
	float AlertAge = -1.0f;
	double AlertPhase = 0.0;
	float AlertNoise = 0.0f;
	float BoomAge[MaxBooms];
	float BoomLevel[MaxBooms];
	double BoomPhase[MaxBooms];
	float BoomNoise = 0.0f;

	float Noise();
};
