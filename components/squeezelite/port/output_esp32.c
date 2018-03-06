/*
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *      Ralph Irving 2015-2017, ralph_irving@hotmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Stdout output

#include "squeezelite.h"
#include "audio_renderer.h"

#define FRAME_BLOCK MAX_SILENCE_FRAMES

static log_level loglevel;

static bool running = true;

extern struct outputstate output;
extern struct buffer *outputbuf;

#define LOCK   mutex_lock(outputbuf->mutex)
#define UNLOCK mutex_unlock(outputbuf->mutex)

extern u8_t *silencebuf;
#if DSD
extern u8_t *silencebuf_dop;
#endif

// buffer to hold output data so we can block on writing outside of output lock, allocated on init
static u8_t *buf;
static unsigned buffill;
static int bytes_per_frame;
extern struct decodestate decode;
static int _stdout_write_frames(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR,
				s32_t cross_gain_in, s32_t cross_gain_out, s32_t **cross_ptr) {
     
  u8_t *obuf;

  
     if (!silence)
       {
	 obuf = outputbuf->readp;
	 _scale_and_pack_frames(buf + buffill * bytes_per_frame, (s32_t *)(void *)obuf, out_frames, gainL, gainR, output.format);
	 
	buffill += out_frames;

       }
       
     return (int)out_frames;
}

pcm_format_t _buffer_fmt = {
    .sample_rate = 44100,
    .bit_depth = I2S_BITS_PER_SAMPLE_16BIT,
    .num_channels = 2,
    .buffer_format = PCM_LEFT_RIGHT
};


static void *output_thread() {

     LOCK;

     switch (output.format) {
     case S32_LE:
	  bytes_per_frame = 4 * 2; break;
     case S24_3LE:
	  bytes_per_frame = 3 * 2; break;
     case S16_LE:
	  bytes_per_frame = 2 * 2; break;
     default:
	  bytes_per_frame = 4 * 2; break;
	  break;
     }

     UNLOCK;

     while (running) {

       LOCK;
       if (decode.state == 2)
	 {
	  

	   output.device_frames = 0;
	   output.updated = gettime_ms();
	   output.frames_played_dmp = output.frames_played;
	      
	   int size = _output_frames(FRAME_BLOCK);


	   LOG_INFO("buffill : %d %d", buffill, size);
	   LOG_INFO("Writing %d * %d bytes", bytes_per_frame, buffill);
	   render_samples((char*)buf, size * bytes_per_frame, &_buffer_fmt);		
	   
	   UNLOCK;  
	   
	 }
       else
	 {
	   UNLOCK;
	   LOG_INFO("sleep");
	   usleep(100000);
	      
	 }

     }

     return 0;
}

static thread_type thread;

void output_init_esp32(log_level level, unsigned output_buf_size, char *params, unsigned rates[], unsigned rate_delay) {
     loglevel = level;

     LOG_INFO("init output stdout");

     buf = malloc(FRAME_BLOCK * BYTES_PER_FRAME);
     if (!buf) {
	  LOG_ERROR("unable to malloc buf");
	  return;
     }
     buffill = 0;

     memset(&output, 0, sizeof(output));

     output.format = S32_LE;
     output.start_frames = FRAME_BLOCK * 2;
     output.write_cb = &_stdout_write_frames;
     output.rate_delay = rate_delay;

     if (params) {
	  if (!strcmp(params, "32"))	output.format = S32_LE;
	  if (!strcmp(params, "24")) output.format = S24_3LE;
	  if (!strcmp(params, "16")) output.format = S16_LE;
     }

     // ensure output rate is specified to avoid test open
     if (!rates[0]) {
	  rates[0] = 44100;
     }

     output_init_common(level, "-", output_buf_size, rates, 0);


     LOG_INFO("Pthread create output_thread");
     pthread_create(&thread, NULL, output_thread, NULL);
}


bool test_open(const char *device, unsigned rates[]) {
     return true;
}
void output_close_stdout(void) {
     LOG_INFO("close output");

     LOCK;
     running = false;
     UNLOCK;

     free(buf);

     output_close_common();
}
