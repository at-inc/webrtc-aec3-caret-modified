#ifndef CARET_AEC_H
#define CARET_AEC_H

#include <memory>
#include <vector>

// Add Windows DLL export macros
#ifdef _WIN32
#ifdef CARET_AEC_EXPORTS
#define CARET_AEC_API __declspec(dllexport)
#else
#define CARET_AEC_API __declspec(dllimport)
#endif
#else
#define CARET_AEC_API
#endif

namespace webrtc {
    class AudioBuffer;
    class HighPassFilter;
    class EchoControl;
    struct EchoCanceller3Config;
    class StreamConfig;
}  // namespace webrtc

// C interface for Swift/C#
#ifdef __cplusplus
extern "C" {
#endif

    // Process audio buffers with AEC
    // Returns the number of processed samples written to outputBuffer
    // bufferSize is determined by the size of input buffers
    CARET_AEC_API size_t CaretAEC_ProcessBuffers(const float* systemBuffer,
        const float* micBuffer,
        float* outputBuffer,
        size_t bufferSize);

    // Initialize the AEC module
    CARET_AEC_API bool CaretAEC_Initialize(int sampleRate, int channels);

    // Shutdown the AEC module
    CARET_AEC_API void CaretAEC_Shutdown();

#ifdef __cplusplus
}
#endif

class CARET_AEC_API CaretAEC {
public:
    static CaretAEC& shared();

    // Initialize the AEC module with sample rate and channels
    bool initialize(int sampleRate, int channels);

    // Process audio buffers to remove echo
    // system_audio_buffer: The reference audio (playback) that might cause echo
    // mic_audio_buffer: The microphone input that contains the echo
    // Returns: Processed microphone buffer with echo removed
    std::vector<float> applyingEchoCancellation(
        const std::vector<float>& system_audio_buffer,
        const std::vector<float>& mic_audio_buffer);

    // Clean up resources
    void shutdown();

private:
    CaretAEC();
    ~CaretAEC();

    // Prevent copying
    CaretAEC(const CaretAEC&) = delete;
    CaretAEC& operator=(const CaretAEC&) = delete;

    // AEC3 components
    std::unique_ptr<webrtc::EchoControl> echo_controller_;
    std::shared_ptr<webrtc::HighPassFilter> hp_filter_;
    std::shared_ptr<webrtc::AudioBuffer> ref_audio_;
    std::shared_ptr<webrtc::AudioBuffer> aec_audio_;

    int sample_rate_;
    int channels_;
    bool initialized_;
};

#endif  // CARET_AEC_H 
