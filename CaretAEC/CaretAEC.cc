#define CARET_AEC_EXPORTS

#include "CaretAEC.h"
#include "api/echo_canceller3_config.h"
#include "api/echo_canceller3_factory.h"
#include "audio_processing/audio_buffer.h"
#include "audio_processing/high_pass_filter.h"
#include "audio_processing/include/audio_processing.h"
#include "rtc_base/logging.h"

// C function that can be called from C#
extern "C" {
    // Bridge function to process audio buffers with AEC
    // Returns the number of processed samples written to outputBuffer
    CARET_AEC_API size_t CaretAEC_ProcessBuffers(const float* systemBuffer,
        const float* micBuffer,
        float* outputBuffer,
        size_t bufferSize) {
        if (systemBuffer == nullptr || micBuffer == nullptr ||
            outputBuffer == nullptr || bufferSize == 0) {
            return 0;
        }

        try {
            std::vector<float> systemBufferVec(systemBuffer, systemBuffer + bufferSize);
            std::vector<float> micBufferVec(micBuffer, micBuffer + bufferSize);

            std::vector<float> result = CaretAEC::shared().applyingEchoCancellation(
                systemBufferVec, micBufferVec);

            // Copy the result to the output buffer
            // The result is in deinterleaved format (LLLRRR)
            size_t resultSize = result.size();
            size_t copySize = std::min(resultSize, bufferSize);

            std::copy(result.begin(), result.begin() + copySize, outputBuffer);

            return copySize;
        }
        catch (const std::exception& e) {
            // In case of any exception, copy the original mic buffer as fallback
            std::copy(micBuffer, micBuffer + bufferSize, outputBuffer);
            return bufferSize;
        }
    }

    // Initialize the AEC module
    CARET_AEC_API bool CaretAEC_Initialize(int sampleRate, int channels) {
        try {
            return CaretAEC::shared().initialize(sampleRate, channels);
        }
        catch (const std::exception& e) {
            return false;
        }
    }

    // Shutdown the AEC module
    CARET_AEC_API void CaretAEC_Shutdown() {
        try {
            CaretAEC::shared().shutdown();
        }
        catch (const std::exception& e) {
            // Ignore exceptions during shutdown
        }
    }
}

CaretAEC& CaretAEC::shared() {
    static CaretAEC instance;
    return instance;
}

CaretAEC::CaretAEC()
    : sample_rate_(0),
    channels_(0),
    initialized_(false),
    echo_controller_(nullptr),
    hp_filter_(nullptr),
    ref_audio_(nullptr),
    aec_audio_(nullptr) {
}

CaretAEC::~CaretAEC() {
    shutdown();
}

bool CaretAEC::initialize(int sampleRate, int channels) {
    if (initialized_) {
        shutdown();
    }

    sample_rate_ = sampleRate;
    channels_ = channels;

    // AEC3 configuration
    webrtc::EchoCanceller3Config aec_config;

    // Filter settings
    aec_config.filter.main.length_blocks = 15;
    aec_config.filter.shadow.length_blocks = 15;
    aec_config.filter.main_initial.length_blocks = 12;
    aec_config.filter.shadow_initial.length_blocks = 12;
    aec_config.filter.export_linear_aec_output = false;

    // Echo path strength settings
    aec_config.ep_strength.default_len = 0.9f;
    //  aec_config.ep_strength.nearend_len = 0.8f;

    // Echo return loss enhancement settings
    aec_config.erle.min = 2.0f;
    aec_config.erle.max_l = 10.0f;
    aec_config.erle.max_h = 10.0f;

    // Delay settings
    aec_config.delay.default_delay = -5;
    aec_config.delay.use_external_delay_estimator = true;
    aec_config.delay.down_sampling_factor = 4;
    aec_config.delay.num_filters = 5;

    // Echo suppressor settings
    aec_config.suppressor.normal_tuning.mask_lf.enr_transparent = 0.8f;
    aec_config.suppressor.normal_tuning.mask_lf.enr_suppress = 0.9f;
    aec_config.suppressor.normal_tuning.mask_hf.enr_transparent = 0.8f;
    aec_config.suppressor.normal_tuning.mask_hf.enr_suppress = 0.9f;

    // Create AEC3 factory and echo controller
    webrtc::EchoCanceller3Factory aec_factory(aec_config);
    echo_controller_ = aec_factory.Create(sample_rate_, channels_, channels_);

    // Create high-pass filter
    hp_filter_ =
        std::make_shared<webrtc::HighPassFilter>(sample_rate_, channels_);

    // Audio buffer setup
    webrtc::StreamConfig stream_config(sample_rate_, channels_, false);

    // Create reference audio buffer (for far-end audio)
    ref_audio_ = std::make_shared<webrtc::AudioBuffer>(
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels());

    // Create AEC audio buffer (for near-end mic audio)
    aec_audio_ = std::make_shared<webrtc::AudioBuffer>(
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels(),
        stream_config.sample_rate_hz(), stream_config.num_channels());

    initialized_ = true;
    return true;
}

std::vector<float> CaretAEC::applyingEchoCancellation(
    const std::vector<float>& system_audio_buffer,
    const std::vector<float>& mic_audio_buffer) {
    if (!initialized_ || !echo_controller_ || !hp_filter_ || !ref_audio_ ||
        !aec_audio_) {
        return mic_audio_buffer;  // Return original buffer if not initialized
    }

    // Ensure buffers have the same size
    if (system_audio_buffer.size() != mic_audio_buffer.size()) {
        return mic_audio_buffer;  // Return original buffer if sizes don't match
    }

    const size_t num_samples = mic_audio_buffer.size();

    // Calculate frame size (process in 10ms chunks)
    const size_t samples_per_frame = sample_rate_ / 100;
    const size_t num_frames = num_samples / samples_per_frame;

    // Create result buffer
    std::vector<float> processed_buffer(mic_audio_buffer);

    // Stream configuration
    webrtc::StreamConfig stream_config(sample_rate_, channels_, false);

    // Process each frame
    for (size_t frame = 0; frame < num_frames; ++frame) {
        const size_t frame_offset = frame * samples_per_frame;

        // Create channel pointer arrays (for deinterleaved format)
        std::vector<const float*> system_channels(channels_);
        std::vector<const float*> mic_channels(channels_);
        std::vector<float*> out_channels(channels_);

        // Set data pointers for each channel
        for (int ch = 0; ch < channels_; ++ch) {
            // System audio channel pointer
            system_channels[ch] = &system_audio_buffer[frame_offset];

            // Mic audio channel pointer
            mic_channels[ch] = &mic_audio_buffer[frame_offset];

            // Output buffer channel pointer
            out_channels[ch] = &processed_buffer[frame_offset];
        }

        // Process far-end audio
        // Copy system audio data directly to AudioBuffer
        ref_audio_->CopyFrom(system_channels.data(), stream_config);

        // Split into frequency bands
        ref_audio_->SplitIntoFrequencyBands();

        // Analyze far-end audio
        echo_controller_->AnalyzeRender(ref_audio_.get());

        // Merge frequency bands
        ref_audio_->MergeFrequencyBands();

        // Process near-end mic audio
        // Copy mic audio data directly to AudioBuffer
        aec_audio_->CopyFrom(mic_channels.data(), stream_config);

        // Analyze capture audio
        echo_controller_->AnalyzeCapture(aec_audio_.get());

        // Split into frequency bands
        aec_audio_->SplitIntoFrequencyBands();

        // Apply high-pass filter
        hp_filter_->Process(aec_audio_.get(), true);

        // Set delay (adjust if needed)
        echo_controller_->SetAudioBufferDelay(-10);

        // Process echo cancellation
        echo_controller_->ProcessCapture(aec_audio_.get(), false);

        // Merge frequency bands
        aec_audio_->MergeFrequencyBands();

        // Copy processed result to output buffer
        aec_audio_->CopyTo(stream_config, out_channels.data());
    }

    return processed_buffer;
}

void CaretAEC::shutdown() {
    if (initialized_) {
        echo_controller_ = nullptr;
        hp_filter_ = nullptr;
        ref_audio_ = nullptr;
        aec_audio_ = nullptr;
        initialized_ = false;
    }
}