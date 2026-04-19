#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "llvm/IR/Dominators.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
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

  
  struct DominatorAnalysis : PassInfoMixin<DominatorAnalysis> {

    // ------------------------------------------------------------------
    // All dominator state needed by LICM, returned as a plain struct
    // so LICM can call computeDominators() without running a pass
    // ------------------------------------------------------------------
    struct Result {
        std::vector<BasicBlock*>   vertex;   // vertex[i] = BB at DFS index i
        DenseMap<BasicBlock*, int> index;    // BB -> DFS index
        std::vector<int>           idom;     // idom[i] = DFS index of idom(i)
    };

    // ------------------------------------------------------------------
    // Your Lengauer-Tarjan implementation, refactored into a static
    // function so LICMPass can call it directly
    // ------------------------------------------------------------------
    static Result computeDominators(Function& F) {
        Result R;
        std::vector<int> parent;
        int N = 0;

        // ---- DFS numbering (fixed: pass parent explicitly) ----
        std::function<void(BasicBlock*, int)> dfs = [&](BasicBlock* v, int par) {
            R.index[v] = N++;
            R.vertex.push_back(v);
            parent.push_back(par);
            for (BasicBlock* succ : successors(v))
                if (!R.index.count(succ))
                    dfs(succ, R.index[v]);
        };
        dfs(&F.getEntryBlock(), -1);

        // ---- Initialize arrays ----
        std::vector<int> semi(N), idom(N, -1), ancestor(N, -1), label(N);
        std::vector<std::vector<int>> bucket(N);
        for (int i = 0; i < N; ++i) { semi[i] = i; label[i] = i; }

        // ---- Path compression (your compress, fixed guard) ----
        std::function<void(int)> compress = [&](int v) {
            if (ancestor[v] != -1 && ancestor[ancestor[v]] != -1) {
                compress(ancestor[v]);
                if (semi[label[ancestor[v]]] < semi[label[v]])
                    label[v] = label[ancestor[v]];
                ancestor[v] = ancestor[ancestor[v]];
            }
        };

        auto eval = [&](int v) -> int {
            if (ancestor[v] == -1) return label[v];
            compress(v);
            return label[v];
        };

        // ---- Lengauer-Tarjan main loop (your logic, unchanged) ----
        for (int w = N - 1; w > 0; --w) {
            BasicBlock* BB = R.vertex[w];

            // Compute semidominator
            for (BasicBlock* pred : predecessors(BB)) {
                if (!R.index.count(pred)) continue;
                int v = R.index[pred];
                int u = eval(v);
                semi[w] = std::min(semi[w], semi[u]);
            }

            bucket[semi[w]].push_back(w);
            ancestor[w] = parent[w];   // link(parent[w], w)

            // Process bucket
            for (int v : bucket[parent[w]]) {
                int u = eval(v);
                idom[v] = (semi[u] < semi[v]) ? u : parent[w];
            }
            bucket[parent[w]].clear();
        }

        // Finalize (your logic, unchanged)
        for (int i = 1; i < N; ++i)
            if (idom[i] != semi[i])
                idom[i] = idom[idom[i]];

        R.idom = idom;
        return R;
    }

    // ------------------------------------------------------------------
    // dominates(): walks idom chain from B upward looking for A
    // ------------------------------------------------------------------
    static bool dominates(BasicBlock* A, BasicBlock* B, const Result& R) {
        if (A == B) return true;
        auto it = R.index.find(B);
        if (it == R.index.end()) return false;
        int cur = it->second;
        while (cur > 0) {
            cur = R.idom[cur];
            if (cur < 0) break;
            if (R.vertex[cur] == A) return true;
        }
        return false;
    }

    // ------------------------------------------------------------------
    // Pass entry point: prints idom for each block (your original output)
    // ------------------------------------------------------------------
    PreservedAnalyses run(Function& F, FunctionAnalysisManager& FAM) {
        auto& LI = FAM.getResult<LoopAnalysis>(F);
        Result R  = computeDominators(F);

        errs() << "=== Dominators: " << F.getName() << " ===\n";

        for (Loop* L : LI) {
            errs() << "Loop header: " << getShortValueName(L->getHeader()) << "\n";
            for (BasicBlock* BB : L->blocks()) {
                auto it = R.index.find(BB);
                if (it == R.index.end()) continue;
                int idx = it->second;
                if (idx == 0) {
                    errs() << "  " << getShortValueName(BB) << " idom: (entry)\n";
                    continue;
                }
                BasicBlock* idomBB = R.vertex[R.idom[idx]];
                errs() << "  " << getShortValueName(BB)
                       << " idom: " << getShortValueName(idomBB) << "\n";
            }
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

  // Checks if the instruction is safe to hoist out of the loop
  bool isSafeToHoist(Instruction *I) {
      return isSafeToSpeculativelyExecute(I) && 
            !I->mayReadFromMemory() && 
            !isa<LandingPadInst>(I);
  }

  // Checks if all operands of an instruction are loop-invariant
  bool hasLoopInvariantOperands(Instruction *I, Loop *L, const std::set<Instruction*> &InvariantSet) {
      for (Use &U : I->operands()) {
          Value *V = U.get();
          
          // Constants and Arguments are always loop-invariant
          if (isa<Constant>(V) || isa<Argument>(V)) {
              continue;
          }
          
          if (Instruction *OpInst = dyn_cast<Instruction>(V)) {
              // If the operand is an instruction defined OUTSIDE the loop, it's invariant
              if (!L->contains(OpInst)) {
                  continue;
              }
              // If it's defined INSIDE the loop, it must already be in our InvariantSet
              if (InvariantSet.count(OpInst)) {
                  continue;
              }
          }
          
          // If we reach here, at least one operand is modified inside the loop and not invariant
          return false;
      }
      return true; // All operands passed the checks
  }

  struct MyLICMSafetyPass : PassInfoMixin<MyLICMSafetyPass> {
    // Logic from before to check if operands are outside the loop
    bool isInstructionInvariant(Instruction &I, Loop &L) {
      if (I.getType()->isVoidTy() || I.isTerminator()) return false;
      for (Value *Op : I.operands()) {
        if (isa<Constant>(Op) || isa<Argument>(Op)) continue;
        if (Instruction *OpInst = dyn_cast<Instruction>(Op)) {
          if (L.contains(OpInst->getParent())) return false;
          continue;
        }
        return false;
      }
      return true;
    }

    PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                          LoopStandardAnalysisResults &AR, LPMUpdater &U) {
      
      DominatorTree &DT = AR.DT;
      errs() << "\n--- Analyzing Loop: " << L.getHeader()->getName() << " ---\n";

      // 1. Get and print all Exit Blocks
      SmallVector<BasicBlock *, 4> ExitingBlocks;
      L.getExitingBlocks(ExitingBlocks);
      
      errs() << "  Exiting Blocks: ";
      for (BasicBlock *EB : ExitingBlocks) {
        errs() << EB->getName() << " ";
      }
      errs() << "\n";

      // 2. Iterate through blocks and instructions
      for (BasicBlock *BB : L.getBlocks()) {
        for (Instruction &I : *BB) {
          
          if (isInstructionInvariant(I, L)) {
            errs() << "  Found Invariant: " << I << "\n";

            // 3. Check if the instruction's block dominates ALL exiting blocks
            bool dominatesAllExits = true;
            for (BasicBlock *EB : ExitingBlocks) {
              if (!DT.dominates(BB, EB)) {
                dominatesAllExits = false;
                break;
              }
            }

            if (dominatesAllExits) {
              errs() << "    [SAFE] Dominates all exiting blocks. Can be hoisted.\n";
            } else {
              errs() << "    [UNSAFE] Does not dominate all exiting blocks. Hoisting might change program behavior.\n";
            }
          }
        }
      }

      return PreservedAnalyses::all();
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
            FPM.addPass(createFunctionToLoopPassAdaptor(MyLICMSafetyPass()));
            return true;
          }
          return false;
        }
      );
    }
  };
}