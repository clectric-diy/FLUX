#include "daisysp.h"
#include "daisy_spark.h"
#include <cstdarg>
#include <cstdio>

using namespace daisy;
using namespace daisysp;
using namespace daisy_spark;

static Spark spark;

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

static uint32_t   lastInteractionTime = 0;
static bool       saveNeeded          = false;
static const auto SAVE_DELAY_MS       = 3000U;
static float      sampleRate          = 48000.0f;
static float      drumPhase           = 0.0f;
static bool       encoderTurnedWhilePressed = false;
static uint32_t   lastDebugStatusMs   = 0;

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

enum DebugLevel {
    DBG_OFF   = 0,
    DBG_ERROR = 1,
    DBG_INFO  = 2,
    DBG_TRACE = 3
};

enum DebugCategoryMask : uint8_t {
    DBG_CAT_CTRL    = 1 << 0,
    DBG_CAT_AUDIO   = 1 << 1,
    DBG_CAT_STATE   = 1 << 2,
    DBG_CAT_STORAGE = 1 << 3
};

static uint8_t debugLevel = SPARK_DEBUG_DEFAULT_LEVEL;
static uint8_t debugMask  = SPARK_DEBUG_DEFAULT_MASK;

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

static void DebugLog(uint8_t level, uint8_t category, const char* format, ...)
{
#if SPARK_DEBUG_ENABLE
    if(level > debugLevel || (debugMask & category) == 0)
    {
        return;
    }

    char    line[192];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    spark.seed.PrintLine("%s", line);
#else
    (void)level;
    (void)category;
    (void)format;
#endif
}

static void DebugInit()
{
#if SPARK_DEBUG_ENABLE
    spark.seed.StartLog();
    DebugLog(DBG_INFO, DBG_CAT_STATE, "spark debug on: level=%d mask=0x%02x", debugLevel, debugMask);
#endif
}

static void DebugMaybeStatus()
{
    const uint32_t now = System::GetNow();
    if(now - lastDebugStatusMs < SPARK_DEBUG_STATUS_INTERVAL_MS)
    {
        return;
    }
    lastDebugStatusMs = now;

    SparkSettings& current = storage.GetSettings();
    DebugLog(DBG_TRACE,
             DBG_CAT_STATE,
             "status mode=%s wf=%d a=%d b=%d freq=%.2f shape=%.2f",
             ModeName(current.sparkMode),
             current.waveform,
             current.macroA,
             current.macroB,
             current.waveformFreq,
             shapeParam.Value());
}

static void ProcessDebugButtons()
{
#if SPARK_DEBUG_ENABLE
    if(spark.button1.FallingEdge())
    {
        debugLevel = (debugLevel + 1) % 4;
        DebugLog(DBG_INFO, DBG_CAT_STATE, "debug level -> %d (0=off,1=err,2=info,3=trace)", debugLevel);
    }

    if(spark.button2.FallingEdge())
    {
        debugMask <<= 1;
        if(debugMask == 0 || debugMask > 0x08)
        {
            debugMask = 0x01;
        }
        DebugLog(DBG_INFO, DBG_CAT_STATE, "debug mask -> 0x%02x", debugMask);
    }
#endif
}

static inline int WrapIndex(int value, int count)
{
    while(value >= count)
    {
        value -= count;
    }
    while(value < 0)
    {
        value += count;
    }
    return value;
}

static void MarkInteraction()
{
    saveNeeded          = true;
    lastInteractionTime = System::GetNow();
    DebugLog(DBG_TRACE, DBG_CAT_CTRL, "interaction marked");
}

static void UpdateLeds()
{
    SparkSettings& current = storage.GetSettings();
    const float    modeMix = static_cast<float>(current.sparkMode) / static_cast<float>(MODE_COUNT - 1);
    const float    idxMix
        = (current.sparkMode == MODE_WAVEFORMS)
              ? static_cast<float>(current.waveform) / static_cast<float>(WAVE_COUNT - 1)
          : (current.sparkMode == MODE_MACRO_A)
              ? static_cast<float>(current.macroA) / static_cast<float>(MACRO_A_COUNT - 1)
              : static_cast<float>(current.macroB) / static_cast<float>(MACRO_B_COUNT - 1);

    spark.led1.Set(modeMix, 0.1f, 1.0f - modeMix);
    spark.led2.Set(idxMix, 1.0f - idxMix, 0.2f);
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
    DebugInit();
    lastDebugStatusMs = System::GetNow();

    freqParam.Init(spark.knob1, 20.0f, 2000.0f, Parameter::LOGARITHMIC);
    shapeParam.Init(spark.knob2, 0.0f, 1.0f, Parameter::LINEAR);

    spark.StartAudio(AudioCallback);

    while(1)
    {
        spark.ProcessAllControls();
        ProcessEncoder();
        ProcessKnobs();
        UpdateLeds();
        ProcessDebugButtons();
        DebugMaybeStatus();

        if(saveNeeded)
        {
            if(System::GetNow() - lastInteractionTime > SAVE_DELAY_MS)
            {
                storage.Save();
                saveNeeded = false;
                DebugLog(DBG_INFO, DBG_CAT_STORAGE, "settings saved");
            }
        }
    }
}