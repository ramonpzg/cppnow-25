// recorder.cpp - Compile separately: g++ recorder.cpp -o recorder -lportaudio -lsndfile
#include <iostream>
#include <vector>
#include <portaudio.h>
#include <sndfile.h> // For writing WAV files
#include <chrono>
#include <thread>
#include <cstdio> // For fprintf, stderr

#define SAMPLE_RATE (44100)
#define FRAMES_PER_BUFFER (512)
#define NUM_SECONDS (5) // Record for 5 seconds
#define NUM_CHANNELS (1) // Mono
#define OUTPUT_FILENAME "recording.wav"

// Structure to hold recording data
struct RecordData {
    std::vector<float> recordedSamples;
    bool recording = true;
    size_t maxSamples = 0; // Limit sample storage
};

// PortAudio callback function
static int recordCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData) {
    RecordData *data = (RecordData*)userData;
    const float *rptr = (const float*)inputBuffer;
    unsigned long framesToRecord = framesPerBuffer;

    (void) outputBuffer; // Prevent unused variable warning

    // Check if we have space left (optional safety)
    if (data->recordedSamples.size() + framesPerBuffer > data->maxSamples && data->maxSamples > 0) {
        framesToRecord = data->maxSamples - data->recordedSamples.size();
        data->recording = false; // Signal to stop if we hit the limit early
    }


    if(inputBuffer == NULL) {
        // Handle cases where input is NULL if necessary, here just add zeros
         for(unsigned long i=0; i < framesToRecord; i++ ) {
             data->recordedSamples.push_back(0.0f); // Assuming mono
         }
    } else {
        for(unsigned long i=0; i < framesToRecord; i++ ) {
            // Assuming mono input for simplicity
             data->recordedSamples.push_back(*rptr++);
        }
    }

    // Check recording flag *after* processing the buffer
    return data->recording ? paContinue : paComplete;
}

// Function to handle PortAudio errors and termination
void handlePaError(PaError err, const char* stage) {
     if (err != paNoError) {
        fprintf( stderr, "PortAudio error during %s\n", stage );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        Pa_Terminate();
        exit(1); // Exit on error
     }
}


int main() {
    PaStreamParameters inputParameters;
    PaStream *stream = nullptr; // Initialize to nullptr
    PaError err;
    RecordData data;
    int totalFrames = NUM_SECONDS * SAMPLE_RATE;
    data.maxSamples = totalFrames * NUM_CHANNELS; // Set the limit
    data.recordedSamples.reserve(data.maxSamples); // Pre-allocate

    err = Pa_Initialize();
    handlePaError(err, "Initialization"); // Use helper function

    inputParameters.device = Pa_GetDefaultInputDevice();
    if (inputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default input device.\n");
        Pa_Terminate(); // Terminate before exiting
        return 1;
    }
    inputParameters.channelCount = NUM_CHANNELS;
    inputParameters.sampleFormat = paFloat32; // Use floats for simplicity
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
              &stream,
              &inputParameters,
              NULL, // No output
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff, // Don't clip
              recordCallback,
              &data );
    handlePaError(err, "Stream Opening");

    std::cout << "Recording for " << NUM_SECONDS << " seconds..." << std::endl;
    err = Pa_StartStream( stream );
    handlePaError(err, "Stream Starting");

    // Wait for recording duration
    std::this_thread::sleep_for(std::chrono::seconds(NUM_SECONDS));
    data.recording = false; // Signal callback to stop

    // Wait a short moment for the callback to finish processing the last buffer
    // Adjust timing if needed, ensures the callback sees recording=false
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    err = Pa_StopStream( stream ); // Stop stream *before* closing
    handlePaError(err, "Stream Stopping");

    err = Pa_CloseStream( stream );
    handlePaError(err, "Stream Closing");

    Pa_Terminate(); // Terminate PortAudio
    std::cout << "Recording finished." << std::endl;

    // --- Save to WAV file using libsndfile ---
    SF_INFO sfinfo;
    sfinfo.samplerate = SAMPLE_RATE;
    // Use actual recorded size which might be slightly different
    sfinfo.frames = data.recordedSamples.size() / NUM_CHANNELS;
    sfinfo.channels = NUM_CHANNELS;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT; // Save as float WAV

    SNDFILE* outfile = sf_open(OUTPUT_FILENAME, SFM_WRITE, &sfinfo);
    if (!outfile) {
        fprintf(stderr, "Error opening output file %s: %s\n", OUTPUT_FILENAME, sf_strerror(NULL));
        return 1; // Return error code
    }

    // Declare frames_written here
    sf_count_t frames_written = sf_write_float(outfile, data.recordedSamples.data(), data.recordedSamples.size());

    if (frames_written != (sf_count_t)data.recordedSamples.size()) {
         fprintf(stderr, "Error writing samples to file. Expected %zu, wrote %lld\n",
                 data.recordedSamples.size(), (long long)frames_written);
         // Still try to close the file
    } else {
         std::cout << "Successfully wrote " << frames_written << " samples to file." << std::endl;
    }


    if (sf_close(outfile) != 0) {
         fprintf(stderr, "Error closing output file.\n");
         return 1; // Return error code
    }
    std::cout << "Saved recording to " << OUTPUT_FILENAME << std::endl;

    return 0; // Success
}