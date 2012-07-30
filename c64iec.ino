#define SRQIN 2
#define ATN 3
#define CLOCK 4
#define DATA 5
#define AVR_EOI 10
#define AVR_DEBUG 12
#define AVR_TIMEOUT 11

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

#define TIMEOUT_COUNT 2500
#define DEVNO 8

typedef unsigned char uchar;

int iec_mode = 0;
uchar iobuf[255];

// #define DEBUG 1

static inline void print_str(char *str) {
#if DEBUG
  Serial.print(str);
#endif
}

static inline void println_str(char *str) {
#if DEBUG
  Serial.println(str);
#endif
}

static inline void print_dec(int dec) {
#if DEBUG
  Serial.print(dec);
#endif
}

static inline void println_dec(int dec) {
#if DEBUG
  Serial.println(dec);
#endif
}

static inline void print_hex(int hex) {
#if DEBUG
  Serial.print(hex, HEX);
#endif
}

static inline void println_hex(int hex) {
#if DEBUG
  Serial.println(hex, HEX);
#endif
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
  //  *output = ~(*output);
  //  Serial.println(*output);

  if(eoi) {
    release_clock();
    pull_data();
  }
  delayMicroseconds(100);
  //  Serial.print("Time: ");
  //  Serial.println(time_second-time_first);

  return eoi;
}

void handle_close(uchar sec_addr) {
  print_str("CLOSE: ");
  println_hex(sec_addr);
}

void handle_open(uchar sec_addr, uchar *iobuf, uchar max_len) {
  int a = 0;
  int eoi;
  print_str("OPEN: ");
  println_hex(sec_addr);

  do {
    if(atn_active()) return;
    eoi = get_byte(&iobuf[a], 0);
    //    Serial.print("EOI: ");
    //    Serial.println(eoi);
    a++;
    //    Serial.print("Count: ");
    //    Serial.println(a);
  } while(!eoi && a < max_len-1);
  iobuf[a] = '\0';
  print_str("OPEN: Data == ");
  println_str((char *)iobuf);
  if(eoi) {
    println_str("OPEN: EOI triggered");
  }
  print_str("OPEN: Bytes received: ");
  println_dec(a);
  Serial.print("OPEN: Bytes received: ");
  Serial.println(a);
  Serial.print("OPEN: Data == ");
  Serial.println((char *)iobuf);
  for(int i=0;i<a;i++) {
    Serial.println(iobuf[i], HEX);
  }
}

void handle_atn() {
  uchar device,sec,sec_addr;
  uchar mode,sec_mode;

  wait_clock(HIGH);
  wait_clock(LOW);
  if(!get_byte(&device, 1)) return;
  mode = device & IEC_COMMAND;
  if(mode == IEC_UNLISTEN) {
    wait_atn(HIGH);
    return;
  } else if(mode == IEC_UNTALK) {
    wait_atn(HIGH);
    return;
  } else if(mode == IEC_LISTEN) {
    if(DEVNO != (device & IEC_DEVICE)) {
      /* Not our device */
      release_clock();
      release_data();
      Serial.println("LISTEN: Not our device");
      return;
    }
  } else if(mode == IEC_TALK) {
    if(DEVNO != (device & IEC_DEVICE)) {
      /* Not our device */
      release_clock();
      release_data();
      Serial.println("TALK: Not our device");
      return;
    }
  }

  delayMicroseconds(200);
  if(!get_byte(&sec, 1)) return;
  print_str("Device: ");
  println_hex(device);
  print_str("Mode: ");
  println_hex(mode);
  print_str("Secondary: ");
  println_hex(sec);

  sec_mode = sec & IEC_MODE;
  if(sec_mode == IEC_OPEN) {
    wait_atn(HIGH);
    sec_addr = sec & IEC_ADDRESS;
    handle_open(sec_addr, iobuf, sizeof(iobuf));
  } else if(sec_mode == IEC_CLOSE) {
    sec_addr = sec & IEC_ADDRESS;
    handle_close(sec_addr);
  } else if(sec_mode == IEC_DATA) {
    sec_addr = sec & IEC_ADDRESS;
    if(mode == IEC_LISTEN) {
      wait_atn(HIGH);
      // handle_listen();
    } else if(mode == IEC_TALK) {
      release_clock();
      release_data();
      wait_atn(HIGH);
      delayMicroseconds(200);
      release_data();
      pull_clock();
      delayMicroseconds(200);
      // handle_talk();
      release_data();
      release_clock();
    }
  } else if(sec_mode == IEC_FASTLOAD) {
    sec_addr = sec & IEC_ADDRESS;
    // handle_fastload();
  }
}

void setup() {
  pinMode(SRQIN, OUTPUT);
  pinMode(ATN, INPUT);
  pinMode(CLOCK, INPUT);
  pinMode(DATA, INPUT);
  pinMode(AVR_EOI, OUTPUT);
  pinMode(AVR_DEBUG, OUTPUT);
  pinMode(AVR_TIMEOUT, OUTPUT);

  clear_iobuf();

  //  digitalWrite(AVR_DEBUG, HIGH);
  pull_clock();
  digitalWrite(CLOCK, HIGH);
  release_clock();
  pull_data();
  //  digitalWrite(AVR_DEBUG, LOW);
  //  digitalWrite(AVR_TIMEOUT, LOW);
  
  Serial.begin(9600);
}

void loop() {
  if(atn_active()) {
    release_clock();
    pull_data();
    handle_atn();
  } else {
    delayMicroseconds(100);
  }
}
