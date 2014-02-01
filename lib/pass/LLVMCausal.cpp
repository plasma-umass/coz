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

struct Causal : public ModulePass {
	static char ID;
	
	Constant* probe_fn;
	
	Causal() : ModulePass(ID) {}
  
	virtual bool runOnModule(Module& m) {
		// Declare the basic block probe function
		probe_fn = m.getOrInsertFunction(
			"__causal_probe",
			TypeBuilder<void(), true>::get(m.getContext()));
		
		for(Function& f : m) {
      // Process each function defined in this module
      if(!f.isIntrinsic() && !f.isDeclaration())
				runOnFunction(m, f);
			
      // Rename main to allow setup code to run before the main program
			if(f.getName() == "main")
				f.setName("__real_main");
		}
		
		return true;
	}
	
	void runOnFunction(Module& m, Function& f) {
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
