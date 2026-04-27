#include "daisysp.h"
#include "daisy_spark.h"
#include <cstdarg>
#include <cstdio>

using namespace daisy;
using namespace daisysp;
using namespace daisy_spark;

static Spark spark;
static SparkDiagnostics diagnostics(spark);
static SparkRuntime runtime(spark, diagnostics);

static Oscillator            osc;
static VariableSawOscillator variableSaw;
static VosimOscillator       vosim;
static StringVoice           stringVoice;
static Particle              particle;
static SyntheticBassDrum     synthBassDrum;
static GrainletOscillator    grainlet;
static Overdrive             overdrive;
static Parameter             freqParam;
static Parameter             shapeParam;

static const auto SAVE_DELAY_MS       = 3000U;
static float      sampleRate          = 48000.0f;
static float      drumPhase           = 0.0f;
static bool       encoderTurnedWhilePressed = false;
static bool       i2cScanPrinted            = false;
static Spark::LedCalibration kOnboardLedCalibration = {
    0.45f, // global_gain
    1.00f, // red_gain
    1.00f, // green_gain
    1.00f, // blue_gain
    1.80f  // gamma
};

enum SparkModes {
    MODE_WAVEFORMS = 0,
    MODE_MACRO_A   = 1,
    MODE_MACRO_B   = 2,
    MODE_COUNT     = 3
};

enum Waveforms {
    WAVE_SIN             = 0,
    WAVE_POLYBLEP_TRI    = 1,
    WAVE_POLYBLEP_SAW    = 2,
    WAVE_RAMP            = 3,
    WAVE_POLYBLEP_SQUARE = 4,
    WAVE_COUNT           = 5
};

enum MacrosBankA {
    MACRO_A_VARIABLE_SAW = 0,
    MACRO_A_VOSIM        = 1,
    MACRO_A_STRING       = 2,
    MACRO_A_COUNT        = 3
};

enum MacrosBankB {
    MACRO_B_PARTICLE        = 0,
    MACRO_B_BASS_DRUM_CLICK = 1,
    // Placeholder name kept for UI/backward compatibility.
    // Implemented with GrainletOscillator until a dedicated ring-mod noise block is added.
    MACRO_B_RING_MOD_NOISE = 2,
    MACRO_B_OVERDRIVE      = 3,
    MACRO_B_COUNT          = 4
};

struct SparkSettings {
    int   sparkMode;
    int   waveform;
    float waveformFreq;
    int   macroA;
    int   macroB;

    bool operator!=(const SparkSettings& a) const {
        return a.sparkMode != sparkMode ||
               a.waveform != waveform ||
               a.waveformFreq != waveformFreq ||
               a.macroA != macroA ||
               a.macroB != macroB;
    }
} storageState;

PersistentStorage<SparkSettings> storage(spark.seed.qspi);

#ifndef SPARK_DEBUG_ENABLE
#define SPARK_DEBUG_ENABLE 1
#endif

#ifndef SPARK_DEBUG_DEFAULT_LEVEL
#define SPARK_DEBUG_DEFAULT_LEVEL 2
#endif

#ifndef SPARK_DEBUG_DEFAULT_MASK
#define SPARK_DEBUG_DEFAULT_MASK 0x0F
#endif

#ifndef SPARK_DEBUG_STATUS_INTERVAL_MS
#define SPARK_DEBUG_STATUS_INTERVAL_MS 1000U
#endif

static const char* ModeName(int mode)
{
    switch(mode)
    {
        case MODE_WAVEFORMS: return "wave";
        case MODE_MACRO_A: return "macroA";
        case MODE_MACRO_B: return "macroB";
        default: return "unknown";
    }
}

struct RgbColor
{
    float r;
    float g;
    float b;
};

// ROYGBIV palette with red intentionally moved to the end:
// orange, yellow, green, blue, indigo, violet, red
static constexpr RgbColor kRoygbivRedLast[] = {
    {1.00f, 0.35f, 0.00f}, // orange
    {0.90f, 0.80f, 0.00f}, // yellow
    {0.00f, 0.90f, 0.00f}, // green
    {0.00f, 0.25f, 1.00f}, // blue
    {0.20f, 0.00f, 0.70f}, // indigo
    {0.55f, 0.00f, 0.75f}, // violet
    {0.90f, 0.00f, 0.00f}, // red (last)
};

static RgbColor PaletteColor(int index)
{
    const int paletteCount = static_cast<int>(sizeof(kRoygbivRedLast) / sizeof(kRoygbivRedLast[0]));
    return kRoygbivRedLast[WrapIndex(index, paletteCount)];
}

static void DebugLog(uint8_t level, uint8_t category, const char* format, ...)
{
#if SPARK_DEBUG_ENABLE
    va_list args;
    va_start(args, format);
    char line[192];
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    diagnostics.Log(level, category, "%s", line);
#else
    (void)level;
    (void)category;
    (void)format;
#endif
}

static void DebugInit()
{
#if SPARK_DEBUG_ENABLE
    diagnostics.Init(SPARK_DEBUG_DEFAULT_LEVEL, SPARK_DEBUG_DEFAULT_MASK, "oscillators");
    DebugLog(DBG_INFO,
             DBG_CAT_STATE,
             "spark debug on: level=%d mask=0x%02x",
             diagnostics.Level(),
             diagnostics.Mask());

    // I2C scan is printed later (after USB CDC attach) from the main loop.
#endif
}

static void DebugMaybePrintI2cScan()
{
#if SPARK_DEBUG_ENABLE
    if(i2cScanPrinted || System::GetNow() < 2000U)
    {
        return;
    }

    i2cScanPrinted = true;
    spark.seed.PrintLine("");
    spark.seed.PrintLine("---- delayed i2c scan ----");

    if(!spark.InitPeripheralI2c())
    {
        spark.seed.PrintLine("i2c scan: peripheral I2C init failed");
        spark.seed.PrintLine("--------------------------");
        return;
    }

    uint8_t found[16];
    const uint8_t count = spark.ScanI2cDevices(found, 16);
    if(count == 0)
    {
        spark.seed.PrintLine("i2c scan: no devices found on peripheral bus");
    }
    else
    {
        spark.seed.PrintLine("i2c scan: %d device(s) found", count);
        for(uint8_t i = 0; i < count; i++)
        {
            spark.seed.PrintLine("i2c addr: 0x%02x", found[i]);
        }
    }
    spark.seed.PrintLine("--------------------------");
#endif
}

static void DebugMaybeStatus()
{
    if(!diagnostics.StatusDue(SPARK_DEBUG_STATUS_INTERVAL_MS))
    {
        return;
    }

    SparkSettings& current = storage.GetSettings();
    diagnostics.RefreshStatusLine(ModeName(current.sparkMode),
                                  current.waveform,
                                  current.macroA,
                                  current.macroB,
                                  current.waveformFreq,
                                  spark.knob1.Value(),
                                  spark.knob2.Value(),
                                  spark.encoder.Pressed(),
                                  spark.button1.Pressed(),
                                  spark.button2.Pressed());
}

static void MarkInteraction()
{
    runtime.MarkInteraction();
    DebugLog(DBG_TRACE, DBG_CAT_CTRL, "interaction marked");
}

static void UpdateLeds()
{
    SparkSettings& current = storage.GetSettings();
    const int modeColorIndex = (current.sparkMode == MODE_WAVEFORMS)
                                   ? 0
                               : (current.sparkMode == MODE_MACRO_A) ? 2
                                                                       : 4;
    const int itemColorIndex = (current.sparkMode == MODE_WAVEFORMS)
                                   ? current.waveform
                               : (current.sparkMode == MODE_MACRO_A) ? current.macroA
                                                                       : current.macroB;

    const RgbColor modeColor = PaletteColor(modeColorIndex);
    const RgbColor itemColor = PaletteColor(itemColorIndex);
    float led1r, led1g, led1b;
    spark.ApplyLedCalibration(Spark::LedTarget::Onboard,
                              modeColor.r * 0.28f,
                              modeColor.g * 0.28f,
                              modeColor.b * 0.28f,
                              led1r,
                              led1g,
                              led1b);

    float led2r, led2g, led2b;
    spark.ApplyLedCalibration(Spark::LedTarget::Onboard,
                              itemColor.r * 0.45f,
                              itemColor.g * 0.45f,
                              itemColor.b * 0.45f,
                              led2r,
                              led2g,
                              led2b);

    spark.led1.Set(led1r, led1g, led1b);
    spark.led2.Set(led2r, led2g, led2b);
    spark.UpdateLeds();
}

void ProcessEncoder()
{
    SparkSettings& current = storage.GetSettings();
    const int32_t  inc     = spark.encoder.Increment();

    if(spark.encoder.RisingEdge())
    {
        encoderTurnedWhilePressed = false;
    }

    if(inc != 0)
    {
        if(spark.encoder.Pressed())
        {
            current.sparkMode = WrapIndex(current.sparkMode + static_cast<int>(inc), MODE_COUNT);
            encoderTurnedWhilePressed = true;
            DebugLog(DBG_INFO, DBG_CAT_CTRL, "bank -> %s", ModeName(current.sparkMode));
        }
        else
        {
            if(current.sparkMode == MODE_WAVEFORMS)
            {
                current.waveform = WrapIndex(current.waveform + static_cast<int>(inc), WAVE_COUNT);
                DebugLog(DBG_INFO, DBG_CAT_CTRL, "waveform -> %d", current.waveform);
            }
            else if(current.sparkMode == MODE_MACRO_A)
            {
                current.macroA = WrapIndex(current.macroA + static_cast<int>(inc), MACRO_A_COUNT);
                DebugLog(DBG_INFO, DBG_CAT_CTRL, "macroA -> %d", current.macroA);
            }
            else
            {
                current.macroB = WrapIndex(current.macroB + static_cast<int>(inc), MACRO_B_COUNT);
                DebugLog(DBG_INFO, DBG_CAT_CTRL, "macroB -> %d", current.macroB);
            }
        }
        MarkInteraction();
    }

    if(spark.encoder.FallingEdge() && !encoderTurnedWhilePressed)
    {
        if(current.sparkMode == MODE_MACRO_A && current.macroA == MACRO_A_STRING)
        {
            stringVoice.Trig();
            DebugLog(DBG_INFO, DBG_CAT_CTRL, "string trig");
        }
        else if(current.sparkMode == MODE_MACRO_B
                && current.macroB == MACRO_B_BASS_DRUM_CLICK)
        {
            synthBassDrum.Trig();
            DebugLog(DBG_INFO, DBG_CAT_CTRL, "drum trig");
        }
    }
}

void ProcessKnobs()
{
    SparkSettings& current = storage.GetSettings();
    const float    newFreq = freqParam.Process();
    if(fabsf(newFreq - current.waveformFreq) > 0.2f)
    {
        current.waveformFreq = newFreq;
        MarkInteraction();
        DebugLog(DBG_TRACE, DBG_CAT_CTRL, "freq -> %.2f", current.waveformFreq);
    }

    (void)shapeParam.Process();
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    SparkSettings& current = storage.GetSettings();
    const float    shape   = shapeParam.Value();

    osc.SetFreq(current.waveformFreq);

    for(size_t i = 0; i < size; i++)
    {
        float sig = 0.0f;

        if(current.sparkMode == MODE_WAVEFORMS)
        {
            osc.SetWaveform(static_cast<uint8_t>(current.waveform));
            sig = (current.waveform == WAVE_RAMP) ? (-1.0f * osc.Process()) : osc.Process();
        }
        else if(current.sparkMode == MODE_MACRO_A)
        {
            switch(current.macroA)
            {
                case MACRO_A_VARIABLE_SAW:
                    variableSaw.SetFreq(current.waveformFreq);
                    variableSaw.SetPW((shape * 2.0f) - 1.0f);
                    variableSaw.SetWaveshape(shape);
                    sig = variableSaw.Process();
                    break;

                case MACRO_A_VOSIM:
                    vosim.SetFreq(current.waveformFreq);
                    vosim.SetForm1Freq(current.waveformFreq * (1.0f + (shape * 2.0f)));
                    vosim.SetForm2Freq(current.waveformFreq * (2.0f + (shape * 3.0f)));
                    vosim.SetShape((shape * 2.0f) - 1.0f);
                    sig = vosim.Process();
                    break;

                case MACRO_A_STRING:
                default:
                    stringVoice.SetSustain(true);
                    stringVoice.SetFreq(current.waveformFreq);
                    stringVoice.SetAccent(0.7f);
                    stringVoice.SetStructure(shape);
                    stringVoice.SetBrightness(shape);
                    stringVoice.SetDamping(1.0f - (shape * 0.8f));
                    sig = stringVoice.Process(false);
                    break;
            }
        }
        else
        {
            switch(current.macroB)
            {
                case MACRO_B_PARTICLE:
                    particle.SetFreq(current.waveformFreq);
                    particle.SetResonance(0.2f + (shape * 0.75f));
                    particle.SetDensity(shape);
                    particle.SetRandomFreq(0.4f + (shape * 12.0f));
                    particle.SetSpread(4.0f * shape);
                    particle.SetGain(0.75f);
                    sig = particle.Process();
                    break;

                case MACRO_B_BASS_DRUM_CLICK:
                {
                    drumPhase += current.waveformFreq / sampleRate;
                    bool trig = false;
                    if(drumPhase >= 1.0f)
                    {
                        drumPhase -= 1.0f;
                        trig = true;
                    }
                    synthBassDrum.SetFreq(fmaxf(20.0f, current.waveformFreq));
                    synthBassDrum.SetTone(shape);
                    synthBassDrum.SetDecay(0.2f + (shape * 0.8f));
                    synthBassDrum.SetDirtiness(shape);
                    synthBassDrum.SetFmEnvelopeAmount(shape);
                    synthBassDrum.SetFmEnvelopeDecay(shape);
                    sig = synthBassDrum.Process(trig);
                    break;
                }

                case MACRO_B_RING_MOD_NOISE:
                    grainlet.SetFreq(current.waveformFreq);
                    grainlet.SetFormantFreq(current.waveformFreq * (2.0f + (shape * 6.0f)));
                    grainlet.SetShape(0.2f + (shape * 2.0f));
                    grainlet.SetBleed(shape);
                    sig = grainlet.Process();
                    break;

                case MACRO_B_OVERDRIVE:
                default:
                    osc.SetWaveform(Oscillator::WAVE_SAW);
                    overdrive.SetDrive(shape);
                    sig = overdrive.Process(osc.Process());
                    break;
            }
        }

        out[0][i] = sig * 0.5f;
        out[1][i] = sig * 0.5f;
    }
}

int main(void) {

    spark.Init();

    // Give I2C LED Driver and other components a chance to wake up.
    System::Delay(500);

    SparkSettings defaultSettings;
    defaultSettings.sparkMode    = MODE_WAVEFORMS;
    defaultSettings.waveform     = WAVE_SIN;
    defaultSettings.waveformFreq = 261.63f;
    defaultSettings.macroA = 0;
    defaultSettings.macroB = 0;

    storage.Init(defaultSettings);
    spark.SetAudioBlockSize(4);
    sampleRate = spark.AudioSampleRate();

    osc.Init(sampleRate);
    osc.SetAmp(1.0f);
    variableSaw.Init(sampleRate);
    vosim.Init(sampleRate);
    stringVoice.Init(sampleRate);
    particle.Init(sampleRate);
    synthBassDrum.Init(sampleRate);
    grainlet.Init(sampleRate);
    overdrive.Init();
    spark.SetLedCalibration(Spark::LedTarget::Onboard, kOnboardLedCalibration);
    DebugInit();

    freqParam.Init(spark.knob1, 20.0f, 2000.0f, Parameter::LOGARITHMIC);
    shapeParam.Init(spark.knob2, 0.0f, 1.0f, Parameter::LINEAR);

    spark.StartAudio(AudioCallback);

    while(1)
    {
        runtime.ProcessControls();
        ProcessEncoder();
        ProcessKnobs();
        UpdateLeds();
        DebugMaybePrintI2cScan();
        DebugMaybeStatus();

        if(runtime.MaybeSave(storage, SAVE_DELAY_MS))
        {
            DebugLog(DBG_INFO, DBG_CAT_STORAGE, "settings saved");
        }
    }
}