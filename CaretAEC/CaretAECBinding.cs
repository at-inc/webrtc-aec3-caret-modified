using System;
using System.Runtime.InteropServices;

namespace Caret.Windows.AudioEngine.AEC
{
    /// <summary>
    /// C# wrapper for the CaretAEC native DLL
    /// </summary>
    public static class CaretAEC
    {
        private static bool _initialized = false;

        // DLL imports - make sure the DLL name matches your build output
        [DllImport("CaretAEC.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern bool CaretAEC_Initialize(int sampleRate, int channels, int defaultDelay = -5, int audioBufferDelay = -10);

        [DllImport("CaretAEC.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void CaretAEC_Shutdown();

        [DllImport("CaretAEC.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr CaretAEC_ProcessBuffers(
            [In] float[] systemBuffer,
            [In] float[] micBuffer,
            [Out] float[] outputBuffer,
            IntPtr bufferSize);

        /// <summary>
        /// Initialize the AEC module
        /// </summary>
        /// <param name="sampleRate">Sample rate in Hz (e.g., 48000)</param>
        /// <param name="channels">Number of audio channels (e.g., 1 for mono, 2 for stereo)</param>
        /// <param name="defaultDelay">Default delay for AEC configuration (default: -5)</param>
        /// <param name="audioBufferDelay">Audio buffer delay used during processing (default: -10)</param>
        /// <returns>True if initialization was successful</returns>
        public static bool Initialize(int sampleRate, int channels, int defaultDelay = -5, int audioBufferDelay = -10)
        {
            if (_initialized)
            {
                Shutdown();
            }

            _initialized = CaretAEC_Initialize(sampleRate, channels, defaultDelay, audioBufferDelay);
            return _initialized;
        }

        /// <summary>
        /// Process audio buffers to remove echo
        /// </summary>
        /// <param name="systemBuffer">Reference audio buffer (playback that might cause echo)</param>
        /// <param name="micBuffer">Microphone input buffer that contains the echo</param>
        /// <param name="outputBuffer">Buffer to receive the processed audio</param>
        /// <returns>Number of processed samples written to outputBuffer</returns>
        public static int ProcessBuffers(float[] systemBuffer, float[] micBuffer, float[] outputBuffer)
        {
            if (!_initialized)
            {
                throw new InvalidOperationException("AEC module not initialized. Call Initialize() first.");
            }

            if (systemBuffer == null || micBuffer == null || outputBuffer == null)
            {
                throw new ArgumentNullException("Audio buffers cannot be null");
            }

            if (systemBuffer.Length != micBuffer.Length || micBuffer.Length != outputBuffer.Length)
            {
                throw new ArgumentException("All audio buffers must have the same length");
            }

            IntPtr result = CaretAEC_ProcessBuffers(
                systemBuffer,
                micBuffer,
                outputBuffer,
                (IntPtr)systemBuffer.Length);

            return result.ToInt32();
        }

        /// <summary>
        /// Shutdown the AEC module
        /// </summary>
        public static void Shutdown()
        {
            if (_initialized)
            {
                CaretAEC_Shutdown();
                _initialized = false;
            }
        }
    }
} 
