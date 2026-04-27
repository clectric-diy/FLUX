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
static uint32_t   lastEncoderPressMs        = 0;
static int        octaveShift               = 0;
static float      lastKnob2ShapeLog         = -1.0f;
static uint32_t   lastKnobFeedbackMs        = 0;
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

static constexpr float kMiddleC            = 261.63f;
static constexpr int   kKnobSemitoneSpan   = 12;
static constexpr int   kOctaveShiftMin     = -2;
static constexpr int   kOctaveShiftMax     = 2;
static constexpr uint32_t kBankSwitchGraceMs = 180;
static constexpr uint32_t kKnobFeedbackIntervalMs = 120;
static constexpr bool  kQuantizeToWesternSemitones = false;
static constexpr float kKnob1Min = 0.00f;
static constexpr float kKnob1Max = 0.96f;
static constexpr float kKnob2Min = 0.00f;
static constexpr float kKnob2Max = 0.96f;

static float CalibrateKnob(float raw, float minv, float maxv)
{
    if(maxv <= minv)
    {
        return 0.0f;
    }
    float v = (raw - minv) / (maxv - minv);
    if(v < 0.0f)
    {
        v = 0.0f;
    }
    if(v > 1.0f)
    {
        v = 1.0f;
    }
    return v;
}

static float Knob1Calibrated()
{
    return CalibrateKnob(spark.knob1.Value(), kKnob1Min, kKnob1Max);
}

static float Knob2Calibrated()
{
    return CalibrateKnob(spark.knob2.Value(), kKnob2Min, kKnob2Max);
}

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

static void DebugStatusNow(const char* reason)
{
    SparkSettings& current = storage.GetSettings();
    (void)reason;

    // Force immediate single-line refresh without adding scrollback noise.
    const float k1 = Knob1Calibrated();
    const float k2 = Knob2Calibrated();
    const bool  e  = spark.encoder.Pressed();
    const bool  b1 = spark.button1.Pressed();
    const bool  b2 = spark.button2.Pressed();

    diagnostics.RefreshStatusLine(ModeName(current.sparkMode),
                                  current.waveform,
                                  current.macroA,
                                  current.macroB,
                                  current.waveformFreq,
                                  k1,
                                  k2,
                                  e,
                                  b1,
                                  b2);
}

static void LogKnobFeedback(const char* source)
{
    const uint32_t now = System::GetNow();
    if((now - lastKnobFeedbackMs) < kKnobFeedbackIntervalMs)
    {
        return;
    }
    lastKnobFeedbackMs = now;

    SparkSettings& current = storage.GetSettings();
    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    "%s freq=%dHz shape=%d%% oct=%d",
                    source,
                    static_cast<int>(current.waveformFreq),
                    static_cast<int>(Knob2Calibrated() * 100.0f),
                    octaveShift);
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
                                  Knob1Calibrated(),
                                  Knob2Calibrated(),
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
    const uint32_t nowMs   = System::GetNow();

    if(spark.encoder.RisingEdge())
    {
        encoderTurnedWhilePressed = false;
        lastEncoderPressMs        = nowMs;
        DebugLog(DBG_TRACE, DBG_CAT_CTRL, "encoder press");
        DebugStatusNow("enc-press");
    }

    if(spark.encoder.FallingEdge())
    {
        DebugLog(DBG_TRACE, DBG_CAT_CTRL, "encoder release");
        DebugStatusNow("enc-release");
    }

    if(inc != 0)
    {
        // Be tolerant of slight timing jitter between click and first detent.
        const bool bankSelectActive
            = spark.encoder.Pressed() || ((nowMs - lastEncoderPressMs) <= kBankSwitchGraceMs);
        DebugLog(DBG_TRACE,
                 DBG_CAT_CTRL,
                 "encoder turn inc=%ld pressed=%d grace_ms=%lu bank=%d",
                 static_cast<long>(inc),
                 spark.encoder.Pressed() ? 1 : 0,
                 static_cast<unsigned long>(nowMs - lastEncoderPressMs),
                 bankSelectActive ? 1 : 0);

        if(bankSelectActive)
        {
            current.sparkMode = WrapIndex(current.sparkMode + static_cast<int>(inc), MODE_COUNT);
            encoderTurnedWhilePressed = true;
            DebugLog(DBG_INFO, DBG_CAT_CTRL, "bank -> %s", ModeName(current.sparkMode));
            LogKnobFeedback("bank");
            DebugStatusNow("bank");
        }
        else
        {
            if(current.sparkMode == MODE_WAVEFORMS)
            {
                current.waveform = WrapIndex(current.waveform + static_cast<int>(inc), WAVE_COUNT);
                DebugLog(DBG_INFO, DBG_CAT_CTRL, "waveform -> %d", current.waveform);
                LogKnobFeedback("wave");
                DebugStatusNow("wave");
            }
            else if(current.sparkMode == MODE_MACRO_A)
            {
                current.macroA = WrapIndex(current.macroA + static_cast<int>(inc), MACRO_A_COUNT);
                DebugLog(DBG_INFO, DBG_CAT_CTRL, "macroA -> %d", current.macroA);
                LogKnobFeedback("macroA");
                DebugStatusNow("macroA");
            }
            else
            {
                current.macroB = WrapIndex(current.macroB + static_cast<int>(inc), MACRO_B_COUNT);
                DebugLog(DBG_INFO, DBG_CAT_CTRL, "macroB -> %d", current.macroB);
                LogKnobFeedback("macroB");
                DebugStatusNow("macroB");
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
            DebugStatusNow("string-trig");
        }
        else if(current.sparkMode == MODE_MACRO_B
                && current.macroB == MACRO_B_BASS_DRUM_CLICK)
        {
            synthBassDrum.Trig();
            DebugLog(DBG_INFO, DBG_CAT_CTRL, "drum trig");
            DebugStatusNow("drum-trig");
        }
    }
}

void ProcessKnobs()
{
    SparkSettings& current = storage.GetSettings();
    // Musical pitch mapping centered at middle C.
    const float pitchNorm = Knob1Calibrated();
    float semitones = ((pitchNorm * 2.0f) - 1.0f) * static_cast<float>(kKnobSemitoneSpan)
                      + static_cast<float>(octaveShift * 12);
    if(kQuantizeToWesternSemitones)
    {
        semitones = roundf(semitones);
    }
    const float newFreq = kMiddleC * powf(2.0f, semitones / 12.0f);
    if(fabsf(newFreq - current.waveformFreq) > 0.2f)
    {
        current.waveformFreq = newFreq;
        MarkInteraction();
        DebugLog(DBG_TRACE, DBG_CAT_CTRL, "freq -> %.2f (oct=%d)", current.waveformFreq, octaveShift);
        LogKnobFeedback("knob1");
        DebugStatusNow("freq");
    }

    (void)shapeParam.Process();
    const float shapeNow = Knob2Calibrated();
    if(lastKnob2ShapeLog < 0.0f || fabsf(shapeNow - lastKnob2ShapeLog) > 0.02f)
    {
        lastKnob2ShapeLog = shapeNow;
        DebugLog(DBG_TRACE, DBG_CAT_CTRL, "shape -> %.3f", shapeNow);
        LogKnobFeedback("knob2");
        DebugStatusNow("shape");
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    SparkSettings& current = storage.GetSettings();
    const float    shape   = Knob2Calibrated();

    osc.SetFreq(current.waveformFreq);

    for(size_t i = 0; i < size; i++)
    {
        float sig = 0.0f;

        if(current.sparkMode == MODE_WAVEFORMS)
        {
            switch(current.waveform)
            {
                case WAVE_SIN:
                    // Sine-to-triangle tilt for useful low-harmonic shaping.
                    osc.SetWaveform(Oscillator::WAVE_SIN);
                    sig = osc.Process();
                    sig = ((1.0f - shape) * sig) + (shape * asinf(sig) * (2.0f / PI_F));
                    break;
                case WAVE_POLYBLEP_TRI:
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
                    osc.SetPw(0.5f + ((shape - 0.5f) * 0.3f));
                    sig = osc.Process();
                    break;
                case WAVE_POLYBLEP_SAW:
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
                    osc.SetPw(0.3f + (shape * 0.4f));
                    sig = osc.Process();
                    break;
                case WAVE_RAMP:
                    osc.SetWaveform(Oscillator::WAVE_RAMP);
                    osc.SetPw(0.3f + (shape * 0.4f));
                    sig = -1.0f * osc.Process();
                    break;
                case WAVE_POLYBLEP_SQUARE:
                default:
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
                    osc.SetPw(0.05f + (shape * 0.90f));
                    sig = osc.Process();
                    break;
            }
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
    spark.StartAdc();

    freqParam.Init(spark.knob1, 20.0f, 2000.0f, Parameter::LOGARITHMIC);
    shapeParam.Init(spark.knob2, 0.0f, 1.0f, Parameter::LINEAR);

    spark.StartAudio(AudioCallback);

    while(1)
    {
        runtime.ProcessControls();

        if(spark.button1.FallingEdge())
        {
            octaveShift = (octaveShift > kOctaveShiftMin) ? (octaveShift - 1) : kOctaveShiftMin;
            MarkInteraction();
            DebugLog(DBG_INFO, DBG_CAT_CTRL, "octave shift -> %d", octaveShift);
            DebugStatusNow("oct-down");
        }
        if(spark.button2.FallingEdge())
        {
            octaveShift = (octaveShift < kOctaveShiftMax) ? (octaveShift + 1) : kOctaveShiftMax;
            MarkInteraction();
            DebugLog(DBG_INFO, DBG_CAT_CTRL, "octave shift -> %d", octaveShift);
            DebugStatusNow("oct-up");
        }

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