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
static Oscillator            supersawLow;
static Oscillator            supersawHigh;
static VariableSawOscillator variableSaw;
static VariableShapeOscillator variableShape;
static VosimOscillator       vosim;
static FormantOscillator     formantOsc;
static ZOscillator           zOsc;
static Fm2                   fm2;
static HarmonicOscillator<8> harmonicOsc;
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
static bool       octaveChangedPending      = false;
static float      lastKnob2ShapeLog         = -1.0f;
static float      lastKnob1LogValue         = -1.0f;
static float      shapeTarget               = -1.0f;
static float      shapeSmoothed             = -1.0f;
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
    WAVE_POLYBLEP_SQUARE = 3,
    WAVE_RAMP            = 4,
    WAVE_SUPERSAW        = 5,
    WAVE_COUNT           = 6
};

enum MacrosBankA {
    MACRO_A_VARIABLE_SAW = 0,
    MACRO_A_VARIABLE_SHAPE = 1,
    MACRO_A_FM2            = 2,
    MACRO_A_FORMANT        = 3,
    MACRO_A_HARMONIC       = 4,
    MACRO_A_ZOSC           = 5,
    MACRO_A_COUNT          = 6
};

enum MacrosBankB {
    MACRO_B_VOSIM           = 0,
    MACRO_B_STRING          = 1,
    MACRO_B_PARTICLE        = 2,
    // Placeholder name kept for UI/backward compatibility.
    // Implemented with GrainletOscillator until a dedicated ring-mod noise block is added.
    MACRO_B_RING_MOD_NOISE = 3,
    MACRO_B_OVERDRIVE      = 4,
    MACRO_B_BASS_DRUM_CLICK = 5,
    MACRO_B_COUNT           = 6
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

#ifndef SPARK_ENCODER_CLICK_WORKAROUND
#define SPARK_ENCODER_CLICK_WORKAROUND 1
#endif

#ifndef SPARK_TRACE_ENABLE
#define SPARK_TRACE_ENABLE 0
#endif

static constexpr float kMiddleC            = 261.63f;
static constexpr int   kKnobSemitoneSpan   = 12;
static constexpr int   kOctaveShiftMin     = -2;
static constexpr int   kOctaveShiftMax     = 2;
static constexpr uint32_t kBankSwitchGraceMs = 180;
static constexpr uint32_t kKnobFeedbackIntervalMs = 120;
static constexpr uint32_t kSw2HoldMs = 220;
static constexpr bool  kQuantizeToWesternSemitones = false;
static constexpr float kShapeDeadband = 0.005f;
static constexpr float kShapeSmoothingAlpha = 0.20f;
static constexpr float kShapeLogStep = 0.02f;
static constexpr float kKnob1Min = 0.00f;
static constexpr float kKnob1Max = 0.96f;
static constexpr float kKnob2Min = 0.00f;
static constexpr float kKnob2Max = 0.96f;
static constexpr float kWaveOutputGain = 0.68f;
static constexpr float kWaveGainSine = 1.0000f;
static constexpr float kWaveGainTri = 1.0307f;      // 4.37 / 4.24
static constexpr float kWaveGainSaw = 1.0069f;      // 4.37 / 4.34
static constexpr float kWaveGainSquare = 1.3571f;   // 4.37 / 3.22
static constexpr float kWaveGainRamp = 0.9541f;     // 4.37 / 4.58
static constexpr float kWaveGainSuperSaw = 0.9440f; // reduced to tame beat peaks (4.87V -> ~4.37V target)
static float            modifierHarmonics = 0.0f;
static float            modifierMorph = 0.0f;
static uint32_t         sw2PressStartMs = 0;
static bool             sw2ModifierActive = false;

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

static float CurrentShapeValue()
{
    if(shapeSmoothed < 0.0f)
    {
        return Knob2Calibrated();
    }
    return shapeSmoothed;
}

static float CurrentPitchFrequency(float pitchNorm)
{
    float semitones = ((pitchNorm * 2.0f) - 1.0f) * static_cast<float>(kKnobSemitoneSpan)
                      + static_cast<float>(octaveShift * 12);
    if(kQuantizeToWesternSemitones)
    {
        semitones = roundf(semitones);
    }
    return kMiddleC * powf(2.0f, semitones / 12.0f);
}

static float ApplyFinalLimiter(float input)
{
    return input;
}

template <size_t N>
static const char* NameFromTable(const char* const (&table)[N], int index, const char* fallback)
{
    return (index >= 0 && index < static_cast<int>(N)) ? table[index] : fallback;
}

static float CurrentModelOutputGain(const SparkSettings& current)
{
    if(current.sparkMode == MODE_WAVEFORMS)
    {
        switch(current.waveform)
        {
            case WAVE_SIN: return kWaveOutputGain * kWaveGainSine;
            case WAVE_POLYBLEP_TRI: return kWaveOutputGain * kWaveGainTri;
            case WAVE_POLYBLEP_SAW: return kWaveOutputGain * kWaveGainSaw;
            case WAVE_POLYBLEP_SQUARE: return kWaveOutputGain * kWaveGainSquare;
            case WAVE_RAMP: return kWaveOutputGain * kWaveGainRamp;
            case WAVE_SUPERSAW: return kWaveOutputGain * kWaveGainSuperSaw;
            default: return kWaveOutputGain;
        }
    }
    if(current.sparkMode == MODE_MACRO_A)
    {
        switch(current.macroA)
        {
            case MACRO_A_VARIABLE_SAW: return 0.86f;
            case MACRO_A_VARIABLE_SHAPE: return 0.86f;
            case MACRO_A_FM2: return 0.72f;
            case MACRO_A_FORMANT: return 0.82f;
            case MACRO_A_HARMONIC: return 0.74f;
            case MACRO_A_ZOSC: return 0.80f;
            default: return 0.80f;
        }
    }
    switch(current.macroB)
    {
        case MACRO_B_VOSIM: return 0.76f;
        case MACRO_B_STRING: return 0.80f;
        case MACRO_B_PARTICLE: return 0.72f;
        case MACRO_B_RING_MOD_NOISE: return 0.72f;
        case MACRO_B_OVERDRIVE: return 0.66f;
        case MACRO_B_BASS_DRUM_CLICK: return 0.80f;
        default: return 0.76f;
    }
}

struct ModelControls
{
    float timbre;
    float harmonics;
    float morph;
};

static ModelControls CurvedControls(const SparkSettings& current,
                                    float                timbre,
                                    float                harmonics,
                                    float                morph)
{
    ModelControls out = {timbre, harmonics, morph};

    if(current.sparkMode == MODE_WAVEFORMS)
    {
        out.timbre = powf(timbre, 0.85f);
        return out;
    }

    if(current.sparkMode == MODE_MACRO_A)
    {
        switch(current.macroA)
        {
            case MACRO_A_FM2:
                out.timbre    = powf(timbre, 1.40f);
                out.harmonics = powf(harmonics, 0.80f);
                out.morph     = powf(morph, 1.25f);
                break;
            case MACRO_A_HARMONIC:
                out.timbre    = (timbre * 2.0f) - 1.0f;
                out.harmonics = powf(harmonics, 0.90f);
                out.morph     = powf(morph, 1.35f);
                break;
            case MACRO_A_FORMANT:
            case MACRO_A_ZOSC:
                out.harmonics = powf(harmonics, 0.75f);
                out.morph     = powf(morph, 1.10f);
                break;
            default: break;
        }
        return out;
    }

    switch(current.macroB)
    {
        case MACRO_B_OVERDRIVE:
            out.timbre = powf(timbre, 1.35f);
            break;
        case MACRO_B_BASS_DRUM_CLICK:
            out.timbre = powf(timbre, 0.85f);
            break;
        default: break;
    }
    return out;
}

static const char* const kWaveModelNames[WAVE_COUNT] = {
    "Sine",
    "Tri",
    "Saw",
    "Square",
    "Ramp",
    "SuperSaw",
};

static const char* const kMacroAModelNames[MACRO_A_COUNT] = {
    "VarSaw",
    "VarShape",
    "FM2",
    "Formant",
    "Harmonic",
    "ZOsc",
};

static const char* const kMacroBModelNames[MACRO_B_COUNT] = {
    "Vosim",
    "String",
    "Particle",
    "Grainlet",
    "Drive",
    "Kick",
};

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

static const char* BankDisplayName(int mode)
{
    switch(mode)
    {
        case MODE_WAVEFORMS: return "Wave";
        case MODE_MACRO_A: return "MacroA";
        case MODE_MACRO_B: return "MacroB";
        default: return "Unknown";
    }
}

static const char* WaveName(int waveform)
{
    return NameFromTable(kWaveModelNames, waveform, "Wave?");
}

static const char* MacroAName(int macroA)
{
    return NameFromTable(kMacroAModelNames, macroA, "MacroA?");
}

static const char* MacroBName(int macroB)
{
    return NameFromTable(kMacroBModelNames, macroB, "MacroB?");
}

static const char* CurrentModelName(const SparkSettings& current)
{
    if(current.sparkMode == MODE_WAVEFORMS)
    {
        return WaveName(current.waveform);
    }
    if(current.sparkMode == MODE_MACRO_A)
    {
        return MacroAName(current.macroA);
    }
    return MacroBName(current.macroB);
}

static int ParamPct(float value);

static void RenderFriendlyStatusLine(const SparkSettings& current)
{
    if(DBG_INFO > diagnostics.Level() || (diagnostics.Mask() & DBG_CAT_STATE) == 0)
    {
        return;
    }
    char line[192];
    snprintf(line,
             sizeof(line),
             "%s %s freq=%dHz timbre=%d%% harmonics=%d%% morph=%d%%",
             BankDisplayName(current.sparkMode),
             CurrentModelName(current),
             static_cast<int>(current.waveformFreq),
             static_cast<int>(Knob2Calibrated() * 100.0f),
             ParamPct(modifierHarmonics),
             ParamPct(modifierMorph));
    spark.seed.Print("\r%-72s", line);
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
    RenderFriendlyStatusLine(current);
}

static void LogKnobFeedback(const char* source)
{
    const uint32_t now = System::GetNow();
    if((now - lastKnobFeedbackMs) < kKnobFeedbackIntervalMs)
    {
        return;
    }
    lastKnobFeedbackMs = now;

    char label[16];
    snprintf(label, sizeof(label), "%-10s", source);

    SparkSettings& current = storage.GetSettings();
    const bool     isK1Event = (source[1] == '1');
    const int      freqHz    = static_cast<int>(current.waveformFreq);
    const int      timbrePct = static_cast<int>(Knob2Calibrated() * 100.0f);
    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    isK1Event ? "%sfreq -> %dHz" : "%stimbre -> %d%%",
                    label,
                    isK1Event ? freqHz : timbrePct);
}

static int ParamPct(float value)
{
    if(value < 0.0f)
    {
        value = 0.0f;
    }
    if(value > 1.0f)
    {
        value = 1.0f;
    }
    return static_cast<int>(value * 100.0f);
}

static void LogModifierFeedback(const char* source)
{
    const uint32_t now = System::GetNow();
    if((now - lastKnobFeedbackMs) < kKnobFeedbackIntervalMs)
    {
        return;
    }
    lastKnobFeedbackMs = now;

    char label[16];
    snprintf(label, sizeof(label), "%-10s", source);

    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    (source[1] == '3') ? "%sharmonics -> %d%%" : "%smorph -> %d%%",
                    label,
                    (source[1] == '3') ? ParamPct(modifierHarmonics) : ParamPct(modifierMorph));
}

static void LogModifierState(const char* state)
{
    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    "%-10smod -> %s",
                    "sw2 hold",
                    state);
}

static void LogSwitchFeedback(const char* switch_name)
{
    const float pitchNorm = Knob1Calibrated();
    const float targetFreq = CurrentPitchFrequency(pitchNorm);
    char        label[16];
    snprintf(label, sizeof(label), "%s push", switch_name);

    diagnostics.Log(DBG_INFO,
                    DBG_CAT_CTRL,
                    "%-10soct -> %d freq -> %dHz",
                    label,
                    octaveShift,
                    static_cast<int>(targetFreq));
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

#if SPARK_TRACE_ENABLE
#define TRACE_CTRL_LOG(...) DebugLog(DBG_TRACE, DBG_CAT_CTRL, __VA_ARGS__)
#else
#define TRACE_CTRL_LOG(...) \
    do                     \
    {                      \
    } while(0)
#endif

static void DebugInit()
{
#if SPARK_DEBUG_ENABLE
    diagnostics.Init(SPARK_DEBUG_DEFAULT_LEVEL, SPARK_DEBUG_DEFAULT_MASK, "oscillators");
    DebugLog(DBG_INFO,
             DBG_CAT_STATE,
             "spark debug on: level=%d mask=0x%02x",
             diagnostics.Level(),
             diagnostics.Mask());
#if SPARK_ENCODER_CLICK_WORKAROUND
    DebugLog(DBG_INFO,
             DBG_CAT_STATE,
             "bank select: hold sw2 while turning encoder");
#endif

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
    RenderFriendlyStatusLine(current);
}

static void MarkInteraction()
{
    runtime.MarkInteraction();
    TRACE_CTRL_LOG("interaction marked");
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
    const int32_t  inc     = -spark.encoder.Increment();
    const uint32_t nowMs   = System::GetNow();

    if(spark.encoder.RisingEdge())
    {
        encoderTurnedWhilePressed = false;
        lastEncoderPressMs        = nowMs;
        TRACE_CTRL_LOG("encoder press");
        DebugStatusNow("enc-press");
    }

    if(spark.encoder.FallingEdge())
    {
        TRACE_CTRL_LOG("encoder release");
        DebugStatusNow("enc-release");
    }

    if(inc != 0)
    {
        const char* encDir = (inc > 0) ? "enc up" : "enc down";
        char        encLabel[16];
        snprintf(encLabel, sizeof(encLabel), "%-10s", encDir);
        bool bankSelectActive = false;
#if SPARK_ENCODER_CLICK_WORKAROUND
        // Temporary workaround while encoder click is unavailable:
        // use sw2 as the modifier for bank selection while turning encoder.
        bankSelectActive = spark.button2.Pressed();
#else
        // Be tolerant of slight timing jitter between click and first detent.
        bankSelectActive
            = spark.encoder.Pressed() || ((nowMs - lastEncoderPressMs) <= kBankSwitchGraceMs);
#endif
        TRACE_CTRL_LOG("encoder turn inc=%ld enc=%d sw2=%d bank=%d",
                       static_cast<long>(inc),
                       spark.encoder.Pressed() ? 1 : 0,
                       spark.button2.Pressed() ? 1 : 0,
                       bankSelectActive ? 1 : 0);

        if(bankSelectActive)
        {
            current.sparkMode = WrapIndex(current.sparkMode + static_cast<int>(inc), MODE_COUNT);
            encoderTurnedWhilePressed = true;
            diagnostics.Log(DBG_INFO,
                            DBG_CAT_CTRL,
                            "%smode -> %s",
                            encLabel,
                            ModeName(current.sparkMode));
            DebugStatusNow("bank");
        }
        else
        {
            if(current.sparkMode == MODE_WAVEFORMS)
            {
                current.waveform = WrapIndex(current.waveform + static_cast<int>(inc), WAVE_COUNT);
                diagnostics.Log(DBG_INFO,
                                DBG_CAT_CTRL,
                                "%swave -> %d (%s)",
                                encLabel,
                                current.waveform,
                                WaveName(current.waveform));
                DebugStatusNow("wave");
            }
            else if(current.sparkMode == MODE_MACRO_A)
            {
                current.macroA = WrapIndex(current.macroA + static_cast<int>(inc), MACRO_A_COUNT);
                diagnostics.Log(DBG_INFO,
                                DBG_CAT_CTRL,
                                "%smacroA -> %d (%s)",
                                encLabel,
                                current.macroA,
                                MacroAName(current.macroA));
                DebugStatusNow("macroA");
            }
            else
            {
                current.macroB = WrapIndex(current.macroB + static_cast<int>(inc), MACRO_B_COUNT);
                diagnostics.Log(DBG_INFO,
                                DBG_CAT_CTRL,
                                "%smacroB -> %d (%s)",
                                encLabel,
                                current.macroB,
                                MacroBName(current.macroB));
                DebugStatusNow("macroB");
            }
        }
        MarkInteraction();
    }

    if(spark.encoder.FallingEdge() && !encoderTurnedWhilePressed)
    {
        if(current.sparkMode == MODE_MACRO_B && current.macroB == MACRO_B_STRING)
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
    const float shapeRaw = Knob2Calibrated();
    const float pitchNorm = Knob1Calibrated();

    if(sw2ModifierActive)
    {
        const bool k3Up = (modifierHarmonics <= 0.0f) ? true : (pitchNorm >= modifierHarmonics);
        const bool k4Up = (modifierMorph <= 0.0f) ? true : (shapeRaw >= modifierMorph);
        bool       changed = false;

        if(fabsf(pitchNorm - modifierHarmonics) > 0.01f)
        {
            modifierHarmonics = pitchNorm;
            changed    = true;
            LogModifierFeedback(k3Up ? "k3  up" : "k3  down");
        }
        if(fabsf(shapeRaw - modifierMorph) > 0.01f)
        {
            modifierMorph = shapeRaw;
            changed    = true;
            LogModifierFeedback(k4Up ? "k4  up" : "k4  down");
        }
        if(changed)
        {
            MarkInteraction();
            DebugStatusNow("sw2-mod");
        }
        return;
    }

    // Musical pitch mapping centered at middle C.
    const float newFreq = CurrentPitchFrequency(pitchNorm);
    if(fabsf(newFreq - current.waveformFreq) > 0.2f)
    {
        const bool k1Up = (lastKnob1LogValue < 0.0f) ? true : (pitchNorm >= lastKnob1LogValue);
        current.waveformFreq = newFreq;
        MarkInteraction();
        TRACE_CTRL_LOG("freq -> %.2f (oct=%d)", current.waveformFreq, octaveShift);
        if(octaveChangedPending)
        {
            // Switch press already emitted its own single-line feedback.
            octaveChangedPending = false;
        }
        else
        {
            LogKnobFeedback(k1Up ? "k1  up" : "k1  down");
        }
        lastKnob1LogValue = pitchNorm;
        DebugStatusNow("freq");
    }
    else
    {
        lastKnob1LogValue = pitchNorm;
    }

    (void)shapeParam.Process();
    if(shapeTarget < 0.0f || shapeSmoothed < 0.0f)
    {
        shapeTarget   = shapeRaw;
        shapeSmoothed = shapeRaw;
    }

    if(fabsf(shapeRaw - shapeTarget) > kShapeDeadband)
    {
        shapeTarget = shapeRaw;
    }

    shapeSmoothed += kShapeSmoothingAlpha * (shapeTarget - shapeSmoothed);
    const float shapeNow = shapeSmoothed;
    if(lastKnob2ShapeLog < 0.0f || fabsf(shapeNow - lastKnob2ShapeLog) > kShapeLogStep)
    {
        const bool k2Up = (lastKnob2ShapeLog < 0.0f) ? true : (shapeNow >= lastKnob2ShapeLog);
        lastKnob2ShapeLog = shapeNow;
        MarkInteraction();
        TRACE_CTRL_LOG("shape -> %.3f", shapeNow);
        LogKnobFeedback(k2Up ? "k2  up" : "k2  down");
        DebugStatusNow("shape");
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    SparkSettings& current = storage.GetSettings();
    const float    timbreIn = CurrentShapeValue();
    const float    p3       = modifierHarmonics;
    const float    p4       = modifierMorph;
    const ModelControls controls = CurvedControls(current, timbreIn, p3, p4);
    const float         timbre   = controls.timbre;
    const float         harmonics = controls.harmonics;
    const float         morph     = controls.morph;

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
                    break;
                case WAVE_POLYBLEP_TRI:
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_TRI);
                    osc.SetPw(0.5f + ((timbre - 0.5f) * 0.3f));
                    sig = osc.Process();
                    break;
                case WAVE_POLYBLEP_SAW:
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
                    osc.SetPw(0.3f + (timbre * 0.4f));
                    sig = osc.Process();
                    break;
                case WAVE_RAMP:
                    osc.SetWaveform(Oscillator::WAVE_RAMP);
                    osc.SetPw(0.3f + (timbre * 0.4f));
                    sig = osc.Process();
                    break;
                case WAVE_SUPERSAW:
                {
                    const float detune = 0.002f + (harmonics * 0.035f);
                    const float spread = 0.55f + (morph * 0.45f);
                    const float f0 = current.waveformFreq;
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
                    supersawLow.SetFreq(f0 * (1.0f - detune));
                    supersawHigh.SetFreq(f0 * (1.0f + detune));
                    const float center = osc.Process();
                    const float low = supersawLow.Process();
                    const float high = supersawHigh.Process();
                    const float mix = (center * (1.0f - (spread * 0.35f)))
                                      + ((low + high) * (0.5f * spread * 0.90f));
                    sig = mix;
                    break;
                }
                case WAVE_POLYBLEP_SQUARE:
                    // Band-limited square; PW uses linear shape (not CurvedControls timbre pow).
                    osc.SetWaveform(Oscillator::WAVE_POLYBLEP_SQUARE);
                    {
                        const float pw = 0.10f + fclamp(timbreIn, 0.0f, 1.0f) * 0.80f;
                        osc.SetPw(pw);
                    }
                    sig = osc.Process();
                    break;
                default:
                    osc.SetWaveform(Oscillator::WAVE_SIN);
                    sig = osc.Process();
                    break;
            }
            // Neutral waveform path: no extra color stage.
        }
        else if(current.sparkMode == MODE_MACRO_A)
        {
            switch(current.macroA)
            {
                case MACRO_A_VARIABLE_SAW:
                    variableSaw.SetFreq(current.waveformFreq);
                    variableSaw.SetPW(((timbre * 2.0f) - 1.0f) * (0.4f + (harmonics * 0.6f)));
                    variableSaw.SetWaveshape((timbre * (1.0f - (morph * 0.5f))) + (morph * 0.5f));
                    sig = variableSaw.Process();
                    break;

                case MACRO_A_FM2:
                    fm2.SetFrequency(current.waveformFreq);
                    fm2.SetRatio(0.25f + (harmonics * 7.75f));
                    fm2.SetIndex(0.05f + (timbre * (0.95f + (morph * 8.0f))));
                    sig = fm2.Process();
                    break;

                case MACRO_A_FORMANT:
                    formantOsc.SetCarrierFreq(current.waveformFreq);
                    formantOsc.SetFormantFreq(current.waveformFreq * (1.0f + (timbre * (2.0f + (harmonics * 6.0f)))));
                    formantOsc.SetPhaseShift((morph * 2.0f) - 1.0f);
                    sig = formantOsc.Process();
                    break;

                case MACRO_A_ZOSC:
                    zOsc.SetFreq(current.waveformFreq);
                    zOsc.SetFormantFreq(current.waveformFreq * (1.0f + (timbre * (2.0f + (harmonics * 6.0f)))));
                    zOsc.SetShape((timbre * 0.7f) + (morph * 0.3f));
                    zOsc.SetMode((morph * 2.0f) - 1.0f);
                    sig = zOsc.Process();
                    break;

                case MACRO_A_VARIABLE_SHAPE:
                    variableShape.SetFreq(current.waveformFreq);
                    variableShape.SetPW(0.02f + (timbre * 0.96f));
                    variableShape.SetWaveshape((timbre * 0.65f) + (harmonics * 0.35f));
                    variableShape.SetSync(true);
                    variableShape.SetSyncFreq(current.waveformFreq * (0.25f + (morph * 7.75f)));
                    sig = variableShape.Process();
                    break;

                case MACRO_A_HARMONIC:
                {
                    harmonicOsc.SetFreq(current.waveformFreq);
                    harmonicOsc.SetFirstHarmIdx(1 + static_cast<int>(harmonics * 6.0f));
                    float amps[8];
                    const float tilt = timbre;
                    float sum = 0.0f;
                    for(int h = 0; h < 8; ++h)
                    {
                        const float rank = static_cast<float>(h + 1);
                        float       a    = powf(rank, -(1.0f + (morph * 2.0f)));
                        a *= (tilt >= 0.0f) ? powf(rank, -tilt) : powf(rank, fabsf(tilt));
                        amps[h] = a;
                        sum += a;
                    }
                    const float norm = (sum > 0.0f) ? (0.9f / sum) : 1.0f;
                    for(int h = 0; h < 8; ++h)
                    {
                        amps[h] *= norm;
                    }
                    harmonicOsc.SetAmplitudes(amps);
                    sig = harmonicOsc.Process();
                    break;
                }

                default:
                    sig = 0.0f;
                    break;
            }
        }
        else
        {
            switch(current.macroB)
            {
                case MACRO_B_VOSIM:
                    vosim.SetFreq(current.waveformFreq);
                    vosim.SetForm1Freq(current.waveformFreq * (1.0f + (timbre * (1.0f + (harmonics * 3.0f)))));
                    vosim.SetForm2Freq(current.waveformFreq * (2.0f + (timbre * (2.0f + (morph * 6.0f)))));
                    vosim.SetShape(((timbre * 2.0f) - 1.0f) * (0.35f + (harmonics * 0.65f)));
                    sig = vosim.Process();
                    break;

                case MACRO_B_STRING:
                    stringVoice.SetSustain(true);
                    stringVoice.SetFreq(current.waveformFreq);
                    stringVoice.SetAccent(0.35f + (harmonics * 0.65f));
                    stringVoice.SetStructure((timbre * 0.7f) + (harmonics * 0.3f));
                    stringVoice.SetBrightness((timbre * 0.6f) + (morph * 0.4f));
                    stringVoice.SetDamping(1.0f - (((timbre * 0.65f) + (morph * 0.35f)) * 0.8f));
                    sig = stringVoice.Process(false);
                    break;

                case MACRO_B_PARTICLE:
                    particle.SetFreq(current.waveformFreq);
                    particle.SetResonance(0.2f + (timbre * 0.75f));
                    particle.SetDensity(timbre);
                    particle.SetRandomFreq(0.4f + (timbre * 12.0f));
                    particle.SetSpread(4.0f * timbre);
                    particle.SetGain(0.75f);
                    sig = particle.Process();
                    break;

                case MACRO_B_RING_MOD_NOISE:
                    grainlet.SetFreq(current.waveformFreq);
                    grainlet.SetFormantFreq(current.waveformFreq * (2.0f + (timbre * 6.0f)));
                    grainlet.SetShape(0.2f + (timbre * 2.0f));
                    grainlet.SetBleed(timbre);
                    sig = grainlet.Process();
                    break;

                case MACRO_B_OVERDRIVE:
                    osc.SetWaveform(Oscillator::WAVE_SAW);
                    overdrive.SetDrive(timbre);
                    sig = overdrive.Process(osc.Process());
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
                    synthBassDrum.SetTone(timbre);
                    synthBassDrum.SetDecay(0.2f + (timbre * 0.8f));
                    synthBassDrum.SetDirtiness(timbre);
                    synthBassDrum.SetFmEnvelopeAmount(timbre);
                    synthBassDrum.SetFmEnvelopeDecay(timbre);
                    sig = synthBassDrum.Process(trig);
                    break;
                }

                default:
                    sig = 0.0f;
                    break;
            }
        }

        const float modelGain = CurrentModelOutputGain(current);
        const float voiced    = ApplyFinalLimiter(sig * modelGain);
        out[0][i]             = voiced;
        out[1][i]             = voiced;
    }
}

int main(void) {

    spark.Init();

    // Give I2C LED Driver and other components a chance to wake up.
    System::Delay(500);

    SparkSettings defaultSettings;
    defaultSettings.sparkMode    = MODE_WAVEFORMS;
    defaultSettings.waveform     = WAVE_SIN;
    defaultSettings.waveformFreq = kMiddleC;
    defaultSettings.macroA = 0;
    defaultSettings.macroB = 0;

    storage.Init(defaultSettings);
    // Neutral startup voice in Wave bank.
    storage.GetSettings().sparkMode = MODE_WAVEFORMS;
    storage.GetSettings().macroA    = WrapIndex(storage.GetSettings().macroA, MACRO_A_COUNT);
    storage.GetSettings().macroB    = WrapIndex(storage.GetSettings().macroB, MACRO_B_COUNT);
    spark.SetAudioBlockSize(4);
    sampleRate = spark.AudioSampleRate();

    osc.Init(sampleRate);
    osc.SetAmp(0.70f);
    supersawLow.Init(sampleRate);
    supersawLow.SetAmp(0.70f);
    supersawLow.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    supersawHigh.Init(sampleRate);
    supersawHigh.SetAmp(0.70f);
    supersawHigh.SetWaveform(Oscillator::WAVE_POLYBLEP_SAW);
    variableSaw.Init(sampleRate);
    variableShape.Init(sampleRate);
    vosim.Init(sampleRate);
    formantOsc.Init(sampleRate);
    zOsc.Init(sampleRate);
    fm2.Init(sampleRate);
    harmonicOsc.Init(sampleRate);
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

        const uint32_t nowMs = System::GetNow();
        if(spark.button2.RisingEdge())
        {
            sw2PressStartMs   = nowMs;
            sw2ModifierActive = false;
        }
        if(spark.button2.Pressed() && !sw2ModifierActive
           && ((nowMs - sw2PressStartMs) >= kSw2HoldMs))
        {
            sw2ModifierActive = true;
            LogModifierState("on");
            DebugStatusNow("sw2-mod-on");
        }

        if(spark.button1.FallingEdge())
        {
            octaveShift = (octaveShift < kOctaveShiftMax) ? (octaveShift + 1) : kOctaveShiftMax;
            octaveChangedPending = true;
            MarkInteraction();
            LogSwitchFeedback("sw1");
            DebugStatusNow("oct-up");
        }
        if(spark.button2.FallingEdge())
        {
            if(sw2ModifierActive)
            {
                sw2ModifierActive = false;
                LogModifierState("off");
                DebugStatusNow("sw2-mod-off");
            }
            else
            {
                octaveShift = (octaveShift > kOctaveShiftMin) ? (octaveShift - 1) : kOctaveShiftMin;
                octaveChangedPending = true;
                MarkInteraction();
                LogSwitchFeedback("sw2");
                DebugStatusNow("oct-down");
            }
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