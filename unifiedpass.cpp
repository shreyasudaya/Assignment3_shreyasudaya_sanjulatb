#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include <llvm/ADT/BitVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>

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
      //BB and indices
      std::vector<BasicBlock*> blocks;
      DenseMap<BasicBlock*, unsigned> blockIndices;
      for (auto& BB : F) {
        blockIndices[&BB] = blocks.size();
        blocks.push_back(&BB);
      }
      unsigned N = blocks.size();
      if (N == 0) return PreservedAnalyses::all();

      // Initialization for Dominators:
      // Dom(Entry) = {Entry}
      // Dom(Others) = {All Nodes}
      std::vector<BitVector> doms(N, BitVector(N, true));
      doms[0].reset();
      doms[0].set(0); 

      bool changed = true;
      while (changed) {
        changed = false;
        // Iterate over all blocks except the entry block
        for (unsigned i = 1; i < N; ++i) {
          BitVector newDoms(N, true); // Universal set for intersection
          bool hasPreds = false;

          for (auto* pred : predecessors(blocks[i])) {
            newDoms &= doms[blockIndices[pred]];
            hasPreds = true;
          }

          // If a block has no predecessors and isn't the entry, it dominates nothing but itself
          if (!hasPreds) newDoms.reset();

          newDoms.set(i); // A block always dominates itself
          
          if (newDoms != doms[i]) {
            doms[i] = newDoms;
            changed = true;
          }
        }
      }

      // Print results
      // Step 2: Filter and Print Immediate Dominators (IDoms)
      for (unsigned i = 0; i < N; ++i) {
        errs() << "Block " << getShortValueName(blocks[i]) << " is dominated by: ";
        
        for (unsigned d = 0; d < N; ++d) {
          // A node d is IDom(i) if d strictly dominates i AND
          // there is no other strict dominator 's' such that d dominates s.
          if (d == i || !doms[i].test(d)) continue; 

          bool isImmediate = true;
          for (unsigned s = 0; s < N; ++s) {
            if (s == i || s == d || !doms[i].test(s)) continue;
            // If d dominates another strict dominator s, then s is "closer" to i than d is.
            if (doms[s].test(d)) {
              isImmediate = false;
              break;
            }
          }

          if (isImmediate) {
            errs() << getShortValueName(blocks[d]) << " ";
          }
        }
        errs() << "\n";
      }

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
              if (succ_empty(&BB)) {
                // newFaintOut.reset(); // Function exit: values are NOT faint (useful)
              } else {
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
            BitVector FaintKill = Use;
            bool lhsIsUseful = false;
            for (int i : Lhs.set_bits()) {
              if (!faintOut[&I].test(i)) { // Not faint in Out set means useful
                lhsIsUseful = true;
                break;
              }
            }
            if (lhsIsUseful) FaintKill |= Rhs;

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
      for (auto& BB : F) {
        errs() << "BB: " << getShortValueName(&BB) << "\n";
        printBitSet(errs(), "faintIn", faintIn[&BB.front()], universe);
        printBitSet(errs(), "faintOut", faintOut[&BB.back()], universe);
      }

      //Eliminate Instructions
      std::vector<Instruction*> toRemove;
      for (auto& BB : F) {
        for (auto& I : BB) {
          if (!isLive(&I) && instIdxMap.count(&I)) {
            int idx = instIdxMap[&I];
            //Assignment is dead if its result is faint at the exit of the instruction
            if (faintOut[&I].test(idx)) {
              toRemove.push_back(&I);
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
} //namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "AssignmentPasses", "v0.1",
    [](PassBuilder& PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager& FPM, ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "dominator") {
            FPM.addPass(DominatorAnalysis());
            return true;
          } 
          else if (Name == "dead-code-elimination") {
            FPM.addPass(DCEPass());
            return true;
          }
          return false;
        }
      );
    }
  };
}