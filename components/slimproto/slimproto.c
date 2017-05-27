#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <netinet/in.h>

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/udp.h"
#include "lwip/debug.h"
#include "slimproto.h"
#include "stream.h"
#include "my_little_radio.h"
#include "math.h"
#include "log.h"

LOG_DOMAIN("slimproto")

#define PORT 3483
#define PLAYER_NAME_LEN 64
#define FIXED_CAP_LEN 256
#define VAR_CAP_LEN   128
#define ADDRESS_LEN 128
#define MAXBUF 4096
#define u64_t long long
#define MAX_HEADER 4096 // do not reduce as icy-meta max is 4080

static struct {
     u32_t updated;
     u32_t stream_start;
     u32_t stream_full;
     u32_t stream_size;
     u64_t stream_bytes;
     u32_t output_full;
     u32_t output_size;
     u32_t frames_played;
     u32_t device_frames;
     u32_t current_sample_rate;
     u32_t last;
     //	stream_state stream_state;
} status;

char player_name[PLAYER_NAME_LEN + 1] = "MyLittleRadio";
char server_ip[ADDRESS_LEN + 1];
short server_port = PORT;
int sock = -1;
bool sentSTMu, sentSTMo, sentSTMl;
static in_addr_t slimproto_ip = 0;
extern EventGroupHandle_t wifi_event_group;
static nvs_handle slimproto_nvs_handle;
// clock
u32_t gettime_ms(void) {
     struct timeval tv;
     gettimeofday(&tv, NULL);
     return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}


void packN(u32_t *dest, u32_t val) {
     u8_t *ptr = (u8_t *)dest;
     *(ptr)   = (val >> 24) & 0xFF; *(ptr+1) = (val >> 16) & 0xFF; *(ptr+2) = (val >> 8) & 0xFF;	*(ptr+3) = val & 0xFF;
}

void packn(u16_t *dest, u16_t val) {
     u8_t *ptr = (u8_t *)dest;
     *(ptr) = (val >> 8) & 0xFF; *(ptr+1) = val & 0xFF;
}

u32_t unpackN(u32_t *src) {
     u8_t *ptr = (u8_t *)src;
     return *(ptr) << 24 | *(ptr+1) << 16 | *(ptr+2) << 8 | *(ptr+3);
} 

u16_t unpackn(u16_t *src) {
     u8_t *ptr = (u8_t *)src;
     return *(ptr) << 8 | *(ptr+1);
} 


static int _discover_server(void)
{
     struct sockaddr_in d;
     struct sockaddr_in s;
     char *buf;

     int disc_sock = socket(AF_INET, SOCK_DGRAM, 0);

     socklen_t enable = 1;
     setsockopt(disc_sock, SOL_SOCKET, SO_BROADCAST, (const void *)&enable, sizeof(enable));

     buf = "e";

     memset(&d, 0, sizeof(d));
     d.sin_family = AF_INET;
     d.sin_port = htons(PORT);
     d.sin_addr.s_addr = htonl(INADDR_BROADCAST);

     do {

          DBG("sending discovery");
          memset(&s, 0, sizeof(s));

          if (sendto(disc_sock, buf, 1, 0, (struct sockaddr *)&d, sizeof(d)) < 0) {
               ERR("error sending disovery");
               vTaskDelay(100 / portTICK_PERIOD_MS);
               continue;
          }
          char readbuf[10];
          socklen_t slen = sizeof(s);
          recvfrom(disc_sock, readbuf, 10, 0, (struct sockaddr *)&s, &slen);
          DBG("got response from: %s:%d", inet_ntoa(s.sin_addr), ntohs(s.sin_port));
          vTaskDelay(100 / portTICK_PERIOD_MS);


     } while (s.sin_addr.s_addr == 0);

     closesocket(disc_sock);

     snprintf(server_ip, sizeof(server_ip), "%s", inet_ntoa(s.sin_addr));
     slimproto_ip = s.sin_addr.s_addr;
     server_port = ntohs(s.sin_port);
     return 1;
}

static void _send_packet(u8_t *packet, size_t len)
{
     u8_t *ptr = packet;
     unsigned try = 0;
     ssize_t n;

     while (len)
     {
          n = send(sock, ptr, len, 0);
          if (n <= 0)
          {
               if (n < 0 && try < 10)
               {
                    DBG("retrying (%d) writing to socket", ++try);
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    continue;
               }
               DBG("failed writing to socket: %s", strerror(errno));
               return;
          }
          ptr += n;
          len -= n;
//DBG("%d bytes sent", n);
     }
}


static void _send_helo()
{
     const char *base_cap = "Model=squeezelite,AccuratePlayPoints=1,HasDigitalOut=1,HasPolarityInversion=1,Firmware=v1.8.4-758,ModelName=SqueezeLite,MaxSampleRate=384000,aac,ogg,flc,aif,pcm,mp3";
     struct HELO_packet pkt;

     DBG("Send Helo");
     
     memset(&pkt, 0, sizeof(pkt));
     memcpy(&pkt.opcode, "HELO", 4);
     pkt.length = htonl(sizeof(struct HELO_packet) - 8 + strlen(base_cap));
     pkt.deviceid = 12; // squeezeplay
     pkt.revision = 0;
     packn(&pkt.wlan_channellist, 0x4000);
     packN(&pkt.bytes_received_H, (u64_t)status.stream_bytes >> 32);
     packN(&pkt.bytes_received_L, (u64_t)status.stream_bytes & 0xffffffff);
  
     esp_wifi_get_mac(0, pkt.mac);
  
     DBG("mac: %02x:%02x:%02x:%02x:%02x:%02x", pkt.mac[0], pkt.mac[1], pkt.mac[2], pkt.mac[3], pkt.mac[4], pkt.mac[5]);

     DBG("cap: %s", base_cap);

     _send_packet((u8_t *)&pkt, sizeof(pkt));
     _send_packet((u8_t *)base_cap, strlen(base_cap));
}

static void sendSETDName(const char *name) {
     struct SETD_header pkt_header;
  
     memset(&pkt_header, 0, sizeof(pkt_header));
     memcpy(&pkt_header.opcode, "SETD", 4);
  
     pkt_header.id = 0; // id 0 is playername S:P:Squeezebox2
     pkt_header.length = htonl(sizeof(pkt_header) + strlen(name) + 1 - 8);
  
     printf("set playername: %s\n", name);
  
     _send_packet((u8_t *)&pkt_header, sizeof(pkt_header));
     _send_packet((u8_t *)name, strlen(name) + 1);
}

static void sendSTAT(const char *event, u32_t server_timestamp) {
     struct STAT_packet pkt;
     u32_t now = gettime_ms();
     u32_t ms_played;

     if (status.current_sample_rate && status.frames_played && status.frames_played > status.device_frames) {
          ms_played = (u32_t)(((u64_t)(status.frames_played - status.device_frames) * (u64_t)1000) / (u64_t)status.current_sample_rate);
          printf("ms_played: %u (frames_played: %u device_frames: %u)\n", ms_played, status.frames_played, status.device_frames);
     } else if (status.frames_played && now > status.stream_start) {
          ms_played = now - status.stream_start;
          printf("ms_played: %u using elapsed time (frames_played: %u device_frames: %u)\n", ms_played, status.frames_played, status.device_frames);
     } else {
          DBG("ms_played: 0");
          ms_played = 0;
     }
	
     memset(&pkt, 0, sizeof(struct STAT_packet));
     memcpy(&pkt.opcode, "STAT", 4);
     pkt.length = htonl(sizeof(struct STAT_packet) - 8);
     memcpy(&pkt.event, event, 4);
     // num_crlf
     // mas_initialized; mas_mode;
     packN(&pkt.stream_buffer_fullness, status.stream_full);
     packN(&pkt.stream_buffer_size, status.stream_size);
     packN(&pkt.bytes_received_H, (u64_t)status.stream_bytes >> 32);
     packN(&pkt.bytes_received_L, (u64_t)status.stream_bytes & 0xffffffff);
     pkt.signal_strength = 0xffff;
     packN(&pkt.jiffies, now);
     packN(&pkt.output_buffer_size, status.output_size);
     packN(&pkt.output_buffer_fullness, status.output_full);
     packN(&pkt.elapsed_seconds, ms_played / 1000);
     // voltage;
     packN(&pkt.elapsed_milliseconds, ms_played);
     pkt.server_timestamp = server_timestamp; // keep this is server format - don't unpack/pack
     // error_code;

     /* printf("received bytesL: %u streambuf: %u outputbuf: %u calc elapsed: %u real elapsed: %u (diff: %d) device: %u delay: %d\n", */
     /*        (u32_t)status.stream_bytes, status.stream_full, status.output_full, ms_played, now - status.stream_start, */
     /*        ms_played - now + status.stream_start, status.device_frames * 1000 / status.current_sample_rate, now - status.updated); */
	

     _send_packet((u8_t *)&pkt, sizeof(pkt));
}


static int _connect_to_server(void)
{
     DBG("start tcp client");
     sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  
     DBG("socket: rc: %d", sock);
     struct sockaddr_in serverAddress;
     serverAddress.sin_family = AF_INET;
     inet_pton(AF_INET, server_ip, &serverAddress.sin_addr.s_addr);
     serverAddress.sin_port = htons(server_port);
  
     int rc = connect(sock, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in));
     DBG("connect rc: %d", rc);
     return rc;
}


struct handler {
     char opcode[5];
     void (*handler)(u8_t *, int);
};

static void _process_strm(u8_t *pkt, int len)
{
     struct strm_packet *strm = (struct strm_packet *)pkt;

     DBG("Process strm with command %c", strm->command);

     switch(strm->command)
     {
     case 't':
          sendSTAT("STMt", strm->replay_gain); // STMt replay_gain is no longer used to track latency, but support it
          break;
     case 'q':
          //decode_flush();
          //output_flush();
          status.frames_played = 0;
          //stream_disconnect();
          sendSTAT("STMf", 0);
          //buf_flush(streambuf);
          break;
     case 'f':
          //decode_flush();
          //output_flush();
          status.frames_played = 0;
          /* if (stream_disconnect()) { */
          /*   sendSTAT("STMf", 0); */
          /* } */
          //buf_flush(streambuf);
          break;
     case 'p':
     {
          unsigned interval = unpackN(&strm->replay_gain);

          /* output.pause_frames = interval * status.current_sample_rate / 1000; */
          /* if (interval) { */
          /* 	output.state = OUTPUT_PAUSE_FRAMES; */
          /* } else { */
          /* 	output.state = OUTPUT_STOPPED; */
          /* 	output.stop_time = gettime_ms(); */
          /* } */

          if (!interval) sendSTAT("STMp", 0);
          printf("pause interval: %u\n", interval);
     }
     break;
     case 'a':
     {
          unsigned interval = unpackN(&strm->replay_gain);
          /* output.skip_frames = interval * status.current_sample_rate / 1000; */
          /* output.state = OUTPUT_SKIP_FRAMES;				 */
          printf("skip ahead interval: %u\n", interval);
     }
     break;
     case 'u':
     {
          unsigned jiffies = unpackN(&strm->replay_gain);
          /* output.state = jiffies ? OUTPUT_START_AT : OUTPUT_RUNNING; */
          /* output.start_at = jiffies; */
          /* status.frames_played = output.frames_played; */
#if GPIO
          ampidle = 0;
#endif
          printf("unpause at: %u now: %u\n", jiffies, gettime_ms());
          sendSTAT("STMr", 0);
     }
     break;
     case 's':
     {
          unsigned header_len = len - sizeof(struct strm_packet);
          char *header = (char *)(pkt + sizeof(struct strm_packet));
          in_addr_t ip = (in_addr_t)strm->server_ip; // keep in network byte order
          u16_t port = strm->server_port; // keep in network byte order
          if (ip == 0) ip = slimproto_ip; 

          DBG("Stream start :\nautostart: %c transition period: %u transition type: %u codec: %c\n", 
                 strm->autostart, strm->transition_period, strm->transition_type - '0', strm->format);
			
          //autostart = strm->autostart - '0';
#if GPIO
          ampidle = 0;
#endif
          sendSTAT("STMf", 0);
          if (header_len > MAX_HEADER -1)
          {
               printf("header too long: %u\n", header_len);
               break;
          }
          if (strm->format != '?')
          {
               DBG("Codec : \n\tFormat\t%c\n\tSample size\t%d\n\tSample rate\t%d\n\tChannels\t%d\n\tEndiannes\t%d\n",
                   strm->format,
                   strm->pcm_sample_size,
                   strm->pcm_sample_rate,
                   strm->pcm_channels,
                   strm->pcm_endianness);
                   
               //codec_open(strm->format, strm->pcm_sample_size, strm->pcm_sample_rate, strm->pcm_channels, strm->pcm_endianness);
          } /* else if (autostart >= 2) { */
          /* 	// extension to slimproto to allow server to detect codec from response header and send back in codc message */
          /* 	printf("streaming unknown codec\n"); */
          /* }  */
          else
          {
               DBG("unknown codec requires autostart >= 2\n");
               break;
          }
          /* if (ip == LOCAL_PLAYER_IP && port == LOCAL_PLAYER_PORT) { */
          /* 	// extension to slimproto for LocalPlayer - header is filename not http header, don't expect cont */
          /* 	stream_file(header, header_len, strm->threshold * 1024); */
          /* 	autostart -= 2; */
          /* } else { */

          //stream_start(ip, port, header, header_len);
          
          /* } */

          
          
          sendSTAT("STMc", 0);
          sentSTMu = sentSTMo = sentSTMl = false;
          //output.threshold = strm->output_threshold;
          //output.next_replay_gain = unpackN(&strm->replay_gain);
          //output.fade_mode = strm->transition_type - '0';
          //output.fade_secs = strm->transition_period;
          //output.invert    = (strm->flags & 0x03) == 0x03;
          //printf("set fade mode: %u\n", output.fade_mode);
     }
     break;
     default:
          printf("unhandled strm %c\n", strm->command);
          break;
     }

}


static void _process_cont(u8_t *pkt, int len)
{
     DBG("Process cont");
}

static void _process_codc(u8_t *pkt, int len)
{
     DBG("Process codc");
}

static void _process_aude(u8_t *pkt, int len)
{
     DBG("Process aude"); 
}

static void _process_audg(u8_t *pkt, int len)
{
  float ldb, rdb, fvoll, fvolr;
     DBG("Process audg");
     struct audg_packet *audg = (struct audg_packet *)pkt;

     uint32_t voll = htonl(audg->gainL);
     uint32_t volr = htonl(audg->gainR);

     fvoll = voll / 65536.0;
     fvolr = volr / 65536.0;
     
     ldb = 20 * log10(voll); 
     rdb = 20 * log10(volr); 

     float vol;
     if (ldb > -30 && ldb <= 0)
       vol = voll * ((1 << 8) + 0.5) * (1 << 8);
     else
       vol = voll * (1 << 16) + 0.5;

     DBG("vol %3.3f", vol);
     
     DBG("Volume : %ul %ul | adjust : %ul | preamp %ul", voll, volr,
         audg->adjust, audg->preamp);
     DBG("Volume dB: %3.3f %3.3f %3.3f %3.3f\n", fvoll, fvolr, ldb, rdb);

}

static void _process_setd(u8_t *pkt, int len)
{
     DBG("Process setd");
     struct setd_packet *setd = (struct setd_packet *)pkt;

     // handle player name query and change
     if (setd->id == 0)
     {
          if (len == 5)
          {
               if (strlen(player_name))
               {
                    sendSETDName(player_name);
               }
          }
          else if (len > 5)
          {
               strncpy(player_name, setd->data, PLAYER_NAME_LEN);
               player_name[PLAYER_NAME_LEN] = '\0';
               DBG("set name: %s\n", setd->data);

               nvs_set_blob(slimproto_nvs_handle, "player_name",
                            player_name, strlen(player_name));
               nvs_commit(slimproto_nvs_handle);
               /* confirm change to server */
               sendSETDName(setd->data);
               
          }
     }

}
static void _process_serv(u8_t *pkt, int len)
{
     DBG("Process serv");
}

static struct handler handlers[] = {
     { "strm", _process_strm },
     { "cont", _process_cont },
     { "codc", _process_codc },
     { "aude", _process_aude },
     { "audg", _process_audg },
     { "setd", _process_setd },
     { "serv", _process_serv },
     { "",     NULL  },
};

static void _process(u8_t *pack, int len)
{
     struct handler *h = handlers;
     while (h->handler && strncmp((char *)pack, h->opcode, 4)) { h++; }

     if (h->handler)
     {
          DBG("opcode %s", h->opcode);
          h->handler(pack, len);
     } else
     {
          pack[4] = '\0';
          DBG("unhandled %s", (char *)pack);
     }
}


void slimproto_task(void *pvParameter)
{
     static u8_t buffer[MAXBUF];
     int  expect = 0;
     int  got    = 0;
     u32_t now;
     static u32_t last = 0;
     esp_err_t err;

     DBG("Starting slimproto_task");

     err = nvs_open("slimproto", NVS_READWRITE, &slimproto_nvs_handle);
     if (err != ESP_OK)
     {
         ERR("Error (%d) opening NVS!\n", err);
     }

     
     size_t player_name_size = 0;
     err = nvs_get_blob(slimproto_nvs_handle, "player_name", NULL, &player_name_size);
     if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
     {
          ERR("Unable to read player_name from NVS");
     }
     else
     {
           // Read previously saved blob if available
          if ((player_name_size > 0) && (player_name_size < PLAYER_NAME_LEN))
          {
               err = nvs_get_blob(slimproto_nvs_handle, "player_name", player_name, &player_name_size);
               if (err != ESP_OK)
               {
                    ERR("Error retrieving player_name from nvs");
               }
               else
               {
                    player_name[player_name_size] = '\0';
               }
          }
     }

     DBG("Player name : %s", player_name);
     
     while(1)
     {
          xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                              false, true, portMAX_DELAY);

          DBG("Connected. Launching discovery server");
          _discover_server();

          DBG("connecting to %s:%d", server_ip, server_port);
          _connect_to_server();

          _send_helo();
  
          while(1)
          {
               if (expect > 0)
               {
                    int n = recv(sock, buffer + got, expect, 0);
                    if (n <= 0)
                    {
                         DBG("error reading from socket: %s", n ? strerror(errno) : "closed");
                         break;
                    }
                    expect -= n;
                    got += n;
                    if (expect == 0)
                    {
                         //DBG("process buffer %d bytes", got);
                         _process(buffer, got);
                         got = 0;
                    }
               }
               else if (expect == 0)
               {
                    int n = recv(sock, buffer + got, 2 - got, 0);
                    if (n <= 0)
                    {
                         ERR("error reading from socket: %s", n ? strerror(errno) : "closed");
                         break;
                    }
                    got += n;
                    if (got == 2)
                    {
                         expect = buffer[0] << 8 | buffer[1]; // length pack 'n'
                         got = 0;
                         if (expect > MAXBUF)
                         {
                              ERR("FATAL: slimproto packet too big: %d > %d", expect, MAXBUF);
                              break;
                         }
                    }
               }
               else
               {
                    ERR("FATAL: negative expect");
                    break;
               }
          }
     }
}

void slimproto_start(void)
{
    DBG("Slimproto Start\n");
    xTaskCreatePinnedToCore(&slimproto_task, "slimproto_task", 2560, NULL, 20,
                            NULL, 0);
}
