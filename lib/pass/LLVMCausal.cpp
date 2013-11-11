/**
 * LLVM Transformation pass for Causal Profiling
 * Charlie Curtsinger <charlie@cs.umass.edu>
 */

#define DEBUG_TYPE "causal"

#include <stdint.h>

#include <llvm/DebugInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/TypeBuilder.h>
#include <llvm/Pass.h>
#include <llvm/PassManager.h>
#include <llvm/PassRegistry.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

using namespace llvm;
using namespace types;

struct DebugInfo {
	void* block;
	const char* filename;
	uint32_t start;
	uint32_t end;
};

namespace llvm {
	template<bool xcompile>
	class TypeBuilder<DebugInfo, xcompile> {
	public:
		static StructType *get(LLVMContext &c) {
			return StructType::get(
				TypeBuilder<i<8>*, xcompile>::get(c),
				TypeBuilder<i<8>*, xcompile>::get(c),
				TypeBuilder<i<32>, xcompile>::get(c),
				TypeBuilder<i<32>, xcompile>::get(c),
				NULL);
		}
	};
}

struct Causal : public ModulePass {
	static char ID;
	
	Constant* probe_fn;
	Constant* extern_call_fn;
	
	std::vector<Constant*> debug_info_elements;
	
	Causal() : ModulePass(ID) {}
	
	bool isCausalRuntimeFunction(Function& f) {
		return f.getName().find("__causal") == 0;
	}
	
	virtual bool runOnModule(Module& m) {
		// Declare the basic block probe function
		probe_fn = m.getOrInsertFunction(
			"__causal_probe",
			TypeBuilder<void(), true>::get(m.getContext()));
		
		// Extern enter function has type void(void*)
		extern_call_fn = m.getOrInsertFunction(
			"__causal_extern_call", 
			TypeBuilder<void(i<8>*), true>::get(m.getContext()));
		
		for(Function& f : m) {
			if(f.isIntrinsic()) {
				runOnIntrinsic(f);
			} else if(f.isDeclaration()) {
				if(!isCausalRuntimeFunction(f)) {
					runOnDeclaration(f);
				}
			} else {
				runOnFunction(m, f);
			}
			
			if(f.getName() == "main") {
				f.setName("__real_main");
			}
		}
		
		Constant* info = createDebugInfoArray(m);
		
		Constant* register_debug_info_fn = m.getOrInsertFunction(
			"__causal_register_debug_info",
			TypeBuilder<void(DebugInfo*, i<32>), true>::get(m.getContext()));
		
    Function* ctor = makeConstructor(m, "__causal_module_ctor");
    BasicBlock* ctor_bb = BasicBlock::Create(m.getContext(), "", ctor);
		std::vector<Value*> args;
		args.push_back(info);
		args.push_back(ConstantInt::get(Type::getInt32Ty(m.getContext()), debug_info_elements.size(), false));
		
		CallInst::Create(register_debug_info_fn, args, "", ctor_bb);
		ReturnInst::Create(m.getContext(), ctor_bb);
		
		return true;
	}
	
	/// Do nothing for intrinsic functions
	void runOnIntrinsic(Function& f) {}
	
	/// Wrap calls to external functions in extern_
	void runOnDeclaration(Function& f) {
		for(auto iter = f.use_begin(); iter != f.use_end(); iter++) {
			User* user = *iter;
			if(isa<CallInst>(user)) {
				CallInst* call = dyn_cast<CallInst>(user);
				// Cast the external function pointer to an i8* (a.k.a. void*)
				Value* arg = new BitCastInst(&f, Type::getInt8PtrTy(f.getContext()), "_extern_fn", call);
				// Call the extern_call function before the call
				CallInst::Create(extern_call_fn, arg, "", call);
			}
		}
	}
	
	void runOnFunction(Module& m, Function& f) {
		for(BasicBlock& b : f) {
			Instruction* insertion_point = b.getFirstNonPHI();
			while(insertion_point != NULL && isa<LandingPadInst>(insertion_point)) {
				insertion_point = insertion_point->getNextNode();
			}
			if(insertion_point != NULL) {
				CallInst::Create(probe_fn, "", insertion_point);
				collectDebugInfo(m, b);
			}
		}
	}
	
	Constant* getIntAsPtr(Module& m, uint64_t n) {
		return ConstantExpr::getIntToPtr(
			ConstantInt::get(Type::getInt64Ty(m.getContext()), n, false),
			Type::getInt8PtrTy(m.getContext()));
	}
	
	StructType* getDebugInfoType(Module& m) {
		return TypeBuilder<DebugInfo, true>::get(m.getContext());
		/*return StructType::get(
			Type::getInt8PtrTy(m.getContext()),	// Block address
			Type::getInt8PtrTy(m.getContext()), // File name
			Type::getInt32Ty(m.getContext()), 	// Starting line number
			Type::getInt32Ty(m.getContext()),		// Ending line number
			NULL);*/
	}
	
	Constant* getStringPointer(Module& m, StringRef s) {
		std::vector<Constant*> indices;
		indices.push_back(ConstantInt::get(Type::getInt32Ty(m.getContext()), 0, false));
		indices.push_back(ConstantInt::get(Type::getInt32Ty(m.getContext()), 0, false));
		
		Constant* str = ConstantDataArray::getString(m.getContext(), s);
		
		GlobalVariable* gv = new GlobalVariable(
			m,
			str->getType(),
			true,
			GlobalVariable::InternalLinkage,
			str);
		
		return ConstantExpr::getGetElementPtr(gv, indices);
	}
	
	void saveDebugInfo(Module& m, BasicBlock& b, StringRef filename, uint32_t start, uint32_t end) {
		if(filename.size() == 0 || start == 0 || end == 0) return;
		
		Constant* address;
		if(&b == &b.getParent()->getEntryBlock()) {
			address = ConstantExpr::getPointerCast(b.getParent(), Type::getInt8PtrTy(m.getContext()));
		} else {
			address = BlockAddress::get(&b);
		}
		
		Constant* r = ConstantStruct::get(
			getDebugInfoType(m),
			address,
			getStringPointer(m, filename),
			ConstantInt::get(Type::getInt32Ty(m.getContext()), start, false),
			ConstantInt::get(Type::getInt32Ty(m.getContext()), end, false),
			NULL);
			
		debug_info_elements.push_back(r);
	}
	
	void collectDebugInfo(Module& m, BasicBlock& b) {
		// Set the starting point
		BasicBlock::iterator i = b.begin();
		
		// Seek until debug info is found
		MDNode* md = NULL;
		while(md == NULL && i != b.end()) {
			md = i->getMetadata("dbg");
			i = i->getNextNode();
		}
		// Abort if no debug info was found
		if(i == b.end()) return;
		
		DILocation dbg(md);
		StringRef filename = dbg.getFilename();
		unsigned int start = dbg.getLineNumber();
		unsigned int end = start;
		
		// Traverse the remaining instructions
		while(i != b.end()) {
			// Get metadata for the current location
			md = i->getMetadata("dbg");
			if(md) {
				// Read debug info
				dbg = DILocation(md);
				// Does this debug info filename match the current filename?
				if(dbg.getFilename() == filename && (dbg.getLineNumber() == end || dbg.getLineNumber() == end + 1)) {
					// Yes. Just update the ending line number
					end = dbg.getLineNumber();
					// Update the starting line number, in case of odd zero lines
					if(start == 0) start = end;
				} else {
					// No. Save the old info and replace the filename, line number, etc.
					saveDebugInfo(m, b, filename, start, end);
					filename = dbg.getFilename();
					start = dbg.getLineNumber();
					end = start;
				}
			}
			i++;
		}
		// Update the starting line number, just in case
		if(start == 0) start = end;
		saveDebugInfo(m, b, filename, start, end);
	}
	
	Constant* createDebugInfoArray(Module& m) {
		Constant* init = ConstantArray::get(
			ArrayType::get(getDebugInfoType(m), debug_info_elements.size()),
			debug_info_elements);
		
		GlobalVariable* gv = new GlobalVariable(m, init->getType(), true, GlobalVariable::InternalLinkage, init, "__causal_debug_info");
		
		std::vector<Constant*> indices;
		indices.push_back(ConstantInt::get(Type::getInt32Ty(m.getContext()), 0, false));
		indices.push_back(ConstantInt::get(Type::getInt32Ty(m.getContext()), 0, false));
		
		return ConstantExpr::getGetElementPtr(gv, indices);
	}
	
  Function* makeConstructor(Module& m, StringRef name) {
    // Void type
    Type* void_t = Type::getVoidTy(m.getContext());

    // 32 bit integer type
    Type* i32_t = Type::getInt32Ty(m.getContext());

    // Constructor function type
    FunctionType* ctor_fn_t = FunctionType::get(void_t, false);
    PointerType* ctor_fn_p_t = PointerType::get(ctor_fn_t, 0);

    // Constructor table entry type
    StructType* ctor_entry_t = StructType::get(i32_t, ctor_fn_p_t, NULL);

    // Create constructor function
    Function* init = Function::Create(ctor_fn_t, Function::InternalLinkage, name, &m);

    // Sequence of constructor table entries
    std::vector<Constant*> ctor_entries;

    // Add the entry for the new constructor
    ctor_entries.push_back(
      ConstantStruct::get(ctor_entry_t,
        ConstantInt::get(i32_t, 65535, false),
        init,
        NULL
      )
    );
				
		// find the current constructor table
		GlobalVariable* ctors = m.getGlobalVariable("llvm.global_ctors", false);

		// if found, copy the entries from the current ctor table to the new one
		if(ctors) {
			Constant* initializer = ctors->getInitializer();
			ConstantArray* ctor_array_const = dyn_cast<ConstantArray>(initializer);

			if(ctor_array_const) {
				for(auto opi = ctor_array_const->op_begin(); opi != ctor_array_const->op_end(); opi++) {
					ConstantStruct* entry = dyn_cast<ConstantStruct>(opi->get());
					ctor_entries.push_back(entry);
				}
			} else {
				errs() << "warning: llvm.global_ctors is not a constant array\n";
			}
		}
    
    // set up the constant initializer for the new constructor table
    Constant* ctor_array_const = ConstantArray::get(
      ArrayType::get(
        ctor_entries[0]->getType(),
        ctor_entries.size()
      ),
      ctor_entries
    );

    // create the new constructor table
    GlobalVariable* new_ctors = new GlobalVariable(
      m,
      ctor_array_const->getType(),
      true,
      GlobalVariable::AppendingLinkage,
      ctor_array_const,
      ""
    );
    
    // give the new constructor table the appropriate name, taking it from the current table if one exists
    if(ctors) {
      new_ctors->takeName(ctors);
      ctors->setName("old.llvm.global_ctors");
      ctors->setLinkage(GlobalVariable::PrivateLinkage);
      ctors->eraseFromParent();
    } else {
      new_ctors->setName("llvm.global_ctors");
    }
    
    return init;
  }
};

char Causal::ID = 0;
static RegisterPass<Causal> X("causal", "Causal Profiling instrumentation pass");

static void registerCausalPass(const PassManagerBuilder&, PassManagerBase& PM) {
	PM.add(new Causal());
}

static RegisterStandardPasses RegisterCausal(PassManagerBuilder::EP_OptimizerLast, registerCausalPass);
static RegisterStandardPasses RegisterCausalForO0(PassManagerBuilder::EP_EnabledOnOptLevel0, registerCausalPass);
