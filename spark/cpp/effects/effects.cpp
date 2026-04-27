#include "daisysp.h"
#include "daisy_spark.h"
#include <cstdarg>
#include <cstdio>

using namespace daisy;
using namespace daisysp;
using namespace daisy_spark;

static Spark            spark;
static SparkDiagnostics diagnostics(spark);
static SparkRuntime     runtime(spark, diagnostics);

static Oscillator osc;
static Overdrive  overdrive;
static Parameter  freqParam;
static Parameter  mixParam;

static const uint32_t SAVE_DELAY_MS = 3000U;

enum EffectModel
{
    FX_BYPASS = 0,
    FX_OVERDRIVE,
    FX_COUNT
};

struct EffectsSettings
{
    int   model;
    float oscFreq;
    float mix;

    bool operator!=(const EffectsSettings& a) const
    {
        return a.model != model || a.oscFreq != oscFreq || a.mix != mix;
    }
};

PersistentStorage<EffectsSettings> storage(spark.seed.qspi);

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
#define SPARK_DEBUG_STATUS_INTERVAL_MS 500U
#endif

static uint8_t debugLevel = SPARK_DEBUG_DEFAULT_LEVEL;
static uint8_t debugMask  = SPARK_DEBUG_DEFAULT_MASK;

static void DebugLog(uint8_t level, uint8_t category, const char* format, ...)
{
#if SPARK_DEBUG_ENABLE
    char    line[192];
    va_list args;
    va_start(args, format);
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
    diagnostics.Init(debugLevel, debugMask, "effects");
    DebugLog(DBG_INFO, DBG_CAT_STATE, "effects scaffold ready");
#endif
}

static void DebugStatus()
{
    if(!diagnostics.StatusDue(SPARK_DEBUG_STATUS_INTERVAL_MS))
    {
        return;
    }

    EffectsSettings& current = storage.GetSettings();
    diagnostics.LogStatusLine("effects",
                              current.model,
                              0,
                              0,
                              current.oscFreq,
                              spark.knob1.Value(),
                              spark.knob2.Value(),
                              spark.encoder.Pressed(),
                              spark.button1.Pressed(),
                              spark.button2.Pressed());
}

static void ProcessControls()
{
    EffectsSettings& current = storage.GetSettings();
    const int32_t    inc     = spark.encoder.Increment();
    if(inc != 0)
    {
        current.model = WrapIndex(current.model + static_cast<int>(inc), FX_COUNT);
        runtime.MarkInteraction();
        DebugLog(DBG_INFO, DBG_CAT_CTRL, "effect model -> %d", current.model);
    }

    const float newFreq = freqParam.Process();
    if(fabsf(newFreq - current.oscFreq) > 0.2f)
    {
        current.oscFreq = newFreq;
        runtime.MarkInteraction();
    }

    const float newMix = mixParam.Process();
    if(fabsf(newMix - current.mix) > 0.01f)
    {
        current.mix = newMix;
        runtime.MarkInteraction();
    }
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    EffectsSettings& current = storage.GetSettings();
    osc.SetFreq(current.oscFreq);

    for(size_t i = 0; i < size; i++)
    {
        const float dry = osc.Process();
        float       wet = dry;

        if(current.model == FX_OVERDRIVE)
        {
            overdrive.SetDrive(current.mix);
            wet = overdrive.Process(dry);
        }

        const float sig = (dry * (1.0f - current.mix)) + (wet * current.mix);
        out[0][i]       = sig * 0.4f;
        out[1][i]       = sig * 0.4f;
    }
}

int main(void)
{
    spark.Init();
    System::Delay(250);

    EffectsSettings defaults;
    defaults.model  = FX_BYPASS;
    defaults.oscFreq = 220.0f;
    defaults.mix     = 0.5f;
    storage.Init(defaults);

    spark.SetAudioBlockSize(4);
    const float sampleRate = spark.AudioSampleRate();
    osc.Init(sampleRate);
    osc.SetAmp(1.0f);
    overdrive.Init();

    freqParam.Init(spark.knob1, 20.0f, 2000.0f, Parameter::LOGARITHMIC);
    mixParam.Init(spark.knob2, 0.0f, 1.0f, Parameter::LINEAR);

    DebugInit();
    spark.StartAudio(AudioCallback);

    while(1)
    {
        runtime.ProcessControls();
        runtime.ProcessDebugButtons(debugLevel, debugMask);
        ProcessControls();
        DebugStatus();

        if(runtime.MaybeSave(storage, SAVE_DELAY_MS))
        {
            DebugLog(DBG_INFO, DBG_CAT_STORAGE, "effects settings saved");
        }
    }
}
