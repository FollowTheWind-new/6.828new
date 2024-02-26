// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	// LAB 4: Your code here.
  if(!(err & FEC_WR))
    panic("pgfault :not a fixable fault\n");
    
  addr = (void *)ROUNDDOWN(addr, PGSIZE);
  if(!(uvpt[PGNUM(addr)] & PTE_COW) || !(uvpt[PGNUM(addr)] & PTE_U))
    panic("pgfault :illegal page privilege!\n");

  envid_t envid = sys_getenvid();
  if(sys_page_alloc(envid, (void *)PFTEMP, PTE_W|PTE_U|PTE_P) < 0)
    panic("pgfault: page_alloc error!\n");

  memcpy((void *)PFTEMP, addr, PGSIZE);
  
  if(sys_page_map(envid, (void *)PFTEMP, envid, addr, PTE_W|PTE_U|PTE_P) < 0)
    panic("pgfault: page_map error!\n");
  if(sys_page_unmap(envid, PFTEMP) < 0)
    panic("pgfault: page_unmap error!\n");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
  int r;
  envid_t thisid = thisenv->env_id; 
  if(!(uvpt[pn] & PTE_P)) 
    return -E_INVAL;
  void *addr = (void *)(pn*PGSIZE);
  // is this true? I can't give a quite answer.
  if(uvpt[pn] & PTE_SHARE) {
    if((r = sys_page_map(thisid, addr, envid, addr, uvpt[pn]&PTE_SYSCALL)) < 0) 
      return r;
    
  }
  else if(uvpt[pn] & (PTE_W|PTE_COW)) {
    if((r = sys_page_map(thisid, addr, envid, addr, PTE_U|PTE_P|PTE_COW)) < 0)
      return r;
    if((r = sys_page_map(thisid, addr, thisid, addr, PTE_U|PTE_P|PTE_COW)) < 0)
      return r;
  }
  else {
    if((r = sys_page_map(thisid, addr, envid, addr, PTE_U|PTE_P)) < 0)
      return r;
  }
	// LAB 4: Your code here.
	// panic("duppage not implemented");
	return 0;
}



//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
  set_pgfault_handler(pgfault);
  extern void  _pgfault_upcall(void);
  envid_t id = sys_exofork();
  if(id < 0)
    return id;
  if(id > 0) {
    // it's parent here
    for(uint32_t addr = 0; addr < USTACKTOP; addr += PGSIZE) {
      if((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_U))
        duppage(id, PGNUM(addr));
    }
    int r;
    if((r = sys_page_alloc(id, (void *)UXSTACKTOP-PGSIZE, PTE_W|PTE_U|PTE_P)) < 0)
      panic("fork: %e child exception stk alloc fail!\n",r); 
    sys_env_set_pgfault_upcall(id, (void *)_pgfault_upcall);
    if((r = sys_env_set_status(id, ENV_RUNNABLE) < 0))
      panic("fork: %e child status setting error!\n");
    return id;
  }
  else  {
    thisenv = &envs[ENVX(sys_getenvid())];
    return 0;
  }
	// panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
