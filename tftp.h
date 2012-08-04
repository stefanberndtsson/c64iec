#ifndef _TFTP_H_
#define _TFTP_H_

#define SRCPORT 53280

#define TFTP_NORQ   00
#define TFTP_RRQ    01
#define TFTP_WRQ    02
#define TFTP_DATA   03
#define TFTP_ACK    04
#define TFTP_ERROR  05
#define TFTP_OPTACK 06

#define TFTP_BLKSIZE 256
#define TFTP_GET TFTP_RRQ
#define TFTP_PUT TFTP_WRQ

extern byte tftpdata[];
extern volatile int tftp_request_in_progress;
extern volatile int tftp_error;

extern int tftp_get_file(char *filename);
extern int tftp_put_file(char *filename);
extern int tftp_put_block(byte *send_buffer, uint16_t size);
extern int tftp_get_block(byte *receive_buffer, byte offset);

extern int tftp_recv_packet(int plen);

#endif
