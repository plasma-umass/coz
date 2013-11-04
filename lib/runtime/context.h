#if !defined(CAUSAL_LIB_RUNTIME_CONTEXT_H)
#define CAUSAL_LIB_RUNTIME_CONTEXT_H

#if !defined(_XOPEN_SOURCE)
// Digging inside of ucontext_t is deprecated unless this macros is defined
#define _XOPEN_SOURCE
#endif

#include <cstdio>
#include <stdint.h>
#include <sys/mman.h>
#include <ucontext.h>

#include "arch.h"
#include "debug.h"

/// Bitfield struct for reading the eflags/rflags register
struct flagreg {
	bool CarryFlag : 1;
	bool R1 : 1;
	bool ParityFlag : 1;
	bool R2 : 1;
	bool AdjustFlag : 1;
	bool R3 : 1;
	bool ZeroFlag : 1;
	bool SignFlag : 1;
	bool TrapFlag : 1;
	bool InterruptEnableFlag : 1;
	bool DirectionFlag : 1;
	bool OverflowFlag : 1;
	uint8_t IOPL : 2;
	bool NestedTaskFlag : 1;
	bool R4 : 1;
	bool ResumeFlag : 1;
	bool Virtual8086Mode : 1;
	bool AlignmentCheck : 1;
	bool VirtualInterruptFlag : 1;
	bool VirtualInterruptPending : 1;
	bool CPUIDAvailable : 1;
	uint32_t R5 : 10;
};

struct DR7 {
	bool L0 : 1;
	bool G0 : 1;
	bool L1 : 1;
	bool G1 : 1;
	bool L2 : 1;
	bool G2 : 1;
	bool L3 : 1;
	bool G3 : 1;
	bool LE : 1;
	bool GE : 1;
	int reserved_1 : 3; // = 0b001
	bool GD : 1;
	int reserved_2 : 2; // = 0b00
	int RW0 : 2;
	int Len0 : 2;
	int RW1 : 2;
	int Len1 : 2;
	int RW2 : 2;
	int Len2 : 2;
	int RW3 : 2;
	int Len3 : 2;
	
	void init() {
		LE = true;
		GE = false;
		reserved_1 = 1;
		GD = false;
		reserved_2 = 0;
		RW0 = 0;
		Len0 = 0;
		RW1 = 0;
		Len1 = 0;
		RW2 = 0;
		Len2 = 0;
		RW3 = 0;
		Len3 = 0;
	}
};

enum Register : int {
	// 8 bit registers
	AL, CL, DL, BL, AH, CH, DH, BH, SPL, BPL, SIL, DIL,	R8B, R9B, R10B, R11B, R12B, R13B, R14B, R15B,
	// 16 bit registers
	AX, CX, DX, BX, SP, BP, SI, DI, R8W, R9W, R10W, R11W, R12W, R13W, R14W, R15W,
	// 32 bit registers
	EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, EIP, R8D, R9D, R10D, R11D, R12D, R13D, R14D, R15D,
	// 64 bit registers
	RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, RIP, R8, R9, R10, R11, R12, R13, R14, R15
};

/// Convenient wrappers for reading/modifying a ucontext_t
struct Context {
private:
	/// The actual signal context
	ucontext_t* _c;
	
public:
	Context(ucontext_t* c) : _c(c) {}
	Context(void* c) : _c((ucontext_t*)c) {}
	
	/// Get a mutable reference to a register encoded with libudis86
	template<typename T> inline T& reg(Register r) {
		return *reg_ptr<T>(r);
	}
	
	/// Get a reference to the context instruction pointer
	template<typename T> inline T& ip() {
		_X86(return reg<T>(Register::EIP));
		_X86_64(return reg<T>(Register::RIP));
	}
	
	/// Get a reference to the context stack pointer
	template<typename T> inline T& sp() {
		_X86(return reg<T>(Register::ESP));
		_X86_64(return reg<T>(Register::RSP));
	}
	
	/// Get a reference to the context frame pointer
	template<typename T> inline T& fp() {
		_X86(return reg<T>(Register::EBP));
		_X86_64(return reg<T>(Register::RBP));	
	}
	
	/// Get a mutable reference to the flags register wrapped in the flagreg bitfield defined above
	inline flagreg& flags() {
		_OSX(_X86(return *(flagreg*)&_c->uc_mcontext->__ss.__eflags));
		_OSX(_X86_64(return *(flagreg*)&_c->uc_mcontext->__ss.__rflags));
		// TODO: Linux flags register access
		assert(false && "Flags register is not supported on this target");
	}
	
private:
	/// Get a pointer to a registers state saved in this context
	template<typename T> inline T* reg_ptr(Register r) {
		if(sizeof(T) == 1) {
			// Access 8 bit registers
			switch(r) {
				case Register::AL:   return (T*)reg_ptr<uint32_t>(Register::EAX);
				case Register::CL:   return (T*)reg_ptr<uint32_t>(Register::ECX);
				case Register::DL:   return (T*)reg_ptr<uint32_t>(Register::EDX);
				case Register::BL:   return (T*)reg_ptr<uint32_t>(Register::EBX);
		  	case Register::AH:   return (&(reg_ptr<T>(Register::AL))[1]);
				case Register::CH:   return (&(reg_ptr<T>(Register::CL))[1]);
				case Register::DH:   return (&(reg_ptr<T>(Register::DL))[1]);
				case Register::BH:   return (&(reg_ptr<T>(Register::BL))[1]);
		  	case Register::SPL:  return (T*)reg_ptr<uint32_t>(Register::ESP);
				case Register::BPL:  return (T*)reg_ptr<uint32_t>(Register::EBP);
				case Register::SIL:  return (T*)reg_ptr<uint32_t>(Register::ESI);
				case Register::DIL:  return (T*)reg_ptr<uint32_t>(Register::EDI);
				case Register::R8B:  return (T*)reg_ptr<uint64_t>(Register::R8);
				case Register::R9B:  return (T*)reg_ptr<uint64_t>(Register::R9);
				case Register::R10B: return (T*)reg_ptr<uint64_t>(Register::R10);
				case Register::R11B: return (T*)reg_ptr<uint64_t>(Register::R11);
				case Register::R12B: return (T*)reg_ptr<uint64_t>(Register::R12);
				case Register::R13B: return (T*)reg_ptr<uint64_t>(Register::R13);
				case Register::R14B: return (T*)reg_ptr<uint64_t>(Register::R14);
				case Register::R15B: return (T*)reg_ptr<uint64_t>(Register::R15);
				default: assert(false && "Unknown 8 bit register");
			}
		} else if(sizeof(T) == 2) {
			// Access 16 bit registers
			switch(r) {
				case Register::AX:   return (T*)reg_ptr<uint32_t>(Register::EAX);
				case Register::CX:   return (T*)reg_ptr<uint32_t>(Register::ECX);
				case Register::DX:   return (T*)reg_ptr<uint32_t>(Register::EDX);
				case Register::BX:   return (T*)reg_ptr<uint32_t>(Register::EBX);
				case Register::SP:   return (T*)reg_ptr<uint32_t>(Register::ESP);
				case Register::BP:   return (T*)reg_ptr<uint32_t>(Register::EBP);
				case Register::SI:   return (T*)reg_ptr<uint32_t>(Register::ESI);
				case Register::DI:   return (T*)reg_ptr<uint32_t>(Register::EDI);
				case Register::R8W:  return (T*)reg_ptr<uint64_t>(Register::R8);
				case Register::R9W:  return (T*)reg_ptr<uint64_t>(Register::R9);
				case Register::R10W: return (T*)reg_ptr<uint64_t>(Register::R10);
				case Register::R11W: return (T*)reg_ptr<uint64_t>(Register::R11);
				case Register::R12W: return (T*)reg_ptr<uint64_t>(Register::R12);
				case Register::R13W: return (T*)reg_ptr<uint64_t>(Register::R13);
				case Register::R14W: return (T*)reg_ptr<uint64_t>(Register::R14);
				case Register::R15W: return (T*)reg_ptr<uint64_t>(Register::R15);
				default: assert(false && "Unknown 16 bit register");
			}
		} else if(sizeof(T) == 4) {
			// Access 32 bit registers
			_X86(
				// If built in 32 bit mode, pull register fields from the ucontext_t
				_OSX(
					// OSX-specific ucontext_t access
					switch(r) {
						case Register::EAX: return (T*)&_c->uc_mcontext->__ss.__eax;
						case Register::ECX: return (T*)&_c->uc_mcontext->__ss.__ecx;
						case Register::EDX: return (T*)&_c->uc_mcontext->__ss.__edx;
						case Register::EBX: return (T*)&_c->uc_mcontext->__ss.__ebx;
						case Register::ESP: return (T*)&_c->uc_mcontext->__ss.__esp;
						case Register::EBP: return (T*)&_c->uc_mcontext->__ss.__ebp;
						case Register::ESI: return (T*)&_c->uc_mcontext->__ss.__esi;
						case Register::EDI: return (T*)&_c->uc_mcontext->__ss.__edi;
						case Register::EIP: return (T*)&_c->uc_mcontext->__ss.__eip;
						default: assert(false && "Unknown 32 bit register");
					}
				)
				_LINUX(
					// Linux-specific ucontext_t access
					switch(R) {
						case Register::EAX: return (T*)&_c->uc_mcontext.gregs[REG_EAX];
						case Register::ECX: return (T*)&_c->uc_mcontext.gregs[REG_ECX];
						case Register::EDX: return (T*)&_c->uc_mcontext.gregs[REG_EDX];
						case Register::EBX: return (T*)&_c->uc_mcontext.gregs[REG_EBX];
						case Register::ESP: return (T*)&_c->uc_mcontext.gregs[REG_ESP];
						case Register::EBP: return (T*)&_c->uc_mcontext.gregs[REG_EBP];
						case Register::ESI: return (T*)&_c->uc_mcontext.gregs[REG_ESI];
						case Register::EDI: return (T*)&_c->uc_mcontext.gregs[REG_EDI];
						case Register::RIP: return (T*)&_c->uc_mcontext.gregs[REG_EIP];
						default: assert(false && "Unknown 32 bit register");
					}
				)
			)
			_X86_64(
				// If built in 64 bit mode, return a reference to the lower 32 bits of the
				// corresponding 64 bit register
				switch(r) {
					case Register::EAX:  return (T*)reg_ptr<uint64_t>(Register::RAX);
					case Register::ECX:  return (T*)reg_ptr<uint64_t>(Register::RCX);
					case Register::EDX:  return (T*)reg_ptr<uint64_t>(Register::RDX);
					case Register::EBX:  return (T*)reg_ptr<uint64_t>(Register::RBX);
					case Register::ESP:  return (T*)reg_ptr<uint64_t>(Register::RSP);
					case Register::EBP:  return (T*)reg_ptr<uint64_t>(Register::RBP);
					case Register::ESI:  return (T*)reg_ptr<uint64_t>(Register::RSI);
					case Register::EDI:  return (T*)reg_ptr<uint64_t>(Register::RDI);
				  case Register::R8D:  return (T*)reg_ptr<uint64_t>(Register::R8);
					case Register::R9D:  return (T*)reg_ptr<uint64_t>(Register::R9);
					case Register::R10D: return (T*)reg_ptr<uint64_t>(Register::R10);
					case Register::R11D: return (T*)reg_ptr<uint64_t>(Register::R11);
				  case Register::R12D: return (T*)reg_ptr<uint64_t>(Register::R12);
					case Register::R13D: return (T*)reg_ptr<uint64_t>(Register::R13);
					case Register::R14D: return (T*)reg_ptr<uint64_t>(Register::R14);
					case Register::R15D: return (T*)reg_ptr<uint64_t>(Register::R15);
					default: assert(false && "Unknown 32 bit register");
				}
			)
		} else if(sizeof(T) == 8) {
			_X86_64(
				// Read the ucontext_t if built in 64 bit mode
				_OSX(
					// OSX-specific ucontext_t access
					switch(r) {
						case Register::RAX: return (T*)&_c->uc_mcontext->__ss.__rax;
						case Register::RCX: return (T*)&_c->uc_mcontext->__ss.__rcx;
						case Register::RDX: return (T*)&_c->uc_mcontext->__ss.__rdx;
						case Register::RBX: return (T*)&_c->uc_mcontext->__ss.__rbx;
						case Register::RSP: return (T*)&_c->uc_mcontext->__ss.__rsp;
						case Register::RBP: return (T*)&_c->uc_mcontext->__ss.__rbp;
						case Register::RSI: return (T*)&_c->uc_mcontext->__ss.__rsi;
						case Register::RDI: return (T*)&_c->uc_mcontext->__ss.__rdi;
						case Register::R8:  return (T*)&_c->uc_mcontext->__ss.__r8;
						case Register::R9:  return (T*)&_c->uc_mcontext->__ss.__r9;
						case Register::R10: return (T*)&_c->uc_mcontext->__ss.__r10;
						case Register::R11: return (T*)&_c->uc_mcontext->__ss.__r11;
						case Register::R12: return (T*)&_c->uc_mcontext->__ss.__r12;
						case Register::R13: return (T*)&_c->uc_mcontext->__ss.__r13;
						case Register::R14: return (T*)&_c->uc_mcontext->__ss.__r14;
						case Register::R15: return (T*)&_c->uc_mcontext->__ss.__r15;
						case Register::RIP: return (T*)&_c->uc_mcontext->__ss.__rip;
						default: assert(false && "Unknown 64 bit register");
					}
				)
				_LINUX(
					// Linux-specific ucontext_t access
					switch(r) {
						case Register::RAX: return (T*)&_c->uc_mcontext.gregs[REG_RAX];
						case Register::RCX: return (T*)&_c->uc_mcontext.gregs[REG_RCX];
						case Register::RDX: return (T*)&_c->uc_mcontext.gregs[REG_RDX];
						case Register::RBX: return (T*)&_c->uc_mcontext.gregs[REG_RBX];
						case Register::RSP: return (T*)&_c->uc_mcontext.gregs[REG_RSP];
						case Register::RBP: return (T*)&_c->uc_mcontext.gregs[REG_RBP];
						case Register::RSI: return (T*)&_c->uc_mcontext.gregs[REG_RSI];
						case Register::RDI: return (T*)&_c->uc_mcontext.gregs[REG_RDI];
						case Register::R8:  return (T*)&_c->uc_mcontext.gregs[REG_R8];
						case Register::R9:  return (T*)&_c->uc_mcontext.gregs[REG_R9];
						case Register::R10: return (T*)&_c->uc_mcontext.gregs[REG_R10];
						case Register::R11: return (T*)&_c->uc_mcontext.gregs[REG_R11];
						case Register::R12: return (T*)&_c->uc_mcontext.gregs[REG_R12];
						case Register::R13: return (T*)&_c->uc_mcontext.gregs[REG_R13];
						case Register::R14: return (T*)&_c->uc_mcontext.gregs[REG_R14];
						case Register::R15: return (T*)&_c->uc_mcontext.gregs[REG_R15];
						case Register::RIP: return (T*)&_c->uc_mcontext.gregs[REG_RIP];
						default: assert(false && "Unknown 64 bit register");
					}
				)
			)
		}
		assert(false && "Unsupported register or result size");
	}
};

#endif
