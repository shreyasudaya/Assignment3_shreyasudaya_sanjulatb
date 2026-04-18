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

      //DFS NUMBERING
      std::vector<BasicBlock*> vertex;
      DenseMap<BasicBlock*, int> index;

      std::vector<int> parent;
      int N = 0;

      std::function<void(BasicBlock*)> dfs = [&](BasicBlock* v) {
        index[v] = N++;
        vertex.push_back(v);
        parent.push_back(-1);

        for (auto* succ : successors(v)) {
          if (!index.count(succ)) {
            parent.push_back(index[v]); 
            dfs(succ);
            parent[index[succ]] = index[v];
          }
        }
      };

      BasicBlock* entry = &F.getEntryBlock();
      dfs(entry);

      // Resize arrays
      std::vector<int> semi(N), idom(N, -1), ancestor(N, -1), label(N);
      std::vector<std::vector<int>> bucket(N);

      for (int i = 0; i < N; ++i) {
        semi[i] = i;
        label[i] = i;
      }

      //Union helpers
      std::function<void(int)> compress = [&](int v) {
        if (ancestor[ancestor[v]] != -1) {
          compress(ancestor[v]);
          if (semi[label[ancestor[v]]] < semi[label[v]])
            label[v] = label[ancestor[v]];
          ancestor[v] = ancestor[ancestor[v]];
        }
      };

      std::function<int(int)> eval = [&](int v) -> int {
        if (ancestor[v] == -1)
          return label[v];
        compress(v);
        return label[v];
      };

      auto link = [&](int v, int w) {
        ancestor[w] = v;
      };

      //Lengar-Tarjan Algorithm
      for (int w = N - 1; w > 0; --w) {
        BasicBlock* BB = vertex[w];

        //Compute semi-dominator
        for (auto* pred : predecessors(BB)) {
          if (!index.count(pred)) continue;
          int v = index[pred];
          int u = eval(v);
          semi[w] = std::min(semi[w], semi[u]);
        }

        bucket[semi[w]].push_back(w);
        link(parent[w], w);

        //Process Bucket
        for (int v : bucket[parent[w]]) {
          int u = eval(v);
          if (semi[u] < semi[v])
            idom[v] = u;
          else
            idom[v] = parent[w];
        }
      }

      //Finalize idominators
      for (int i = 1; i < N; ++i) {
        if (idom[i] != semi[i])
          idom[i] = idom[idom[i]];
      }

      for (int i = 1; i < N; ++i) {
        errs() << "Block " << getShortValueName(vertex[i])
              << " is dominated by "
              << getShortValueName(vertex[idom[i]]) << "\n";
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