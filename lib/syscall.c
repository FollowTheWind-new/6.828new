// System call stubs.

#include <inc/syscall.h>
#include <inc/lib.h>
static int32_t
syscall_fast(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
    __attribute__((noinline));

static int32_t
syscall_fast(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
int32_t ret;
	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	// move arguments into registers before %ebp modified
	// as with static identifier, num, check and a1 are stroed
	// in %eax, %edx and %ecx respectively, while a3, a4 and a5
	// are addressed via 0xx(%ebp)
	asm volatile("movl %0,%%edx"::"S"(a1):"%edx");
	asm volatile("movl %0,%%ecx"::"S"(a2):"%ecx");
	asm volatile("movl %0,%%ebx"::"S"(a3):"%ebx");
	asm volatile("movl %0,%%edi"::"S"(a4):"%ebx");

	// save user space %esp in %ebp passed into sysenter_handler
	asm volatile("pushl %ebp");
	asm volatile("movl %esp,%ebp");

	// save user space %eip in %esi passed into sysenter_handler
	asm volatile("leal .syslabel,%%esi":::"%esi");
	asm volatile("sysenter \n\t"
				 ".syslabel:"
			:
			: "a" (num)
			: "memory");
	
	// retrieve return value
	asm volatile("movl %%eax,%0":"=r"(ret));
	
	// restore %ebp
	asm volatile("popl %ebp");
	
	if(check && ret > 0)
		panic("syscall %d returned %d (> 0)", num, ret);
	return ret;
}

static inline int32_t
syscall(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t ret;

	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	asm volatile("int %1\n"
		     : "=a" (ret)
		     : "i" (T_SYSCALL),
		       "a" (num),
		       "d" (a1),
		       "c" (a2),
		       "b" (a3),
		       "D" (a4),
		       "S" (a5)
		     : "cc", "memory");

	if(check && ret > 0)
		panic("syscall %d returned %d (> 0)", num, ret);

	return ret;
}
void
sys_cputs(const char *s, size_t len)
{ 
	syscall_fast(SYS_cputs, 0, (uint32_t)s, len, 0, 0, 0);
}

int
sys_cgetc(void)
{
	return syscall(SYS_cgetc, 0, 0, 0, 0, 0, 0);
}

int
sys_env_destroy(envid_t envid)
{
	return syscall(SYS_env_destroy, 1, envid, 0, 0, 0, 0);
}

envid_t
sys_getenvid(void)
{
	 return syscall(SYS_getenvid, 0, 0, 0, 0, 0, 0);
}

