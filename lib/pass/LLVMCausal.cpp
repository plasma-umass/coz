/**
 * LLVM Transformation pass for Causal Profiling
 * Charlie Curtsinger <charlie@cs.umass.edu>
 */

#define DEBUG_TYPE "causal"

#include <llvm/IR/BasicBlock.h>
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
	
	Causal() : ModulePass(ID) {}
	
	bool isCausalRuntimeFunction(Function& f) {
		return f.getName() == probe_fn_name
			|| f.getName() == extern_enter_fn_name
			|| f.getName() == extern_exit_fn_name
			|| f.getName() == progress_fn_name;
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
				runOnFunction(f);
			}			
		}
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
	
	void runOnFunction(Function& f) {
		for(BasicBlock& b : f) {
			Instruction* insertion_point = b.getFirstNonPHI();
			while(insertion_point != NULL && isa<LandingPadInst>(insertion_point)) {
				insertion_point = insertion_point->getNextNode();
			}
			if(insertion_point != NULL) {
				CallInst::Create(probe_fn, "", insertion_point);
			}
		}
	}
};

char Causal::ID = 0;
static RegisterPass<Causal> X("causal", "Causal Profiling instrumentation pass");

static void registerCausalPass(const PassManagerBuilder&, PassManagerBase& PM) {
	PM.add(new Causal());
}

static RegisterStandardPasses RegisterCausal(PassManagerBuilder::EP_OptimizerLast, registerCausalPass);
static RegisterStandardPasses RegisterCausalForO0(PassManagerBuilder::EP_EnabledOnOptLevel0, registerCausalPass);
