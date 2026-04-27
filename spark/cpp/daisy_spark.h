#pragma once
#ifndef DSY_SPARK_BSP_H
#define DSY_SPARK_BSP_H

#include "daisy_seed.h"
#include <cstdint>

namespace daisy_spark
{
    using namespace daisy;

enum DebugLevel : uint8_t
{
    DBG_OFF   = 0,
    DBG_ERROR = 1,
    DBG_INFO  = 2,
    DBG_TRACE = 3
};

enum DebugCategoryMask : uint8_t
{
    DBG_CAT_CTRL    = 1 << 0,
    DBG_CAT_AUDIO   = 1 << 1,
    DBG_CAT_STATE   = 1 << 2,
    DBG_CAT_STORAGE = 1 << 3
};

/**
    @brief Class that handles initializing all of the hardware specific to the Spark Module. \n
    Helper funtions are also in place to provide easy access to built-in controls and peripherals.
    @author Charles H. Leggett
    @date April 2026
    @ingroup boards
*/
class Spark
{
  public:
    struct LedCalibration
    {
        float global_gain = 1.0f;
        float red_gain    = 1.0f;
        float green_gain  = 1.0f;
        float blue_gain   = 1.0f;
        float gamma       = 1.0f;
    };

    enum class LedTarget : uint8_t
    {
        Onboard = 0,
        Encoder = 1,
    };
    /** Switches */
    enum Sw
    {
        BUTTON_1,    /** & */
        BUTTON_2,    /** & */
        BUTTON_LAST, /** &  */
    };

    /** Knobs */
    enum Knob
    {
        KNOB_1,    /** &  */
        KNOB_2,    /** & */
        KNOB_LAST, /** & */
    };

    Spark() {}
    ~Spark() {}

    /** Init related stuff. */
    void Init(bool boost = false);

    /** Wait for a bit
    \param del Time to wait in ms.
    */
    void DelayMs(size_t del);

    /** Starts the callback
    \param cb Interleaved callback function
    */
    void StartAudio(AudioHandle::InterleavingAudioCallback cb);

    /** Starts the callback
    \param cb multichannel callback function
    */
    void StartAudio(AudioHandle::AudioCallback cb);

    /**
       Switch callback functions
       \param cb New interleaved callback function.
    */
    void ChangeAudioCallback(AudioHandle::InterleavingAudioCallback cb);

    /**
       Switch callback functions
       \param cb New multichannel callback function.
    */
    void ChangeAudioCallback(AudioHandle::AudioCallback cb);

    /** Stops the audio if it is running. */
    void StopAudio();

    /** Updates the Audio Sample Rate, and reinitializes.
     ** Audio must be stopped for this to work.
     */
    void SetAudioSampleRate(SaiHandle::Config::SampleRate samplerate);

    /** Returns the audio sample rate in Hz as a floating point number.
     */
    float AudioSampleRate();

    /** Sets the number of samples processed per channel by the audio callback.
     */
    void SetAudioBlockSize(size_t blocksize);

    /** Returns the number of samples per channel in a block of audio. */
    size_t AudioBlockSize();

    /** Returns the rate in Hz that the Audio callback is called */
    float AudioCallbackRate();

    /**
       Start analog to digital conversion.
     */
    void StartAdc();

    /** Stops Transfering data from the ADC */
    void StopAdc();

    /** Call at same rate as analog reads for smooth reading.*/
    void ProcessAnalogControls();

    /** Process Analog and Digital Controls */
    inline void ProcessAllControls()
    {
        ProcessAnalogControls();
        ProcessDigitalControls();
    }

    /** & */
    float GetKnobValue(Knob k);

    /** Process digital controls */
    void ProcessDigitalControls();

    /** Initialize I2C bus for onboard peripheral discovery/control. */
    bool InitPeripheralI2c();

    /** Scan 7-bit I2C addresses and return number of responsive devices. */
    uint8_t ScanI2cDevices(uint8_t* out_addresses,
                           uint8_t  max_addresses,
                           uint8_t  start_addr = 0x08,
                           uint8_t  end_addr   = 0x77,
                           uint32_t timeout_ms = 2);

    /** Reset Leds*/
    void ClearLeds();

    /** Update Leds to set colors*/
    void UpdateLeds();

    /** Set per-target LED calibration parameters. */
    void SetLedCalibration(LedTarget target, const LedCalibration& calibration);

    /** Apply per-target LED calibration to an RGB triplet. */
    void ApplyLedCalibration(LedTarget target,
                             float     in_r,
                             float     in_g,
                             float     in_b,
                             float&    out_r,
                             float&    out_g,
                             float&    out_b) const;

    /** Public Members */
    DaisySeed     seed;        /**<# */
    Encoder       encoder;     /**< & */
    AnalogControl knob1,       /**< & */
        knob2,                 /**< & */
        *knobs[KNOB_LAST];     /**< & */
    Switch button1,            /**< & */
        button2,               /**< & */
        *buttons[BUTTON_LAST]; /**< & */
    RgbLed led1,               /**< & */
        led2;                  /**< & */
    MidiUartHandler midi;
    I2CHandle       peripheral_i2c;
    bool            peripheral_i2c_ready_ = false;

  private:
    void SetHidUpdateRates();
    void InitButtons();
    void InitEncoder();
    void InitLeds();
    void InitKnobs();
    void InitMidi();
    static float Clamp01(float v);
    static float ApplyGamma(float v, float gamma);

    LedCalibration onboard_led_calibration_;
    LedCalibration encoder_led_calibration_;
};

/** Shared diagnostics helper for Spark firmware variants (oscillators/effects/filters). */
class SparkDiagnostics
{
  public:
    explicit SparkDiagnostics(Spark& spark) : spark_(spark) {}

    void Init(uint8_t level, uint8_t mask, const char* firmware_name);
    void PrintBanner(const char* firmware_name);
    void Log(uint8_t level, uint8_t category, const char* format, ...);
    void LogStatusLine(const char* mode_name,
                       int         primary_index,
                       int         secondary_a,
                       int         secondary_b,
                       float       frequency_hz,
                       float       knob1_value,
                       float       knob2_value,
                       bool        encoder_pressed,
                       bool        button1_pressed,
                       bool        button2_pressed);
    void RefreshStatusLine(const char* mode_name,
                           int         primary_index,
                           int         secondary_a,
                           int         secondary_b,
                           float       frequency_hz,
                           float       knob1_value,
                           float       knob2_value,
                           bool        encoder_pressed,
                           bool        button1_pressed,
                           bool        button2_pressed);
    void LogHeartbeat(const char* firmware_name, uint32_t interval_ms = 1000);
    bool StatusDue(uint32_t interval_ms);
    uint8_t Level() const { return level_; }
    uint8_t Mask() const { return mask_; }

  private:
    Spark&    spark_;
    uint8_t   level_          = DBG_INFO;
    uint8_t   mask_           = DBG_CAT_CTRL | DBG_CAT_AUDIO | DBG_CAT_STATE | DBG_CAT_STORAGE;
    uint32_t  last_status_ms_ = 0;
    uint32_t  last_heartbeat_ms_ = 0;
};

/** Shared main-loop helper for Spark firmware variants. */
class SparkRuntime
{
  public:
    SparkRuntime(Spark& spark, SparkDiagnostics& diagnostics)
        : spark_(spark), diagnostics_(diagnostics)
    {
    }

    void ProcessControls() { spark_.ProcessAllControls(); }
    void MarkInteraction();
    bool IsDirty() const { return dirty_; }

    template <typename T>
    bool MaybeSave(PersistentStorage<T>& storage, uint32_t delay_ms)
    {
        if(!dirty_)
        {
            return false;
        }
        if(System::GetNow() - last_interaction_ms_ <= delay_ms)
        {
            return false;
        }
        storage.Save();
        dirty_ = false;
        return true;
    }

  private:
    Spark&            spark_;
    SparkDiagnostics& diagnostics_;
    bool              dirty_               = false;
    uint32_t          last_interaction_ms_ = 0;
};

inline int WrapIndex(int value, int count)
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

} // namespace daisy_spark
#endif
