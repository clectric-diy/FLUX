
#include "daisysp.h"
#include "daisy_spark.h"

using namespace daisy;
using namespace daisysp;
using namespace daisy_spark;

// === Begin Variables & Classes===

static Spark spark;

static Oscillator osc;
static Parameter pitchParam;

// --- Auto-Save Variables ---
uint32_t lastInteractionTime = 0;
bool needsSave = false;
const uint32_t SAVE_DELAY_MS = 3000; // Wait 3 seconds after last interaction

enum sparkModes {
	MODE_WAVEFORMS = 0,
	MODE_MACRO_A = 1,
	MODE_MACRO_B = 2,
	MODE_COUNT = 3
};

enum waveforms {
	WAVE_SIN = 0,
	WAVE_POLYBLEP_TRI = 1,
	WAVE_POLYBLEP_SAW = 2,
	WAVE_RAMP = 3,
	// The Hack: How to get a PolyBLEP Ramp
	//
	// Inside your AudioCallback:
    // Generate the Saw, and instantly invert it into a Ramp!
 	// float my_polyblep_ramp = osc.Process() * -1.0f;
	//
	WAVE_POLYBLEP_SQUARE = 4,
	WAVE_COUNT = 5	
};

// Macro A models (in Plaits order)
enum firstBankMacros {
	MACRO_A_VARIABLE_SAW = 0,
	MACRO_A_VOSIM = 1,
	MACRO_A_STRING = 2,
	MACRO_A_COUNT = 3
};

// Macro B models (in Plaits order, fractal noise commented out due to incompatibility)
enum secondBankMacros {
	MACRO_B_PARTICLE = 0,
	// MACRO_B_FRACTAL_NOISE,
	MACRO_B_BASS_DRUM_CLICK = 1,
	MACRO_B_RING_MOD_NOISE = 2,
	MACRO_B_OVERDRIVE = 3,
	MACRO_B_COUNT = 4
};

struct SparkSettings {
	int   sparkMode;
	int   waveform;
	float waveformFreq;
	int   macroA, macroB;

    // Include a default operators so the Daisy knows 
    // what to do the first time it boots up.
    bool operator!=(const SparkSettings& a) const {
        return a.sparkMode != sparkMode ||
               a.waveform != waveform ||
			   a.waveformFreq != waveformFreq ||
               a.macroA != macroA ||
               a.macroB != macroB;
    }
};

PersistentStorage<SparkSettings> storage(spark.seed.qspi);

// === End Variables & Classes ===

// === Begin Functions & Methods ===

void ProcessEncoder() {
    int32_t inc = spark.encoder.Increment();

    if (inc != 0) {
        SparkSettings& current = storage.GetSettings();

        if (inc > 0) {
            current.waveform++;
            if (current.waveform >= WAVE_COUNT) {
				current.waveform = 0;
			}
        } 
        else if (inc < 0) {
            current.waveform--;
            if (current.waveform < 0) {
				current.waveform = WAVE_COUNT - 1;
			}
        }

        // Flag that we need to save!
        needsSave = true;
        lastInteractionTime = System::GetNow();
    }
}

// === The Control Function ===
void ProcessControls() {
    // 1. Tell the hardware to read the physical pins
    spark.ProcessAnalogControls();
    spark.ProcessDigitalControls();

    // 2. Process the Encoder
	ProcessEncoder();

    // 3. (Future) Process your Buttons here
	
    // 4. (Future) Process your CV Inputs here
}

// === The Audio Callback ===
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // ... [Your Audio Callback stays exactly the same] ...
}

// === End Functions & Methods ===

int main(void) {

    spark.Init();

    // Define the Initial Defaults
    SparkSettings defaultSettings;
	defaultSettings.sparkMode = 0;
	defaultSettings.waveform = 0;
	defaultSettings.waveformFreq = 261.63f; // Defaults to Middle C
    defaultSettings.macroA = 0;
    defaultSettings.macroB = 0;

	// Initialize the storage with those defaults
    storage.Init(defaultSettings);

    // Load the settings from Flash Memory into live RAM
    // (If the flash is empty, it automatically loads your defaults)
    SparkSettings& saved = storage.GetSettings();

	spark.SetAudioBlockSize(4);
    float sample_rate = spark.AudioSampleRate();
    osc.Init(sample_rate);

	osc.SetFreq(saved.waveformFreq);
    osc.SetAmp(1);

    // Now you can use saved.waveform in your audio code!
	spark.StartAudio(AudioCallback);

    while(1) {
		
        ProcessControls();

		if (needsSave) {
            // Check if 3000ms have passed since the user's last interaction
            if (System::GetNow() - lastInteractionTime > SAVE_DELAY_MS) {
                storage.Save();        // Burn to flash memory
                needsSave = false;   // Reset the flag so we don't save again
            }
        }
    }
}