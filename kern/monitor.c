// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/types.h>
#include <inc/mmu.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/env.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
  { "smps", "DIsplay information between vitual and physical memory", mon_showmappings},
  { "bt", "Backtrace from the current task", mon_backtrace},
  { "stp", "Set permissions of vitual address",mon_setpermissions},
  { "clr", "Clear permissions of vitual address",mon_clearpermissions},
  { "continue", "Continue task interrupted by monitor", mon_continue},
  { "si", "Continue by one step", mon_mystepi},
};

/***** Implementations of basic kernel monitor commands *****/

#define IS_HEX(s) (s[0]=='0'&&s[1]=='x')

#define PDE(pgdir,va) pgdir[PDX(va)]
#define PTE_PT(pgdir,va) (pte_t *)(KADDR(PTE_ADDR(PDE(pgdir,va)))+PTX(va))
#define PTE(pgdir,va) (*PTE_PT(pgdir,va))
#define P_PDE(pgdir,va) PDE(pgdir,va)|PTE_P
#define P_PTE(pgdir,va) PTE(pgdir,va)|PTE_P
#define PERM(pgdir,va) PTE(pgdir,va)&0x1FF

static inline void
num2binstr(uint32_t perm, char *s, size_t num) {
  while(num--) {
    s[num]=perm&0x1 ? '1' : '-';
    perm>>=1;
  } 
}

static inline int
str2num(char *s, int scale){
  int num = 0;
  if(scale == 16) {
    s += 2;
    while((*s<='9'&&*s>='0')||(*s<='f'&&*s>='a')) {
      num *= 16;
      num += *s<='9' ? *s-'0' : *s-'a'+10;
      s++;
    }
  }
  else if(scale == 10) {
    while(*s>='0'&&*s<='9') {
      num *= 10;
      num += *s-'0';
      s++;
    }
  }
  cprintf("%d",num);
  return *s ? -1 : num;
} 

static inline int 
char2perm(char c) {
  c = (c >= 'a' ? c-'a'+'A' : c); 
  switch(c){
    case 'G': return PTE_G;
	  case 'D': return PTE_D;
  	case 'A': return PTE_A;
  	case 'C': return PTE_PCD;
  	case 'T': return PTE_PWT;
  	case 'U': return PTE_U;
  	case 'W': return PTE_W;
  	case 'P': return PTE_P;
  	default: return -1;
  }
}  

static inline int 
str2perm(char *s){
  int num = 0;
  while(*s) {
    num |= char2perm(*s++); 
  } 
  return num;
}

static inline void 
validate_and_retrieve(int argc, char**argv, uint32_t *va_start, uint32_t *n_pages, char *hint) {
  if(argc<2) panic(hint);

  if(!IS_HEX(argv[1])) panic(hint);
  *va_start = ROUNDDOWN(str2num(argv[1],16),PGSIZE);
  cprintf("va_start1: %d",*va_start);
  if(argc == 2) *n_pages = 1;
  else if(IS_HEX(argv[2])) {
    *n_pages = (ROUNDUP(str2num(argv[2],16),PGSIZE) - *va_start)/PGSIZE;
  }
  else{
    *n_pages = str2num(argv[2],10);
  }
  if(*va_start<0||*n_pages<0) panic(hint);
  cprintf("va_start:%d_______n_pages:%d\n", *va_start, *n_pages);
}

static inline void
change_permissions(int argc, char **argv, int flags, char *hint) {
  uintptr_t va_start;
  uint32_t n_pages;
  validate_and_retrieve(argc-1, argv, &va_start, &n_pages, hint);
  int perm = str2perm(argv[argc-1]);
  if(perm < 0) panic("false permissions!\n");
  pte_t *pte = NULL;
  uintptr_t va;
  for(int cnt=0; cnt < n_pages; cnt++) {
      va = va_start + PGSIZE*cnt;
      if(P_PDE(kern_pgdir,va)&&P_PTE(kern_pgdir,va)) {
        pte = PTE_PT(kern_pgdir,va);
        if(*pte&PTE_P) {
          *pte = flags? *pte|perm : *pte^perm;
          *pte |= PTE_P;
        }
      }
  }
}

int 
mon_showmappings(int argc, char **argv, struct Trapframe *tf) {
  char hint[]="\nPlease pass arguments in correct formats, for example:\n"
                "  smps 0x3000 0x5000 ---show the mapping from va=0x3000 to va=0x5000\n"
                "  smps 0x3000 100 ---show the mapping of 100 virtual pages from va=0x3000\n"
                "  smps 0x3000 ---show the mapping of va=0x3000 only\n";
  uintptr_t va_start;
  uint32_t n_pages;
  validate_and_retrieve(argc, argv, &va_start, &n_pages, hint);
  cprintf(
        "G: global   I: page table attribute index D: dirty\n"
        "A: accessed C: cache disable              T: write through\n"
        "U: user     W: writeable                  P: present\n"
        "---------------------------------\n"
        "virtual_ad  physica_ad  GIDACTUWP\n");
  uintptr_t va;
  int cnt;
  extern pde_t *kern_pgdir;
  for(cnt = 0; cnt < n_pages; cnt++) {
    va = va_start + cnt*PGSIZE;
    if(P_PDE(kern_pgdir,va) && P_PTE(kern_pgdir, va)) {
      char permission[10];
      permission[9] = '\0';
      num2binstr(PERM(kern_pgdir, va), permission, 9);
      cprintf("0x%08x  0x%08x  %s\n",va,PTE_ADDR(PTE(kern_pgdir,va)),permission);
      continue;
    }
    cprintf("0x%08x  ----------  ---------\n",va);	
  }
  return 0;
}

int
mon_setpermissions(int argc, char **argv, struct Trapframe *tf){
char hint[]="\nPlease pass arguments in correct formats, for example:\n"
                "  stp 0x3000 0x5000 AD ---set permission bit A and D from va=0x3000 to va=0x5000\n"
                "  stp 0x3000 100 AD---set permission bit A and D of 100 virtual pages from va=0x3000\n"
                "  stp 0x3000 AD---set permission bit A and D of va=0x3000 only\n"
                "\n"
                "G: global   I: page table attribute index D: dirty\n"
                "A: accessed C: cache disable T: write through\n"
                "U: user     W: writeable     P: present\n"
                "\n"
                "ps: P is forbbiden to set by hand\n";
  change_permissions(argc, argv, 1, hint);
  cprintf("Permissions changed already!\n");
  mon_showmappings(argc-1, argv, tf);
  return 0;
}

int
mon_clearpermissions(int argc, char **argv, struct Trapframe *tf){
char hint[]="\nPlease pass arguments in correct formats, for example:\n"
                "  clr 0x3000 0x5000 AD ---set permission bit A and D from va=0x3000 to va=0x5000\n"
                "  clr 0x3000 100 AD---set permission bit A and D of 100 virtual pages from va=0x3000\n"
                "  clr 0x3000 AD---set permission bit A and D of va=0x3000 only\n"
                "\n"
                "G: global   I: page table attribute index D: dirty\n"
                "A: accessed C: cache disable T: write through\n"
                "U: user     W: writeable     P: present\n"
                "\n"
                "ps: P is forbbiden to set by hand\n";
  change_permissions(argc, argv, 0, hint);
  cprintf("Permissions changed already!\n");
  mon_showmappings(argc-1, argv, tf);
  return 0;
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
  uint32_t *bp = (uint32_t *)read_ebp();
  struct Eipdebuginfo info;
  cprintf("Stack backtrace:\n");
  while(bp != NULL) {
    cprintf("ebp %8x  eip %8x  args %08x %08x %08x %08x %08x\n", bp,  bp[1], bp[2], bp[3], bp[4], bp[5], bp[6]);
    debuginfo_eip(bp[1], &info);
    cprintf("\t%s:%d: %.*s+%d\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, bp[1] - info.eip_fn_addr);
    bp = (uint32_t *)(*bp);
  }
  cprintf("backtrace end!\n");
	return 0;
}

int 
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
  if(tf && (tf->tf_trapno == T_BRKPT || tf->tf_trapno == T_DEBUG)) {
    assert(curenv && curenv->env_status == ENV_RUNNING);
    curenv->env_tf.tf_eflags &= (~FL_TF);
    env_run(curenv);
  } 
  return 0;
}

int
mon_mystepi(int argc, char **argv, struct Trapframe *tf)
{
  assert(tf);
  curenv->env_tf.tf_eflags |= FL_TF;
  assert(curenv && curenv->env_status == ENV_RUNNING);
  cprintf("eip in %08x\n", curenv->env_tf.tf_eip);
  env_run(curenv);
  return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;
  static int welcomeflag = 1;
	if(welcomeflag) {
    welcomeflag--;
    cprintf("Welcome to the JOS kernel monitor!\n");
	  cprintf("Type 'help' for a list of commands.\n");
  }
  
	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

