/**
 * LLVM Transformation pass for Causal Profiling
 * Charlie Curtsinger <charlie@cs.umass.edu>
 */

#define DEBUG_TYPE "causal"

#include <llvm/DebugInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/PassManager.h>
#include <llvm/PassRegistry.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

using namespace llvm;

struct Causal : public ModulePass {
	static char ID;
	
	Constant* probe_fn;
	const StringRef probe_fn_name = "__causal_probe";
	
	Constant* extern_enter_fn;
	const StringRef extern_enter_fn_name = "__causal_extern_enter";
	
	Constant* extern_exit_fn;
	const StringRef extern_exit_fn_name = "__causal_extern_exit";
	
	const StringRef progress_fn_name = "__causal_progress";
	
	std::vector<Constant*> debug_info_elements;
	
	Causal() : ModulePass(ID) {}
	
	bool isCausalRuntimeFunction(Function& f) {
		return f.getName().find("__causal") == 0;
	}
	
	virtual bool runOnModule(Module& m) {
		// Probe function has type void()
		probe_fn = m.getOrInsertFunction(probe_fn_name, Type::getVoidTy(m.getContext()), NULL);
		// Extern enter function has type void(void*)
		extern_enter_fn = m.getOrInsertFunction(extern_enter_fn_name, 
			Type::getVoidTy(m.getContext()), Type::getInt8PtrTy(m.getContext()), NULL);
		// Extern exit function has type void()
		extern_exit_fn = m.getOrInsertFunction(extern_exit_fn_name, Type::getVoidTy(m.getContext()), NULL);
		
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
		
		GlobalVariable* info = createDebugInfoArray(m);
		Constant* register_debug_info_fn = m.getOrInsertFunction(
			"__causal_register_debug_info",
			Type::getVoidTy(m.getContext()),
			info->getType(),	// The debug info array pointer
			Type::getInt32Ty(m.getContext()),	// The number of debug info elements
			NULL);
		
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
				// Don't instrument the call if this function does not return, returns twice,
				// is a tail call, or has the NoReturn attribute
				if(call->doesNotReturn() || call->canReturnTwice() || 
						call->isTailCall() || call->hasFnAttr(Attribute::NoReturn)) {
					errs() << "Skipping call to function " << f.getName() << " in " << 
						call->getParent()->getParent()->getName() << "\n";
				} else {
					// Cast the external function pointer to an i8* (a.k.a. void*)
					Value* arg = new BitCastInst(&f, Type::getInt8PtrTy(f.getContext()), "_extern_fn", call);
					// Call the extern_enter function before calling the external function
					CallInst::Create(extern_enter_fn, arg, "", call);
					// Call the extern_exit function after returning
					CallInst::Create(extern_exit_fn, "", call->getNextNode());
				}
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
				Constant* info = getDebugInfo(m, b);
				if(info != NULL) {
					debug_info_elements.push_back(info);
				}
			}
		}
	}
	
	Constant* getIntAsPtr(Module& m, uint64_t n) {
		return ConstantExpr::getIntToPtr(
			ConstantInt::get(Type::getInt64Ty(m.getContext()), n, false),
			Type::getInt8PtrTy(m.getContext()));
	}
	
	StructType* getDebugInfoType(Module& m) {
		return StructType::get(
			Type::getInt8PtrTy(m.getContext()),	// Block address
			Type::getInt8PtrTy(m.getContext()), // Starting file name string pointer
			Type::getInt32Ty(m.getContext()), 	// Starting line number
			Type::getInt8PtrTy(m.getContext()), // Ending file name string pointer
			Type::getInt32Ty(m.getContext()),		// Ending line number
			NULL);
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
	
	Constant* getDebugInfo(Module& m, BasicBlock& b) {
		MDNode* start = b.front().getMetadata("dbg");
		MDNode* end = b.back().getMetadata("dbg");
		if(start && end) {
			DILocation start_dbg(start);
			DILocation end_dbg(end);
			
			return ConstantStruct::get(
				getDebugInfoType(m),
				BlockAddress::get(&b),
				getStringPointer(m, start_dbg.getFilename()),
				ConstantInt::get(Type::getInt32Ty(m.getContext()), start_dbg.getLineNumber(), false),
				getStringPointer(m, end_dbg.getFilename()),
				ConstantInt::get(Type::getInt32Ty(m.getContext()), end_dbg.getLineNumber(), false),
				NULL);
			
		} else {
			return NULL;
		}
	}
	
	GlobalVariable* createDebugInfoArray(Module& m) {
		Constant* init = ConstantArray::get(
			ArrayType::get(getDebugInfoType(m), debug_info_elements.size()),
			debug_info_elements);
		
		return new GlobalVariable(m, init->getType(), true, GlobalVariable::InternalLinkage, init, "__causal_debug_info");
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
    
    // set up the constant initializer for the new constructor table
    Constant *ctor_array_const = ConstantArray::get(
      ArrayType::get(
        ctor_entries[0]->getType(),
        ctor_entries.size()
      ),
      ctor_entries
    );

    // create the new constructor table
    GlobalVariable *new_ctors = new GlobalVariable(
      m,
      ctor_array_const->getType(),
      true,
      GlobalVariable::AppendingLinkage,
      ctor_array_const,
      ""
    );

    // Get the existing constructor array from the module, if any
    GlobalVariable *ctors = m.getGlobalVariable("llvm.global_ctors", false);
    
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
