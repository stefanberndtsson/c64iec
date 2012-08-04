#include <Arduino.h>
#include "EtherCard.h"
#include "tftp.h"

volatile int tftp_request_in_progress = 0;
volatile int tftp_filesize = 0;
volatile int tftp_wrq_datasize = 0;
volatile int tftp_wrq_lastblock = 0;
uint16_t tftp_srvport = 0;
uint16_t tftp_blknum = 0;
uint16_t tftp_lastblk = 0;
uint16_t tftp_lastgot = 0;
byte tftpdata[TFTP_BLKSIZE+4];

volatile int tftp_data_available = 0;
volatile int tftp_clear_to_send = 0;
volatile int tftp_end_of_data = 0;

#define TFTP_BLKNUM ((uint16_t)(ether.buffer[UDP_DATA_P+2]<<8|ether.buffer[UDP_DATA_P+3]))
#define TFTP_TYPE ((uint16_t)(ether.buffer[UDP_DATA_P]<<8|ether.buffer[UDP_DATA_P+1]))
#define TFTP_DATASIZE ((uint16_t)((ether.buffer[UDP_LEN_H_P]<<8|ether.buffer[UDP_LEN_L_P])-12))
#define TFTP_SRVPORT ((uint16_t)(ether.buffer[UDP_SRC_PORT_H_P]<<8|ether.buffer[UDP_SRC_PORT_L_P]))
#define TFTP_DSTPORT ((uint16_t)(ether.buffer[UDP_DST_PORT_H_P]<<8|ether.buffer[UDP_DST_PORT_L_P]))

int tftp_build_request(char *filename, uint16_t mode) {
  int size = 0;
  if(strlen(filename) > 16+1+3) return 0;
  tftpdata[0] = mode>>8;
  tftpdata[1] = mode;
  size += 2;
  strcpy((char *)&tftpdata[2], filename);
  size += strlen(filename)+1;
  strcpy((char *)&tftpdata[size], "octet");
  size += 6;
  strcpy((char *)&tftpdata[size], "blksize");
  size += 8;
  strcpy((char *)&tftpdata[size], "256");
  size += 4;
  return size;
}

int tftp_build_ack(int blknum) {
  int size = 0;
  tftpdata[0] = TFTP_ACK>>8;
  tftpdata[1] = TFTP_ACK;
  size += 2;
  tftpdata[2] = blknum>>8;
  tftpdata[3] = blknum;
  size += 2;
  return size;
}

int tftp_send_rrq(char *filename) {
  int rrq_size = 0;
  if(tftp_request_in_progress) return 0;
  rrq_size = tftp_build_request(filename, TFTP_RRQ);
  if(rrq_size <= 0) {
    Serial.println("Filename too long.");
    return 0;
  }
  
  ether.sendUdp((char *)tftpdata, rrq_size, SRCPORT, ether.serverip, 69);
  tftp_request_in_progress = TFTP_RRQ;
  tftp_filesize = 0;
  return rrq_size;
}

int tftp_send_wrq(char *filename) {
  int wrq_size = 0;
  if(tftp_request_in_progress) return 0;
  wrq_size = tftp_build_request(filename, TFTP_WRQ);
  if(wrq_size <= 0) {
    Serial.println("Filename too long.");
    return 0;
  }
  
  ether.sendUdp((char *)tftpdata, wrq_size, SRCPORT, ether.serverip, 69);
  tftp_request_in_progress = TFTP_WRQ;
  tftp_filesize = 0;
  return wrq_size;
}

int tftp_send_ack(uint16_t srvport, int blknum) {
  int ack_size = 0;
  ack_size = tftp_build_ack(blknum);
  ether.sendUdp((char *)tftpdata, ack_size, SRCPORT, ether.serverip, srvport);
}

int tftp_recv_data(uint16_t data_size) {
  int is_last = 0;
  tftp_filesize += data_size;
  if(data_size != TFTP_BLKSIZE || data_size == 0) is_last = 1;

  if(data_size > 0) {
    memcpy(tftpdata, &ether.buffer[UDP_DATA_P+4], data_size);
  }

  if(is_last) return -1;
  return data_size;
}

int tftp_send_data(uint16_t srvport, int blknum) {
  tftpdata[0] = TFTP_DATA>>8;
  tftpdata[1] = TFTP_DATA;
  tftpdata[2] = blknum>>8;
  tftpdata[3] = blknum;
  tftp_filesize += tftp_wrq_datasize;
  Serial.print("Sending packet ");
  Serial.print(blknum);
  Serial.print(": ");
  Serial.println(tftp_wrq_datasize);
  ether.sendUdp((char *)tftpdata, tftp_wrq_datasize+4, SRCPORT, ether.serverip, srvport);
}

int tftp_recv_packet(int plen) {
  int status = 0;
  if(plen <= 0) return 0;
  if(!ether.is_myIp(plen)) return 0;

  if(ether.buffer[IP_PROTO_P] == IP_PROTO_UDP_V && TFTP_DSTPORT == SRCPORT) {
    /* Have to extract these immediately, or the next packet we receive will overwrite the data */
    tftp_blknum = TFTP_BLKNUM;
    tftp_srvport = TFTP_SRVPORT;
    if(tftp_lastblk == tftp_blknum) return 0;
    //    Serial.print("Last block: "); Serial.print(tftp_lastblk); Serial.print("   Current block: "); Serial.println(tftp_blknum);
    tftp_lastblk = tftp_blknum;
    if(TFTP_TYPE == TFTP_OPTACK && tftp_request_in_progress != TFTP_WRQ) {
      tftp_send_ack(TFTP_SRVPORT, 0); /* Simply ACK this with blknum 0. No data to receive yet. */
    } else if(TFTP_TYPE == TFTP_DATA && tftp_request_in_progress == TFTP_RRQ) {
      status = tftp_recv_data(TFTP_DATASIZE);
      if(status != 0) tftp_data_available = TFTP_DATASIZE;
      if(status == -1) tftp_end_of_data = 1;
    } else if((TFTP_TYPE == TFTP_ACK || TFTP_TYPE == TFTP_OPTACK) && tftp_request_in_progress == TFTP_WRQ) {
      if(TFTP_TYPE == TFTP_OPTACK) tftp_blknum = 0;
      tftp_clear_to_send = 1;
    } else if(TFTP_TYPE == TFTP_ERROR) {
      tftp_request_in_progress = TFTP_NORQ;
    }
  }
}

int tftp_get_file(char *filename) {
  tftp_data_available = 0;
  tftp_end_of_data = 0;
  tftp_lastgot = 0;
  return tftp_send_rrq(filename);
}

int tftp_get_block(byte *receive_buffer, byte offset) {
  if(!tftp_data_available && !tftp_end_of_data) return 0;
  if(tftp_lastgot && tftp_lastgot >= tftp_blknum) return 0;
  if(tftp_data_available) {
    // We have data to copy. This may be 0 if total size was multiple of blksize
    // in which case we skip copying and just ACK
    memcpy(receive_buffer, tftpdata+offset, tftp_data_available-offset);
  }
  tftp_lastgot = tftp_blknum;
  tftp_send_ack(tftp_srvport, tftp_blknum);
  if(tftp_end_of_data) {
    tftp_request_in_progress = TFTP_NORQ;
  }
  return tftp_data_available-offset;
}

int tftp_put_file(char *filename) {
  tftp_clear_to_send = 0;
  return tftp_send_wrq(filename);
}

int tftp_put_block(byte *send_buffer, uint16_t size) {
  if(!tftp_clear_to_send) return 0;
  tftp_clear_to_send = 0;
  if(size > 0) {
    // We have data to copy. This may be 0 if total size was multiple of blksize
    // in which case we skip copying and just ACK
    memcpy(tftpdata+4, send_buffer, size);
  }
  tftp_wrq_datasize = size;
  tftp_send_data(tftp_srvport, tftp_blknum+1);
  if(size == 0 || size != TFTP_BLKSIZE) {
    tftp_request_in_progress = TFTP_NORQ;
  }
}

