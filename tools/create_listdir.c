#include <stdio.h>
#include <string.h>
#include <dirent.h>

char *valid_extensions[] = {
  "p00", "s00", "u00", "r00", NULL
};

char *file_extension(char *filename) {
  char *ext_pos;
  ext_pos = rindex(filename, '.');
  if(ext_pos) ext_pos++;
  return ext_pos;
}

int extension_valid(char *extension) {
  int i;
  if(!extension) return 0;
  for(i=0;valid_extensions[i];i++) {
    if(!strcmp(valid_extensions[i], extension)) return 1;
  }
  return 0;
}

#define putword(value) do { putchar(value); putchar(value>>8); } while(0);
#define TITLE "DIRECTORY"
#define BLKFREE "BLOCKS FREE."

void output_dir_header(int *addr) {
  int i;
  int len;

  /* Dummy PC64-header */
  for(i=0;i<26;i++) putchar(0);
  putword(*addr);
  len = strlen(TITLE);
  *addr += len+5;
  putword(*addr);
  putword(0);
  putchar(0x12);
  putchar('"');
  for(i=0;i<len;i++) {
    putchar(TITLE[i]);
  }
  putchar('"');
  putchar(0);
}

void output_dir_entry(struct dirent *de, int *addr) {
  FILE *f;
  int size;
  int i;
  int b = 1;
  char c64name[16];
  char *last_pos;
  int len;
  
  if(de->d_type != DT_REG) return;
  if(!extension_valid(file_extension(de->d_name))) return;
  f = fopen(de->d_name, "r");
  fseek(f, 0, SEEK_END);
  size = (ftell(f)-26)/256;
  fseek(f, 8, SEEK_SET);
  
  fread(c64name, 15, 1, f);
  c64name[15] = '\0';
  last_pos = index(c64name, 0xa0);
  if(last_pos) *last_pos = '\0';

  if(size < 100) b++;
  if(size < 10) b++;
  len = strlen(c64name);
  *addr += 26+b;
  putword(*addr);
  putword(size);
  for(i=0;i<b;i++) putchar(' ');
  putchar('"');
  for(i=0;i<len;i++) putchar(c64name[i]);
  putchar('"');
  for(i=0;i<20-len;i++) putchar(' ');
  if(file_extension(de->d_name) && file_extension(de->d_name)[0] == 'p') {
    putchar(' '); putchar('P'); putchar('R'); putchar('G'); putchar(' ');
  } else {
    putchar(' '); putchar('U'); putchar('K'); putchar('N'); putchar(' ');
  }
  putchar(0);

  fclose(f);
}

void output_dir_footer(int *addr) {
  int i;
  
  putword(*addr);
  putword(0xffff);
  for(i=0;i<strlen(BLKFREE);i++) {
    putchar(BLKFREE[i]);
  }
  putword(0);
  putchar(0);
}

int main(int argc, char *argv[]) {
  DIR *d;
  struct dirent *de;
  int addr = 0x0801;

  d = opendir(".");
  output_dir_header(&addr);
  while(de = readdir(d)) {
    output_dir_entry(de, &addr);
  }
  output_dir_footer(&addr);

  return 0;
}
