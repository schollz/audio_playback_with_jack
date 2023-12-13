#include <fcntl.h>
#include <jack/jack.h>
#include <math.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#define CROSSFADE_DURATION 2.0  // Crossfade duration in seconds

// Global variables
jack_client_t *client;
jack_port_t *output_port_left, *output_port_right;
SNDFILE *sndfile, *sndfile_new = NULL;
SF_INFO sfinfo;
struct termios original_term, nonblocking_term;
float *crossfade_buffer = NULL;
int crossfade_samples = 0;
int crossfade_sample_counter = 0;

// The process callback
int process(jack_nframes_t nframes, void *arg) {
  float buffer[nframes * 2];
  float *out_left = (float *)jack_port_get_buffer(output_port_left, nframes);
  float *out_right = (float *)jack_port_get_buffer(output_port_right, nframes);

  // Read audio data (interleaved stereo)
  sf_readf_float(sndfile, buffer, nframes);

  // Process crossfade if active
  if (crossfade_sample_counter > 0) {
    for (unsigned int i = 0; i < nframes; i++) {
      float crossfade_ratio =
          (float)crossfade_sample_counter / crossfade_samples;
      out_left[i] =
          (1 - crossfade_ratio) * buffer[2 * i] +
          crossfade_ratio * crossfade_buffer[crossfade_sample_counter * 2];
      out_right[i] =
          (1 - crossfade_ratio) * buffer[2 * i + 1] +
          crossfade_ratio * crossfade_buffer[crossfade_sample_counter * 2 + 1];
      crossfade_sample_counter++;
      if (crossfade_sample_counter >= crossfade_samples) {
        crossfade_sample_counter = 0;
        break;
      }
    }
  } else {
    // Normal playback
    for (unsigned int i = 0; i < nframes; i++) {
      out_left[i] = buffer[2 * i];
      out_right[i] = buffer[2 * i + 1];
    }
  }

  return 0;
}

void initiate_crossfade(long new_position) {
  if (crossfade_buffer) free(crossfade_buffer);
  crossfade_samples = sfinfo.samplerate * CROSSFADE_DURATION;
  crossfade_sample_counter = 0;

  // Preload crossfade buffer
  crossfade_buffer = (float *)malloc(crossfade_samples * 2 * sizeof(float));
  sf_seek(sndfile, new_position - crossfade_samples / 2, SEEK_SET);
  sf_readf_float(sndfile, crossfade_buffer, crossfade_samples);
  sf_seek(sndfile, -crossfade_samples / 2,
          SEEK_CUR);  // Rewind to proper playback position

  crossfade_sample_counter = 1;  // Start crossfade
}

void set_nonblocking_io() {
  tcgetattr(STDIN_FILENO, &original_term);
  nonblocking_term = original_term;
  nonblocking_term.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &nonblocking_term);
}

void restore_io() { tcsetattr(STDIN_FILENO, TCSANOW, &original_term); }

int main(int argc, char *argv[]) {
  // Open the WAV file
  sndfile = sf_open("your_audio_file.wav", SFM_READ, &sfinfo);
  if (!sndfile) {
    fprintf(stderr, "Error opening WAV file.\n");
    return 1;
  }

  if (sfinfo.channels != 2) {
    fprintf(stderr, "Error: Only stereo files are supported.\n");
    sf_close(sndfile);
    return 1;
  }

  // Initialize the JACK client
  client = jack_client_open("Stereo_WAV_Player", JackNullOption, NULL);
  if (!client) {
    fprintf(stderr, "Error creating JACK client.\n");
    return 1;
  }

  // Set the process callback
  jack_set_process_callback(client, process, 0);

  // Create ports for left and right channels
  output_port_left = jack_port_register(
      client, "output_left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  output_port_right = jack_port_register(
      client, "output_right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

  // Activate the client
  if (jack_activate(client)) {
    fprintf(stderr, "Cannot activate client.\n");
    return 1;
  }

  // Connect the ports
  if (jack_connect(client, jack_port_name(output_port_left),
                   "system:playback_1")) {
    fprintf(stderr, "Cannot connect output left to system playback 1.\n");
  }
  if (jack_connect(client, jack_port_name(output_port_right),
                   "system:playback_2")) {
    fprintf(stderr, "Cannot connect output right to system playback 2.\n");
  }

  // Set non-blocking IO for keyboard input
  set_nonblocking_io();

  // Main loop
  time_t start_time = time(NULL);
  int run_for_seconds = 10;  // Specify how long the program should run

  while (1) {
    fd_set readfds;
    struct timeval tv;
    int retval;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    tv.tv_sec = 0;
    tv.tv_usec = 100000;  // 100 ms

    retval = select(1, &readfds, NULL, NULL, &tv);

    if (retval) {
      char ch;
      read(STDIN_FILENO, &ch, 1);
      if (ch >= 'a' && ch <= 'z') {
        float position = (float)(ch - 'a') / 25.0f;  // Normalize to 0-1
        long new_position = (long)(position * sfinfo.frames);
        initiate_crossfade(new_position);
      }
    }

    // Exit the loop after the specified duration
    if (difftime(time(NULL), start_time) > run_for_seconds) {
      break;
    }
  }

  if (crossfade_buffer) free(crossfade_buffer);

  // Restore original IO settings
  restore_io();

  // Cleanup
  jack_client_close(client);
  sf_close(sndfile);

  return 0;
}