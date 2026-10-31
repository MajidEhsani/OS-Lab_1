// Shell.

#include "types.h"
#include "user.h"
#include "fcntl.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit();

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit();
    exec(ecmd->argv[0], ecmd->argv);
    printf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      printf(2, "open %s failed\n", rcmd->file);
      exit();
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait();
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait();
    wait();
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit();
}

int
getcmd(char *buf, int nbuf)
{
  printf(2, "$ ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

#define LINELEN 256
#define HISTSIZE 10
#define MAXALIAS 16
#define NAMELEN 16

// Alias table
struct alias {
  int used;
  char name[NAMELEN];
  char cmd[LINELEN];
};
static struct alias aliases[MAXALIAS];

// History: command number n is kept in hist[n % HISTSIZE]
static char hist[HISTSIZE][LINELEN];
static int histnext = 1;  // number of the next command

static int
isspace(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v';
}

static int
startswith(char *s, char *prefix)
{
  while(*prefix){
    if(*s != *prefix)
      return 0;
    s++;
    prefix++;
  }
  return 1;
}

// Do the first n chars of a and b match?
static int
samename(char *a, char *b, int n)
{
  int i;

  for(i = 0; i < n; i++)
    if(a[i] != b[i])
      return 0;
  return 1;
}

// Is the first word of s equal to word?
static int
firstword(char *s, char *word)
{
  int n = strlen(word);

  while(isspace(*s))
    s++;
  return startswith(s, word) && (s[n] == 0 || isspace(s[n]));
}

static int
firstfree(int first)
{
  return first < 1 ? 1 : first;
}

static void
addhistory(char *cmd)
{
  strcpy(hist[histnext % HISTSIZE], cmd);
  histnext++;
}

static void
printhistory(void)
{
  int n;

  for(n = firstfree(histnext - HISTSIZE); n < histnext; n++)
    printf(1, "%d. %s\n", n, hist[n % HISTSIZE]);
}

// Replace !!, !n or !prefix at the start of buf with the command
// from history. Returns -1 if there is no such command.
int
histexpand(char *buf)
{
  char tmp[LINELEN], tok[LINELEN];
  char *p, *cmd, *rest;
  int i, n, num, found;

  p = buf;
  while(isspace(*p))
    p++;
  if(*p != '!')
    return 0;

  // tok is the text after '!', rest is what comes after it
  p++;
  i = 0;
  if(*p == '!'){
    tok[i++] = *p++;
  } else {
    while(*p && !isspace(*p))
      tok[i++] = *p++;
  }
  tok[i] = 0;
  rest = p;

  found = 0;
  cmd = 0;
  n = firstfree(histnext - HISTSIZE);
  if(strcmp(tok, "!") == 0){
    if(histnext > n){
      cmd = hist[(histnext-1) % HISTSIZE];
      found = 1;
    }
  } else if(tok[0] >= '0' && tok[0] <= '9'){
    num = atoi(tok);
    if(num >= n && num < histnext){
      cmd = hist[num % HISTSIZE];
      found = 1;
    }
  } else if(tok[0] != 0){
    for(num = histnext-1; num >= n; num--){
      if(startswith(hist[num % HISTSIZE], tok)){
        cmd = hist[num % HISTSIZE];
        found = 1;
        break;
      }
    }
  }

  if(!found){
    printf(2, "!%s: event not found\n", tok);
    return -1;
  }
  if(strlen(cmd) + strlen(rest) >= LINELEN){
    printf(2, "command too long\n");
    return -1;
  }
  strcpy(tmp, cmd);
  strcpy(tmp + strlen(cmd), rest);
  strcpy(buf, tmp);
  printf(1, "%s\n", buf);  // show the final command
  return 0;
}

// alias name=command
void
setalias(char *buf)
{
  char *p, *eq;
  int i, len, slot;

  p = buf;
  while(isspace(*p))
    p++;
  p += 5;  // skip "alias"
  while(isspace(*p))
    p++;

  if(*p == 0){  // no arguments: list aliases
    for(i = 0; i < MAXALIAS; i++)
      if(aliases[i].used)
        printf(1, "alias %s=%s\n", aliases[i].name, aliases[i].cmd);
    return;
  }

  eq = strchr(p, '=');
  len = eq ? eq - p : 0;
  if(eq == 0 || len == 0 || len >= NAMELEN || eq[1] == 0){
    printf(2, "usage: alias <name>=<command>\n");
    return;
  }

  // Use the old slot if the name already exists
  slot = -1;
  for(i = 0; i < MAXALIAS; i++){
    if(aliases[i].used && strlen(aliases[i].name) == len &&
       samename(aliases[i].name, p, len)){
      slot = i;
      break;
    }
  }
  if(slot < 0){
    for(i = 0; i < MAXALIAS; i++){
      if(!aliases[i].used){
        slot = i;
        break;
      }
    }
  }
  if(slot < 0){
    printf(2, "alias: too many aliases\n");
    return;
  }
  if(strlen(eq+1) >= LINELEN){
    printf(2, "alias: command too long\n");
    return;
  }

  aliases[slot].used = 1;
  memmove(aliases[slot].name, p, len);
  aliases[slot].name[len] = 0;
  strcpy(aliases[slot].cmd, eq+1);
}

// Replace the first word of buf if it is an alias (only once)
void
expandalias(char *buf)
{
  char tmp[LINELEN];
  char *p;
  int i, len;

  p = buf;
  while(isspace(*p))
    p++;
  len = 0;
  while(p[len] && !isspace(p[len]))
    len++;

  for(i = 0; i < MAXALIAS; i++){
    if(!aliases[i].used || strlen(aliases[i].name) != len ||
       !samename(aliases[i].name, p, len))
      continue;
    if(strlen(aliases[i].cmd) + strlen(p+len) >= LINELEN){
      printf(2, "alias: command too long\n");
      return;
    }
    strcpy(tmp, aliases[i].cmd);
    strcpy(tmp + strlen(tmp), p+len);
    strcpy(buf, tmp);
    return;
  }
}

int
main(void)
{
  static char buf[LINELEN];
  int fd, i;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    // Remove the \n at the end
    buf[strlen(buf)-1] = 0;
    for(i = 0; isspace(buf[i]); i++)
      ;
    if(buf[i] == 0)  // empty line
      continue;

    if(histexpand(buf) < 0)
      continue;
    addhistory(buf);

    if(firstword(buf, "alias")){
      setalias(buf);
      continue;
    }
    expandalias(buf);

    if(firstword(buf, "history")){
      printhistory();
      continue;
    }
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      if(chdir(buf+3) < 0)
        printf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait();
  }
  exit();
}

void
panic(char *s)
{
  printf(2, "%s\n", s);
  exit();
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    printf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
