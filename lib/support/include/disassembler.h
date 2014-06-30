#if !defined(CAUSAL_SUPPORT_DISASSEMBLER_H)
#define CAUSAL_SUPPORT_DISASSEMBLER_H

#include <udis86.h>

#include <limits>

#include "arch.h"
#include "interval.h"
#include "log.h"

using std::string;

class branch_target {
private:
	uintptr_t _pc;
	ud_operand_t _op;
	
public:
	branch_target(uintptr_t pc, ud_operand_t op) : _pc(pc), _op(op) {}
  
  /// Does this target depend on register or memory values?
	bool dynamic() {
		switch(_op.type) {
			case UD_OP_MEM:
			case UD_OP_REG:
				return true;
			default:
				return false;
		}
	}
	
  /// Get the destination pointer for the branch
	uintptr_t value() {
		if(_op.type == UD_OP_JIMM) {
			switch(_op.size) {
				case 8: return _op.lval.sbyte + _pc;
				case 16: return _op.lval.sword + _pc;
				case 32: return _op.lval.sdword + _pc;
				case 64: return _op.lval.sqword + _pc;
				default: WARNING << "Unsupported JIMM operand size: " << _op.size;
					return 0;
			}
		} else if(_op.type == UD_OP_MEM) {
			uintptr_t p;
			// Decode the base register, if any
			if(_op.base == UD_R_RIP) {
				p = _pc;
			} else if(_op.base == UD_NONE) {
				p = 0;
			} else {
				WARNING << "Unsupported memory operand base";
				return 0;
			}
			
			// Decode the index and scale portion, if any
			if(_op.index != UD_NONE) {
        WARNING << "Memory operands with an index register are not supported";
				return 0;
			}
      
			// Add the offset, if any
			if(_op.size == 8) {
				p += _op.lval.sbyte;
			} else if(_op.size == 16) {
				p += _op.lval.sword;
			} else if(_op.size == 32) {
				p += _op.lval.sdword;
			} else if(_op.size == 64) {
				p += _op.lval.sqword;
			}
			return *(uintptr_t*)p;
		}
		WARNING << "Unsupported jump target operand type";
		return 0;
	}
};

class disassembler {
private:
	ud_t _ud;
  bool _done;
	
public:
  /// Initialize the libudis86 object and begin disassembly
	disassembler(uintptr_t start, uintptr_t end = std::numeric_limits<uintptr_t>::max()) : _done(false) {
		ud_init(&_ud);
		ud_set_syntax(&_ud, UD_SYN_INTEL);
		_X86(ud_set_mode(&_ud, 32));
		_X86_64(ud_set_mode(&_ud, 64));
		ud_set_input_buffer(&_ud, (uint8_t*)start, end - start);
		ud_set_pc(&_ud, start);
		// Disassemble the first instruction
		next();
	}
  
  disassembler(interval range) : disassembler(range.getBase(), range.getLimit()) {}

  /// Disassemble the next instruction
	void next() {
    if(!_done)
      _done = !ud_disassemble(&_ud);
	}
  
  /// Check if disassembly has reached an unconditional branch or return
  bool fallsThrough() {
    switch(_ud.mnemonic) {
      case UD_Iret:
      case UD_Iretf:
      case UD_Ijmp:
      case UD_Iinvalid:
        return false;
      default:
        return true;
    }
  }
  
  /// Has disassembly reached a return or unconditional branch?
  bool done() {
    return _done;
  }
	
  /// Check if this instruction is a branch of some sort (not a call!)
  bool branches() {
		switch(_ud.mnemonic) {
			case UD_Ija:
			case UD_Ijae:
			case UD_Ijb:
			case UD_Ijbe:
			case UD_Ijcxz:
			case UD_Ijecxz:
			case UD_Ijg:
			case UD_Ijge:
			case UD_Ijl:
			case UD_Ijle:
			case UD_Ijmp:
			case UD_Ijno:
			case UD_Ijnp:
			case UD_Ijns:
			case UD_Ijnz:
			case UD_Ijo:
			case UD_Ijp:
			case UD_Ijrcxz:
			case UD_Ijs:
			case UD_Ijz:
        return true;
      default:
        return false;
    }
  }
  
  /// Get the base address of this instruction
  uintptr_t base() {
    return limit() - size();
  }
  
  /// Get the next program counter value after this instruction
  uintptr_t limit() {
    return _ud.pc;
  }
  
  /// Get the size of this instruction in bytes
  size_t size() {
    return _ud.inp_ctr;
  }
  
  /// Get the branch target for this instruction (always in operand 0)
	branch_target target() {
    return branch_target(_ud.pc, _ud.operand[0]);
	}

  /// Pretty-print the instruction
	const char* toString() {
		return ud_insn_asm(&_ud);
	}
};

#endif
