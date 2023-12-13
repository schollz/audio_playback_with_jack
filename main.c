#include <fcntl.h>
#include <jack/jack.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// Global variables
jack_client_t *client;
jack_port_t *output_port_left, *output_port_right;
SNDFILE *sndfile;
SF_INFO sfinfo;
struct termios original_term, nonblocking_term;

// The process callback
int process(jack_nframes_t nframes, void *arg) {
  float buffer[nframes * 2];  // Buffer for stereo (2 channels)
  float *out_left = (float *)jack_port_get_buffer(output_port_left, nframes);
  float *out_right = (float *)jack_port_get_buffer(output_port_right, nframes);

  // Read audio data (interleaved stereo)
  sf_readf_float(sndfile, buffer, nframes);

  // Deinterleave the audio data
  for (unsigned int i = 0; i < nframes; i++) {
    out_left[i] = buffer[2 * i];
    out_right[i] = buffer[2 * i + 1];
  }

  return 0;
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
        sf_seek(sndfile, (sf_count_t)(position * sfinfo.frames), SEEK_SET);
      }
    }

    // Exit the loop after the specified duration
    if (difftime(time(NULL), start_time) > run_for_seconds) {
      break;
    }
  }

  // Restore original IO settings
  restore_io();

  // Cleanup
  jack_client_close(client);
  sf_close(sndfile);

  return 0;
}