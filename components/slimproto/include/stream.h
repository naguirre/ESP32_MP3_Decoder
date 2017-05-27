#ifndef __STREAM_H__
#define __STREAM_H__

//#include "fifo.h"

//extern fifo_t stream_fifo;

void stream_task_cb(void * pvParameters);
void stream_start(uint32_t ip, uint16_t port, const char *header, size_t header_len);
void stream_stop(void);
  
#endif /* __STREAM_H__ */
