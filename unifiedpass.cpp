#include <algorithm>
#include <cstdint>
#include <functional>
#include <numeric>
#include <string>
#include <vector>
 
#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
using namespace llvm;

namespace {
  std::string getShortValueName(const Value* V) {
    if (!V) return "(null)";
    if (V->hasName()) return "%" + V->getName().str();
    if (const auto* C = dyn_cast<ConstantInt>(V)) return std::to_string(C->getSExtValue());
    std::string S;
    raw_string_ostream OS(S);
    V->printAsOperand(OS, false);
    return S;
  }

  template <typename T>
  void printBitSet(raw_ostream& OS, StringRef label, const BitVector& bits,
                  const std::vector<T>& universe) {
    OS << "  " << label << ": { ";
    bool first = true;
    for (unsigned i = 0; i < bits.size(); ++i) {
      if (!bits.test(i)) continue;
      if (!first) OS << "; ";
      first = false;
      OS << getShortValueName(universe[i]); 
    }
    OS << " }\n";
  }

  
  struct DominatorAnalysis : public PassInfoMixin<DominatorAnalysis> {
    PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
      // 1. Map each BasicBlock to an index for BitVector operations
      std::vector<BasicBlock*> blocks;
      DenseMap<BasicBlock *, unsigned> blockIndices;
      for (BasicBlock &BB : F) {
        blockIndices[&BB] = blocks.size();
        blocks.push_back(&BB);
      }

      unsigned N = blocks.size();
      if (N == 0) return PreservedAnalyses::all();

      // 2. Initialization: 
      // Dom(Entry) = {Entry}, Dom(Others) = {All Nodes}
      std::vector<BitVector> doms(N, BitVector(N, true));
      doms[0].reset();
      doms[0].set(0);

      // 3. Iterative Data-Flow Analysis
      bool changed = true;
      while (changed) {
        changed = false;
        for (unsigned i = 1; i < N; ++i) {
          BitVector newDoms(N, true);
          bool hasPreds = false;

          for (auto *pred : predecessors(blocks[i])) {
            newDoms &= doms[blockIndices[pred]];
            hasPreds = true;
          }

          // If a block is unreachable, it only dominates itself
          if (!hasPreds) newDoms.reset();
          newDoms.set(i);

          if (newDoms != doms[i]) {
            doms[i] = newDoms;
            changed = true;
          }
        }
      }

      // 4. Calculate and Print Immediate Dominators (IDoms)
      errs() << "\n*** Dominator Tree for Function: " << F.getName() << " ***\n";

      for (unsigned i = 0; i < N; ++i) {
        std::string idomName = "None (Entry)";
        
        // Find the strict dominator that is "closest" to block i
        for (unsigned d = 0; d < N; ++d) {
          // d must be a strict dominator of i
          if (d == i || !doms[i].test(d)) continue;

          bool isImmediate = true;
          for (unsigned s = 0; s < N; ++s) {
            // If there's another strict dominator 's' that 'd' dominates,
            // then 's' is closer to 'i' than 'd' is.
            if (s == i || s == d || !doms[i].test(s)) continue;
            if (doms[s].test(d)) {
              isImmediate = false;
              break;
            }
          }

          if (isImmediate) {
            idomName = getShortValueName(blocks[d]);
            break;
          }
        }
        
        errs() << "Block " << getShortValueName(blocks[i]) 
              << " -> Immediate Dominator: " << idomName << "\n";
      }
      errs() << "******************************************\n\n";

      return PreservedAnalyses::all();
    }
  };
  
  struct DCEPass : PassInfoMixin<DCEPass> {
    //Liveness conditions
    static bool isLive(const Instruction* I) {
      if (I->isTerminator()) return true;
      if (isa<DbgInfoIntrinsic>(I)) return true;
      if (I->mayHaveSideEffects()) return true; 
      if (I->getType()->isVoidTy()) return true;
      return false;
    }

    PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
      std::vector<Instruction*> universe;
      DenseMap<const Instruction*, int> instIdxMap;

      for (auto& BB : F) {
        for (auto& I : BB) {
          if (!I.getType()->isVoidTy()) {
            instIdxMap[&I] = universe.size();
            universe.push_back(&I);
          }
        }
      }

      if (universe.empty()) return PreservedAnalyses::all();

      DenseMap<const Instruction*, BitVector> faintIn, faintOut;
      BitVector universalSet(universe.size(), true);

      //Initialization to Top (True = Faint/Dead)
      for (auto& BB : F) {
        for (auto& I : BB) {
          faintIn[&I] = universalSet;
          faintOut[&I] = universalSet;
        }
      }

      bool changed = true;
      while (changed) {
        changed = false;
        for (BasicBlock &BB : llvm::reverse(F)) {
          for (Instruction &I : llvm::reverse(BB)) {
            BitVector oldFaintIn = faintIn[&I];

            //FaintOut_n = Intersection of FaintIn of successors
            BitVector newFaintOut(universe.size(), true);
            if (&I == &BB.back()) {
              if (!succ_empty(&BB)) {
                for (BasicBlock* succ : successors(&BB)) {
                  newFaintOut &= faintIn[&succ->front()];
                }
              }
            } else {
              newFaintOut = faintIn[I.getNextNode()];
            }
            faintOut[&I] = newFaintOut;

            //Compute Gen and Kill sets
            BitVector Lhs(universe.size(), false);
            BitVector Rhs(universe.size(), false);
            BitVector Use(universe.size(), false);

            if (!isLive(&I)) {
              if (instIdxMap.count(&I)) Lhs.set(instIdxMap[&I]);
              for (Value* Op : I.operands()) {
                if (auto* OpI = dyn_cast<Instruction>(Op)) {
                  if (instIdxMap.count(OpI)) Rhs.set(instIdxMap[OpI]);
                }
              }
            } else {
              for (Value* Op : I.operands()) {
                if (auto* OpI = dyn_cast<Instruction>(Op)) {
                  if (instIdxMap.count(OpI)) Use.set(instIdxMap[OpI]);
                }
              }
            }

            //FaintGen = Lhs - Rhs
            BitVector FaintGen = Lhs;
            FaintGen.reset(Rhs); 

            //FaintKill = {Rhs if Lhs is useful} U Use
            BitVector FaintKill(universe.size(), false);
            for (int lhsIdx : Lhs.set_bits()) {
              if (!faintOut[&I].test(lhsIdx)) { // This Lhs var is NOT faint (is useful)
                FaintKill |= Rhs;               // Kill all Rhs vars
                break;
              }
            }
            FaintKill |= Use;

            //FaintIn = (FaintOut - FaintKill) U FaintGen
            BitVector newFaintIn = faintOut[&I];
            newFaintIn.reset(FaintKill);
            newFaintIn |= FaintGen;

            if (newFaintIn != oldFaintIn) {
              faintIn[&I] = newFaintIn;
              changed = true;
            }
          }
        }
      }

      //Print 
      errs() << "=== " << F.getName() << " ===\n";

      //Eliminate Instructions
      std::vector<Instruction*> toRemove;
      printf("Instructions that can be deleted:\n");
      for (auto& BB : F) {
        for (auto& I : BB) {
          if (!isLive(&I) && instIdxMap.count(&I)) {
            int idx = instIdxMap[&I];
            //Assignment is dead if its result is faint at the exit of the instruction
            if (faintOut[&I].test(idx)) {
              toRemove.push_back(&I);
              errs() << I << "\n";     // Prints instruction details
            }
          }
        }
      }

      for (Instruction* I : toRemove) {
        errs() << "Removing Instruction: " << *I << "\n";
        I->replaceAllUsesWith(UndefValue::get(I->getType()));
        I->eraseFromParent();
      }

      return toRemove.empty() ? PreservedAnalyses::all() : PreservedAnalyses::none();
    }
  };

  struct LICMPass : PassInfoMixin<LICMPass> {

    // Check if instruction is loop-invariant (LLVM-style simplified)
    static bool isInvariant(Instruction *I,
                            Loop *L,
                            const SmallVectorImpl<Instruction*> &invariants) {

      if (I->isTerminator()) return false;
      if (I->mayHaveSideEffects()) return false;
      if (isa<PHINode>(I)) return false;

      for (Value *Op : I->operands()) {
        if (isa<Constant>(Op)) continue;

        if (auto *OpI = dyn_cast<Instruction>(Op)) {
          if (!L->contains(OpI)) continue;

          if (llvm::find(invariants, OpI) == invariants.end())
            return false;
        }
      }
      return true;
    }

    bool processLoop(Loop *L, DominatorTree &DT) {
      BasicBlock *preheader = L->getLoopPreheader();
      if (!preheader) {
        errs() << "LICM: skipping loop (no preheader)\n";
        return false;
      }

      bool changed = false;
      SmallVector<Instruction*, 32> invariants;

      bool progress = true;
      while (progress) {
        progress = false;

        for (BasicBlock *BB : L->blocks()) {
          for (Instruction &I : *BB) {

            if (llvm::find(invariants, &I) != invariants.end())
              continue;

            if (!isInvariant(&I, L, invariants))
              continue;

            if (!isSafeToSpeculativelyExecute(&I))
              continue;

            invariants.push_back(&I);
            progress = true;
          }
        }
      }

      if (invariants.empty()) return false;

      errs() << "LICM: found " << invariants.size()
            << " invariants in loop headed by "
            << getShortValueName(L->getHeader()) << "\n";

      for (Instruction *I : invariants) {

        // Ensure instruction dominates loop header (basic safety)
        if (!DT.dominates(I->getParent(), L->getHeader()))
          continue;

        errs() << "LICM hoisting: " << *I << "\n";

        I->moveBefore(preheader->getTerminator());
        changed = true;
      }

      return changed;
    }

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {

      errs() << "=== LICM: " << F.getName() << " ===\n";

      auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
      auto &LI = FAM.getResult<LoopAnalysis>(F);

      bool changed = false;

      // Process all loops (including nested)
      for (Loop *L : LI)
        changed |= processLoop(L, DT);

      return changed ? PreservedAnalyses::none()
                    : PreservedAnalyses::all();
    }
  };

} //namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "AssignmentPasses", "v0.1",
    [](PassBuilder& PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager& FPM, ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "dominators") {
            FPM.addPass(DominatorAnalysis());
            return true;
          } 
          else if (Name == "dead-code-elimination") {
            FPM.addPass(DCEPass());
            return true;
          }
          else if (Name == "loop-invariant-code-motion") {
            FPM.addPass(LICMPass());
            return true;
          }
          return false;
        }
      );
    }
  };
}