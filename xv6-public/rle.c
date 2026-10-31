#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define BUFSZ 512

char in[BUFSZ];
char out[BUFSZ];
int outn;   // number of bytes waiting in out
int outfd;  // output file

// Write the bytes waiting in out to the output file
void
flush(void)
{
  if(outn > 0 && write(outfd, out, outn) != outn){
    printf(1, "rle: write error\n");
    exit();
  }
  outn = 0;
}

void
put(char c)
{
  if(outn == BUFSZ)
    flush();
  out[outn++] = c;
}



// Write one run as "c,count;"
void
putrun(char c, int count)
{
  char digits[12];
  int n = 0;

  put(c);
  put(',');
  while(count > 0){
    digits[n++] = '0' + count % 10;
    count /= 10;
  }
  while(n > 0)
    put(digits[--n]);
  put(';');
}

void
encode(int fd)
{
  int n, i, count = 0;
  char cur = 0;

  while((n = read(fd, in, BUFSZ)) > 0){
    for(i = 0; i < n; i++){
      if(count > 0 && in[i] == cur){
        count++;
      } else {
        if(count > 0)
          putrun(cur, count);
        cur = in[i];
        count = 1;
      }
    }
  }
  if(n < 0){
    printf(1, "rle: read error\n");
    exit();
  }
  if(count > 0)  // last run
    putrun(cur, count);
  flush();
}

void
decode(int fd)
{
  int n, i, k;
  int state = 0;  // 0: expect char, 1: expect ',', 2: reading count
  int count = 0, ndigits = 0;
  char ch = 0, c;

  while((n = read(fd, in, BUFSZ)) > 0){
    for(i = 0; i < n; i++){
      c = in[i];
      if(state == 0){
        ch = c;
        state = 1;
      } else if(state == 1){
        if(c != ','){
          printf(1, "rle: invalid encoded file\n");
          exit();
        }
        count = 0;
        ndigits = 0;
        state = 2;
      } else {
        if(c >= '0' && c <= '9'){
          count = count * 10 + (c - '0');
          ndigits++;
        } else if(c == ';' && ndigits > 0){
          for(k = 0; k < count; k++)
            put(ch);
          state = 0;
        } else {
          printf(1, "rle: invalid encoded file\n");
          exit();
        }
      }
    }
  }
  if(n < 0){
    printf(1, "rle: read error\n");
    exit();
  }
  if(state != 0){  // file ended in the middle of a run
    printf(1, "rle: invalid encoded file\n");
    exit();
  }
  flush();
}

int
main(int argc, char *argv[])
{
  int fd, enc;
  char *outname;

  if(argc != 3){
    printf(1, "usage: rle encode|decode file\n");
    exit();
  }
  if(strcmp(argv[1], "encode") == 0){
    enc = 1;
    outname = "encoded.txt";
  } else if(strcmp(argv[1], "decode") == 0){
    enc = 0;
    outname = "decoded.txt";
  } else {
    printf(1, "rle: invalid mode %s\n", argv[1]);
    exit();
  }

  if((fd = open(argv[2], O_RDONLY)) < 0){
    printf(1, "rle: cannot open %s\n", argv[2]);
    exit();
  }

  // xv6 has no O_TRUNC, so remove the old output file first
  unlink(outname);
  if((outfd = open(outname, O_CREATE | O_WRONLY)) < 0){
    printf(1, "rle: cannot create %s\n", outname);
    exit();
  }

  if(enc)
    encode(fd);
  else
    decode(fd);

  close(fd);
  close(outfd);
  exit();
}
