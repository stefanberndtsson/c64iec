#include <EtherCard.h>
#include "tftp.h"

/* #define SRQIN 2 */
#define ATN 2
#define CLOCK 3
#define DATA 4
#define AVR_EOI A0
#define AVR_DEBUG A1
#define AVR_TIMEOUT A2

/* if defined, accept device 8-15, if not only 8
 * 
 * if multi, prepend <devicenum-HEX>/ before path to tftp
 */
#define MULTIDEVICE 1

#define IEC_COMMAND  0xf0
#define IEC_DEVICE   0x0f
#define IEC_UNTALK   0x50
#define IEC_UNLISTEN 0x30
#define IEC_LISTEN   0x20
#define IEC_TALK     0x40
#define IEC_MODE     0xf0
#define IEC_ADDRESS  0x0f
#define IEC_OPEN     0xf0
#define IEC_CLOSE    0xe0
#define IEC_FASTLOAD 0xd0
#define IEC_DATA     0x60

#define TIMEOUT_COUNT 2700
#define DEVNO 8

#define C64_FILE_LIMIT 16
#define PC64_SIZE 0

typedef unsigned char uchar;

int iec_mode = 0;
uchar iobuf[TFTP_BLKSIZE+1];

uint16_t sent_to_c64 = 0;
byte device_mode = 0;
byte secondary = 0;

// ethernet interface mac address
static byte mymac[] = { 0x42,0x42,0x42,0x01,0x02,0x03 };

byte Ethernet::buffer[500];
static long timer;
static int dhcp_active = 0;

// Max filename length * 3 for extreme case, and 5 extra for extension + null termination
// Absolute maximum name
// Odd characters are coded as %xx, hence the *3.
static char tftp_filename[C64_FILE_LIMIT*3+5]; 
unsigned char hexarray[17] = "0123456789abcdef";

void create_tftp_filename(uchar *c64name) {
  int out_pos = C64_FILE_LIMIT*3+4;
  int out_size = out_pos;
  int start_pos = C64_FILE_LIMIT;
  int trailing = 1;
  if(strlen((const char *)c64name) < start_pos) start_pos = strlen((const char *)c64name);
  int i;
  for(i=start_pos-1;i>=0;--i) {
    if(c64name[i] == 0xa0 || c64name[i] == 0x00) continue;
    if(c64name[i] == ' ' && trailing) continue;
    trailing = 0;
    if((c64name[i] >= '0' && c64name[i] <= '9') ||
       (c64name[i] >= 'A' && c64name[i] <= 'Z') ||
       (c64name[i] == ' ' || c64name[i] == '-' || 
        c64name[i] == '_' || c64name[i] == '.')) {
      tftp_filename[--out_pos] = c64name[i];// tolower(c64name[i]);
    } else {
      out_pos -= 3;
      tftp_filename[out_pos] = '%';
      tftp_filename[out_pos+1] = hexarray[(c64name[i]>>4)&0xf];
      tftp_filename[out_pos+2] = hexarray[c64name[i]&0xf];
    }
  }
#if MULTIDEVICE
  memmove(tftp_filename+2, &tftp_filename[out_pos], out_size-out_pos);
  tftp_filename[0] = hexarray[device_mode&0xf];
  tftp_filename[1] = '/';
  tftp_filename[out_size-out_pos+2] = '\0';
#else
  memmove(tftp_filename, &tftp_filename[out_pos], out_size-out_pos);
  tftp_filename[out_size-out_pos] = '\0';
#endif
  //  tftp_filename[out_size-out_pos+1] = 'p';
  //  tftp_filename[out_size-out_pos+2] = '0';
  //  tftp_filename[out_size-out_pos+3] = '0';
  //  tftp_filename[out_size-out_pos+4] = '\0';
}

void pc64_make_header(byte *iobuf) {
  int len;
  len = strlen((const char *)iobuf);
  if(len <= C64_FILE_LIMIT) {
    for(int i=0;i<(C64_FILE_LIMIT-len);i++) {
      iobuf[len+i] = '\0';
    }
  }

  memmove(iobuf+8, iobuf, C64_FILE_LIMIT);
  strcpy((char *)iobuf, "C64File");
  iobuf[24] = '\0';
  iobuf[25] = '\0'; /* REL Record size */
}

void ethernet_probe_for_packet() {
  int plen;
  plen = ether.packetReceive();
  if(!tftp_recv_packet(plen))
    ether.packetLoop(plen);

  /* Check if this packet is interesting, return if not, or if plen == 0 */
}

static inline void clear_iobuf() {
  for(int i=0;i<sizeof(iobuf);i++) {
    iobuf[i] = '\0';
  }
}

static inline int atn_active() {
  if(digitalRead(ATN) == LOW) return 1;
  return 0;
}

static inline int data_active() {
  if(digitalRead(DATA) == LOW) return 1;
  return 0;
}

static inline int clock_active() {
  if(digitalRead(CLOCK) == LOW) return 1;
  return 0;
}

static inline void pull_data() {
  pinMode(DATA, OUTPUT);
  digitalWrite(DATA, LOW);
}

static inline void push_data() {
  pinMode(DATA, OUTPUT);
  digitalWrite(DATA, HIGH);
}

static inline void release_data() {
  pinMode(DATA, INPUT);
}

static inline void pull_clock() {
  pinMode(CLOCK, OUTPUT);
  digitalWrite(CLOCK, LOW);
}

static inline void push_clock() {
  pinMode(CLOCK, OUTPUT);
  digitalWrite(CLOCK, HIGH);
}

static inline void release_clock() {
  pinMode(CLOCK, INPUT);
}

static inline int wait_atn(int state) {
  pinMode(ATN, INPUT);
  int count = 0;
  while(digitalRead(ATN) != state) {
    count++;
    if(count > TIMEOUT_COUNT) return 0;
  }
  return 1;
}

static inline int wait_clock(int state) {
  pinMode(CLOCK, INPUT);
  int count = 0;
  digitalWrite(AVR_DEBUG, HIGH);
  while(digitalRead(CLOCK) != state) {
    count++;
    if(count > TIMEOUT_COUNT) {
      digitalWrite(AVR_DEBUG, LOW);
      digitalWrite(AVR_TIMEOUT, HIGH);
      return 0;
    }
  }
  return 1;
}

static inline int wait_clock_eoi(int state) {
  pinMode(CLOCK, INPUT);
  int count = 0;
  digitalWrite(AVR_DEBUG, HIGH);
  while(digitalRead(CLOCK) != state) {
    count++;
    if(count > TIMEOUT_COUNT/100) {
      digitalWrite(AVR_DEBUG, LOW);
      digitalWrite(AVR_TIMEOUT, HIGH);
      return 0;
    }
  }
  return 1;
}

static inline int wait_data(int state) {
  pinMode(DATA, INPUT);
  int count = 0;
  while(digitalRead(DATA) != state) {
    count++;
    if(count > TIMEOUT_COUNT) {
      digitalWrite(AVR_TIMEOUT, HIGH);
      return 0;
    }
  }
  return 1;
}

int get_byte(uchar *output, int force_eoi) {
  int eoi = 0;
  int time_first;
  int time_second;
  if(!wait_clock(HIGH)) return 0;
  release_data();
  if(!wait_data(HIGH)) return 0;
  digitalWrite(AVR_EOI, LOW);
  digitalWrite(AVR_EOI, HIGH);
  digitalWrite(AVR_EOI, LOW);

  if(!force_eoi) {
    if(!wait_clock_eoi(LOW)) {
      digitalWrite(AVR_EOI, HIGH);
      eoi = 1;
      push_data();
      delayMicroseconds(80);
      pull_data();
      delayMicroseconds(80);
      release_data();
    }
  } else {
    eoi = 1;
  }
  
  digitalWrite(AVR_TIMEOUT, LOW);

  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output = digitalRead(DATA);
  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output |= digitalRead(DATA) << 1;
  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output |= digitalRead(DATA) << 2;
  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output |= digitalRead(DATA) << 3;
  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output |= digitalRead(DATA) << 4;
  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output |= digitalRead(DATA) << 5;
  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output |= digitalRead(DATA) << 6;
  if(!wait_clock(LOW)) return 0;
  
  if(!wait_clock(HIGH)) return 0;
  *output |= digitalRead(DATA) << 7;
  if(!wait_clock(LOW)) return 0;
  
  pull_data();

  if(eoi) {
    release_clock();
    pull_data();
  }
  delayMicroseconds(100);

  return eoi;
}

int put_byte(uchar value, int eoi) {
  int tmp;
  int flag;
  
  delayMicroseconds(100);
  tmp = data_active();
  push_clock();
  if(tmp) {
    if(!wait_data(HIGH)) return 0;
    flag = eoi;
  } else {
    flag = 1;
  }
  if(flag) {
    if(!wait_data(HIGH)) return 0;
    if(!wait_data(LOW)) return 0;
  }

  pull_clock();
  if(!wait_data(HIGH)) return 0;

  for(int i = 0; i<8; i++) {
    delayMicroseconds(70);
    if(data_active()) return 1;
    if(value & (1<<i)) {
      push_data();
    } else {
      pull_data();
    }
    push_clock();
    delayMicroseconds(70);
    pull_clock();
    push_data();
  }

  delayMicroseconds(100);
  if(!wait_data(LOW)) return 0;

  return flag;
}

void handle_close(uchar sec_addr) {
  // Nothing really to do here...
}

void handle_open(uchar sec_addr) {
  int a = 0;
  int eoi;

  do {
    if(atn_active()) return;
    eoi = get_byte(&iobuf[a], 0);
    a++;
  } while(!eoi && a < sizeof(iobuf)-1);
  iobuf[a] = '\0';
}

int send_file() {
  int eoi = 0;
  int at_start = 1;
  int iobuf_offset = 0;
  int unsent_data = 0;
  int sent_this_time = 0;
  
  sent_to_c64 = 0;
  tftp_get_file(tftp_filename);

  while(!eoi && tftp_request_in_progress == TFTP_GET) {
    /* Fetch TFTP block repeatedly until we get data or last block. */
    while(unsent_data == 0 && tftp_request_in_progress == TFTP_GET) {
      ethernet_probe_for_packet();
      if(tftp_error) return 0;
      unsent_data = tftp_get_block(iobuf+iobuf_offset, (at_start ? PC64_SIZE : 0));
      if(unsent_data) {
	sent_this_time = 0;
      }
      if(tftp_request_in_progress != TFTP_GET) {
	eoi = 1;
	break;
      }
    }
    if(unsent_data) {
      for(int i=0;i<unsent_data-1+iobuf_offset;i++) {
	put_byte(iobuf[i], 0);
	sent_to_c64++;
	sent_this_time++;
      }
      if(eoi) {
	if(at_start) {
	  put_byte(iobuf[unsent_data-1], eoi);
	  sent_to_c64++;
	  sent_this_time++;
	} else {
	  put_byte(iobuf[unsent_data], eoi);
	  sent_to_c64++;
	  sent_this_time++;
	}
      }
      unsent_data = 0;
    } else if(eoi) {
      put_byte(iobuf[0], 1);
      sent_to_c64++;
      sent_this_time++;
    }
    if(!eoi) {
      iobuf[0] = iobuf[TFTP_BLKSIZE-PC64_SIZE*at_start-1+iobuf_offset];
      iobuf_offset = 1;
      at_start = 0;
    }
  }
  
  return 1;
}

int recv_file() {
  int a;
  int eoi = 0;

  tftp_put_file(tftp_filename);
  //  pc64_make_header(iobuf);

  a=0;
  do {
    if(atn_active()) return 0;
    ethernet_probe_for_packet();
    eoi = get_byte(&iobuf[a], 0);
    a++;
    if(tftp_request_in_progress != TFTP_PUT || tftp_error) return 0;
    if(a == 256 || eoi) {
      while(!tftp_put_block(iobuf, a))
	ethernet_probe_for_packet();

      if(a == 256 && eoi) {
	while(!tftp_put_block(iobuf, 0))
	  ethernet_probe_for_packet();
      }
      a = 0;
    }
  } while(!eoi);

  return 1;
}

void handle_listen(uchar sec_addr) {
  if(sec_addr != 1) {
    Serial.print("Mode/Device: ");
    Serial.println(device_mode, HEX);
    Serial.print("Secondary: ");
    Serial.println(secondary, HEX);
    Serial.print("Filename: ");
    Serial.println((char *)iobuf);
    Serial.println("TODO: SAVE only for now!");
    // Only supporting SAVE
    return;
  }

  create_tftp_filename(iobuf);

  if(!recv_file()) {
    release_clock();
    release_data();
  }
}

void handle_talk(uchar sec_addr) {
  int eoi = 0;

  Serial.print("Mode/Device/Secondary: ");
  Serial.println(device_mode<<8|secondary, HEX);
  //  Serial.print("Secondary: ");
  //  Serial.println(secondary, HEX);
  //  Serial.print("Filename: ");
  Serial.println((char *)iobuf);

  create_tftp_filename(iobuf);

  if(!send_file()) {
    /* Should hopefully signal error */
    release_clock();
    release_data();
  }
}

void handle_atn() {
  uchar device,sec,sec_addr;
  uchar mode,sec_mode;

  wait_clock(HIGH);
  wait_clock(LOW);
  if(!get_byte(&device, 1)) return;
  mode = device & IEC_COMMAND;
  device_mode = device;
  if(mode == IEC_UNLISTEN) {
    wait_atn(HIGH);
    return;
  } else if(mode == IEC_UNTALK) {
    wait_atn(HIGH);
    return;
  } else if(mode == IEC_LISTEN) {
#if MULTIDEVICE
    if(device & IEC_DEVICE < 8 || device & IEC_DEVICE > 15) {
      /* Not our device */
      release_clock();
      release_data();
      Serial.println("LISTEN: Not our device");
      Serial.print("Mode/Device: ");
      Serial.println(device_mode, HEX);
      return;
    }
#else
    if(DEVNO != (device & IEC_DEVICE)) {
      /* Not our device */
      release_clock();
      release_data();
      Serial.println("LISTEN: Not our device");
      Serial.print("Mode/Device: ");
      Serial.println(device_mode, HEX);
      return;
    }
#endif
  } else if(mode == IEC_TALK) {
#if MULTIDEVICE
    if(device & IEC_DEVICE < 8 || device & IEC_DEVICE > 15) {
      /* Not our device */
      release_clock();
      release_data();
      Serial.println("TALK: Not our device");
      return;
    }
#else
    if(DEVNO != (device & IEC_DEVICE)) {
      /* Not our device */
      release_clock();
      release_data();
      Serial.println("TALK: Not our device");
      return;
    }
#endif
  }

  delayMicroseconds(200);
  if(!get_byte(&sec, 1)) return;
  secondary = sec;

  sec_mode = sec & IEC_MODE;
  if(sec_mode == IEC_OPEN) {
    wait_atn(HIGH);
    sec_addr = sec & IEC_ADDRESS;
    handle_open(sec_addr);
  } else if(sec_mode == IEC_CLOSE) {
    sec_addr = sec & IEC_ADDRESS;
    handle_close(sec_addr);
  } else if(sec_mode == IEC_DATA) {
    sec_addr = sec & IEC_ADDRESS;
    if(mode == IEC_LISTEN) {
      wait_atn(HIGH);
      handle_listen(sec_addr);
    } else if(mode == IEC_TALK) {
      release_clock();
      release_data();
      wait_atn(HIGH);
      delayMicroseconds(200);
      release_data();
      pull_clock();
      delayMicroseconds(200);
      handle_talk(sec_addr);
      release_data();
      release_clock();
    }
  } else if(sec_mode == IEC_FASTLOAD) {
    sec_addr = sec & IEC_ADDRESS;
    // handle_fastload();
  }
}

void setup() {
  /*  pinMode(SRQIN, OUTPUT); */
  pinMode(ATN, INPUT);
  pinMode(CLOCK, INPUT);
  pinMode(DATA, INPUT);
  pinMode(AVR_EOI, OUTPUT);
  pinMode(AVR_DEBUG, OUTPUT);
  pinMode(AVR_TIMEOUT, OUTPUT);

  clear_iobuf();

  pull_clock();
  digitalWrite(CLOCK, HIGH);
  release_clock();
  pull_data();
  
  Serial.begin(9600);

  Serial.println("Initializing Ethernet...");
  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0) {
    Serial.println( "Failed to access Ethernet controller");
  }
  Serial.println("Ethernet connected...");
  Serial.println("Sending DHCP Request...");
  if (!ether.dhcpSetup()) {
    Serial.println("DHCP failed");
  } else {
    dhcp_active = 1;
    Serial.println("DHCP Request received...");
    Serial.println("Fetching Server MAC...");
    ether.sendArpRequest(ether.serverip);
    while(!ether.serverMacKnown())
      ether.packetLoop(ether.packetReceive());
    Serial.println("Server MAC received...");
  }
}

void loop() {
  ethernet_probe_for_packet();

  if(atn_active()) {
    release_clock();
    pull_data();
    handle_atn();
  } else {
    delayMicroseconds(100);
  }
}
