
/* references:
 * https://www.cnblogs.com/mmmmar/p/8227915.html
 * https://github.com/scottt/debugbreaks
/**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ucontext.h>

/*
ARM:
https://github.com/torvalds/linux/blob/master/arch/arm/include/uapi/asm/sigcontext.h
struct sigcontext {
	size_t trap_no;
	size_t error_code;
	size_t oldmask;
	size_t arm_r0;
	size_t arm_r1;
	size_t arm_r2;
	size_t arm_r3;
	size_t arm_r4;
	size_t arm_r5;
	size_t arm_r6;
	size_t arm_r7;
	size_t arm_r8;
	size_t arm_r9;
	size_t arm_r10;
	size_t arm_fp;
	size_t arm_ip;
	size_t arm_sp;
	size_t arm_lr;
	size_t arm_pc;
	size_t arm_cpsr;
	size_t fault_address;
};

ARM64:
https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/sigcontext.h
struct sigcontext {
	__u64 fault_address;
	__u64 regs[31];
	__u64 sp;
	__u64 pc;
	__u64 pstate;
	__u8 __reserved[4096] __attribute__((__aligned__(16)));
};

x64 and x64:
https://github.com/torvalds/linux/blob/master/arch/x86/include/uapi/asm/sigcontext.h
// ignore
**/

// #define ARM
// #define ARM64

#if defined(ARM)
	unsigned char brk_ins[4] = {
		0xf0, 0x01, 0xf0, 0xe7
	};
	typedef struct SigContext {
		size_t trap_no;
		size_t error_code;
		size_t oldmask;
		size_t regs[11];
		size_t fp;
		size_t ip;
		size_t sp;
		size_t lr;
		size_t pc;
		size_t cpsr;
		size_t fault_address;
	} SigContext;
#elif defined(ARM64) || defined(AARCH64)
	unsigned char brk_ins[4] = {
		0x00, 0x00, 0x20, 0xd4
	};
	typedef struct SigContext {
		size_t fault_address;
		size_t regs[31];
		size_t sp;
		size_t pc;
		size_t pstate;
	} SigContext;
#elif defined(X64)
	unsigned char brk_ins[1] = {
		0xcc
	};
	typedef struct SigContext {
		size_t r8;
		size_t r9;
		size_t r10;
		size_t r11;
		size_t r12;
		size_t r13;
		size_t r14;
		size_t r15;
		size_t rdi;
		size_t rsi;
		size_t rbp;
		size_t rbx;
		size_t rdx;
		size_t rax;
		size_t rcx;
		size_t rsp;
		union { size_t pc; size_t rip; }; // size_t rip;
		size_t eflags; /* RFLAGS */
		unsigned short cs, gs, fs, ss;
		// size_t err, trapno, oldmask, cr2; struct _fpstate* fpstate; // ...
	} SigContext;
	#define TRAP_FLAG (0x100)
#elif defined(X86)
	unsigned char brk_ins[1] = {
		0xcc
	};
	typedef struct SigContext {
		unsigned short gs, __gsh;
		unsigned short fs, __fsh;
		unsigned short es, __esh;
		unsigned short ds, __dsh;
		size_t edi;
		size_t esi;
		size_t ebp;
		size_t esp;
		size_t ebx;
		size_t edx;
		size_t ecx;
		size_t eax;
		size_t trapno;
		size_t err;
		union { size_t pc; size_t eip; }; // size_t eip;
		unsigned short cs, __csh;
		size_t eflags;
		size_t esp_at_signal;
		unsigned short ss, __ssh;
		// /* struct _fpstate __user */ void *fpstate;
		// size_t oldmask;
		// size_t cr2;
	} SigContext;
	#define TRAP_FLAG (0x100)
#else
	#error "invalid arch"
#endif

typedef struct BPList {
	struct BPList* next;
	void* address;
	void (*handler)(const SigContext* ctx);
	int ins_size;
	unsigned char saved_ins[sizeof(brk_ins)];
} BPList;

static BPList bplist_header = { NULL };

void nop(const SigContext* ctx) {
	// empty implementation
}

void sigtrap_handler(int signum, siginfo_t* siginfo, void* context) {
	ucontext_t* uc = context;
	SigContext* ctx = (void*) &uc->uc_mcontext;

	void* pc = (void*) ctx->pc;
	// printf("SIGTRAP: %p\n", pc);
	// getchar();

	#if defined(TRAP_FLAG) // x86 || x64
		static BPList* lastbp = NULL;
		if (lastbp) {
			assert((ctx->eflags & TRAP_FLAG) == TRAP_FLAG);
			ctx->eflags &= ~TRAP_FLAG;
			memcpy(lastbp->address, brk_ins, sizeof(brk_ins)); // set break
			lastbp = NULL;
			return;
		}
		// in x86/x64, saved_pc = pc + ins_size
		// in arm/arm64, saved_pc = pc
		pc = (void*) ((size_t) pc - sizeof(brk_ins));
		ctx->pc = (size_t) pc;
	#endif

	int handler_found = 0;
	struct BPList* bp = bplist_header.next;
	while (bp) {
		if (bp->address == pc) {
			handler_found = 1;
			// restore original ins
			memcpy(bp->address, bp->saved_ins, sizeof(brk_ins));
			bp->handler(ctx);

			#if defined(TRAP_FLAG) // x86 || x64
				ctx->eflags |= TRAP_FLAG;
				lastbp = bp;
			#else // arm and aarch6
				// breakpoint ins saved positive ins_size
				// and next ins saved negative ins_size
				memcpy((void*) ((size_t) bp->address + bp->ins_size), brk_ins, sizeof(brk_ins)); // set break
			#endif

			break; // only one handler permitted
		}
		bp = bp->next;
	}
	if (!handler_found) {
		printf("Undefined breakpoint at %p.\n", pc);
	}
}

void sigtrap_handler_setup() {
	struct sigaction sig;
	sigemptyset(&sig.sa_mask);
	sig.sa_flags = SA_SIGINFO;
	sig.sa_sigaction = sigtrap_handler;
	assert(!sigaction(SIGTRAP, &sig, NULL));
}

void breakpoint(void* address, /* int ins_size, */ void (*handler)(const SigContext* ctx)) {
	int ins_size = sizeof(brk_ins); // replace with argument if cisc
	assert(sizeof(brk_ins) <= ins_size);
	static int inited = 0;
	if (!inited) {
		inited = 1;
		sigtrap_handler_setup();
	}
	BPList* iter = bplist_header.next;
	while (iter) {
		if (iter->address == address) { // including breakpoint and breakpoint's next
			printf("Breakpoint %p already has a handler: %p, ignoring new handler %p.\n", iter->address, iter->handler, handler);
			return;
		}
		iter = iter->next;
	}

	assert(!mprotect((void*) ((size_t) address & ~0xfff), 0x1000, 7));
	#if !defined(TRAP_FLAG) // ARM || AARCH64
		assert(!mprotect((void*) (((size_t) address + ins_size) & ~0xfff), 0x1000, 7));
	#endif

	BPList* bp = (BPList*) malloc(sizeof(BPList));
	bp->address = address;
	bp->handler = handler;
	bp->next = bplist_header.next;
	bp->ins_size = ins_size;
	bplist_header.next = bp;
	memcpy(bp->saved_ins, bp->address, sizeof(brk_ins)); // save original ins

	memcpy(address, brk_ins, sizeof(brk_ins)); // set break

	#if !defined(TRAP_FLAG) // ARM || AARCH64
		BPList* nextbp = (BPList*) malloc(sizeof(BPList));
		nextbp->address = (void*) ((size_t) address + ins_size);
		nextbp->handler = nop;
		nextbp->next = bplist_header.next;
		nextbp->ins_size = -ins_size; // negative
		bplist_header.next = nextbp;
		memcpy(nextbp->saved_ins, nextbp->address, sizeof(brk_ins)); // save original ins
	#endif
}

size_t get_1st_arg(const SigContext* ctx) {
	#if !defined(TRAP_FLAG)
		return ctx->regs[0];
	#elif defined(X64)
		return ctx->rdi;
	#elif defined(X86)
		return ((size_t*) ctx->esp) [1];
	#else
		// #error "no arch specified"
		return 0;
	#endif
}

size_t get_2nd_arg(const SigContext* ctx) {
	#if !defined(TRAP_FLAG)
		return ctx->regs[1];
	#elif defined(X64)
		return ctx->rsi;
	#elif defined(X86)
		return ((size_t*) ctx->esp) [2];
	#else
		// #error "no arch specified"
		return 0;
	#endif
}

int pow2(int x) {
	return x * x;
}

void pow2_hook(const SigContext* ctx) {
	int x = get_1st_arg(ctx);
	printf("Called pow2(%d)\n", x);
	// remove "const" of ctx to edit.
	// ctx->regs[0] = x + 1;
}

int test(int a, int b) {
	return pow2(a) + pow2(b);
}

void test_hook(const SigContext* ctx) {
	int a = get_1st_arg(ctx);
	int b = get_2nd_arg(ctx);
	printf("Called test(%d, %d)\n", a, b);
}

// void breakpoint(void* address, void (*handler)(const SigContext* ctx))
int main() {
	breakpoint(test, test_hook);
	breakpoint(pow2, pow2_hook);
	// puts("begin");
	printf("%d\n", test(1, 2));
	printf("%d\n", test(3, 4));
	printf("%d\n", test(7, 5));
	// puts("end");
	return 0;
}

/*
gcc ./main.c -o main -g -D X64

gcc -m32 ./main.c -o main -g -D X86

arm-linux-gnueabi-gcc ./main.c -o main -g -D ARM
qemu-arm -L /usr/arm-linux-gnueabi/ ./main

aarch64-linux-gnu-gcc ./main.c -o main -g -D ARM64
qemu-aarch64 -L /usr/aarch64-linux-gnu/ ./main
**/