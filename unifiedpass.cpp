#include <algorithm>
#include <functional>
#include <string>
#include <vector>

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


  // ============================================================================
  // LICM PASS
  // ============================================================================
  struct LICMPass : PassInfoMixin<LICMPass> {

      // ------------------------------------------------------------------
      // Figure 4 from assignment: invariant candidate check
      // ------------------------------------------------------------------
      static bool isInvariantCandidate(const Instruction* I) {
          return isSafeToSpeculativelyExecute(I) &&
                !I->mayReadFromMemory()          &&
                !isa<LandingPadInst>(I);
      }

      // ------------------------------------------------------------------
      // Check if all operands of I are loop-invariant:
      //   - constants or function arguments: always invariant
      //   - defined outside the loop: invariant
      //   - defined inside the loop but already in invariants set: invariant
      // ------------------------------------------------------------------
      static bool isLoopInvariant(
              const Instruction* I,
              const SmallPtrSet<BasicBlock*, 8>& loopBlocks,
              const SmallPtrSet<const Instruction*, 16>& invariants) {

          if (!isInvariantCandidate(I)) return false;

          for (const Value* Op : I->operands()) {
              if (isa<Constant>(Op) || isa<Argument>(Op)) continue;
              if (const auto* opI = dyn_cast<Instruction>(Op)) {
                  if (!loopBlocks.count(opI->getParent())) continue; // outside loop
                  if (invariants.count(opI))               continue; // already proven
              }
              return false; // anything else: not invariant
          }
          return true;
      }

      // ------------------------------------------------------------------
      // Fixed-point collection of all loop-invariant instructions
      // ------------------------------------------------------------------
      static SmallPtrSet<const Instruction*, 16>
      collectInvariants(Loop* L, const SmallPtrSet<BasicBlock*, 8>& loopBlocks) {
          SmallPtrSet<const Instruction*, 16> invariants;
          bool changed = true;
          while (changed) {
              changed = false;
              for (BasicBlock* BB : L->blocks()) {
                  for (Instruction& I : *BB) {
                      if (invariants.count(&I)) continue;
                      if (isLoopInvariant(&I, loopBlocks, invariants)) {
                          invariants.insert(&I);
                          changed = true;
                      }
                  }
              }
          }
          return invariants;
      }

      // ------------------------------------------------------------------
      // Collect blocks outside the loop that are direct successors of
      // blocks inside the loop (these are the loop exit blocks)
      // ------------------------------------------------------------------
      static SmallVector<BasicBlock*, 4>
      getExitBlocks(Loop* L, const SmallPtrSet<BasicBlock*, 8>& loopBlocks) {
          SmallVector<BasicBlock*, 4> exits;
          SmallPtrSet<BasicBlock*, 4> seen;
          for (BasicBlock* BB : L->blocks())
              for (BasicBlock* succ : successors(BB))
                  if (!loopBlocks.count(succ) && !seen.count(succ)) {
                      exits.push_back(succ);
                      seen.insert(succ);
                  }
          return exits;
      }

      // ------------------------------------------------------------------
      // Topological sort of instructions to hoist
      // Ensures every def is placed before its uses in the target block
      // ------------------------------------------------------------------
      static SmallVector<Instruction*, 16>
      topoSort(const SmallVector<Instruction*, 16>& candidates,
              const SmallPtrSet<const Instruction*, 16>& invariantSet) {

          SmallPtrSet<Instruction*, 16> candSet(candidates.begin(), candidates.end());
          SmallPtrSet<Instruction*, 16> emitted;
          SmallVector<Instruction*, 16> ordered;

          std::function<void(Instruction*)> emit = [&](Instruction* I) {
              if (emitted.count(I)) return;
              emitted.insert(I);
              for (Value* Op : I->operands())
                  if (auto* opI = dyn_cast<Instruction>(Op))
                      if (candSet.count(opI))
                          emit(opI);   // emit dependency first
              ordered.push_back(I);
          };

          for (Instruction* I : candidates) emit(I);
          return ordered;
      }

      // ------------------------------------------------------------------
      // Landing Pad Transformation (Assignment Section 7.4)
      //
      // BEFORE:                    AFTER:
      //   preheader                  preheader
      //       |                          |
      //       v                          v
      //    header <--\               testBlock ---> loopExit
      //       |      |                   |
      //    body   latch              landingPad
      //       |      |                   |
      //    loopExit  |                header <--\
      //                                  |      |
      //                               body   latch
      //                                  |      |
      //                               loopExit  |
      //
      // testBlock:   replicates header branch; skips loop if condition false
      // landingPad:  unconditional; hoisted invariants live here;
      //              only reached when loop will execute >= 1 time
      // ------------------------------------------------------------------
      static BasicBlock* insertLandingPad(
              BasicBlock* header,
              BasicBlock* preheader,
              const SmallPtrSet<BasicBlock*, 8>& loopBlocks) {

          BranchInst* headerBr = dyn_cast<BranchInst>(header->getTerminator());
          if (!headerBr || !headerBr->isConditional()) return nullptr;

          BasicBlock* loopExit = nullptr;
          BasicBlock* loopBody = nullptr;
          for (unsigned i = 0; i < headerBr->getNumSuccessors(); ++i) {
              BasicBlock* succ = headerBr->getSuccessor(i);
              if (!loopBlocks.count(succ)) loopExit = succ;
              else                          loopBody  = succ;
          }
          if (!loopExit || !loopBody) return nullptr;

          LLVMContext& Ctx = header->getContext();
          Function* F   = header->getParent();

          // ---- Create blocks ----
          BasicBlock* testBlock  = BasicBlock::Create(Ctx, "licm.test",       F, header);
          BasicBlock* landingPad = BasicBlock::Create(Ctx, "licm.landingpad", F, header);

          BranchInst::Create(header, landingPad);

          // ------------------------------------------------------------------
          // 1. Recursive cloner to evaluate header values inside testBlock
          // This fixes the "%cmp used before defined" dominance error.
          // ------------------------------------------------------------------
          DenseMap<Value*, Value*> clonedVals;
          std::function<Value*(Value*)> cloneForTestBlock = [&](Value* V) -> Value* {
              if (clonedVals.count(V)) return clonedVals[V];

              Instruction* I = dyn_cast<Instruction>(V);
              // If it's not an instruction or outside header, it already dominates testBlock
              if (!I || I->getParent() != header)
                  return V;

              // If it's a PHI, grab the initial value coming from preheader
              if (PHINode* phi = dyn_cast<PHINode>(I)) {
                  Value* inc = phi->getIncomingValueForBlock(preheader);
                  clonedVals[V] = inc;
                  return inc;
              }

              // Otherwise, recursively clone the instruction
              Instruction* cInst = I->clone();
              for (unsigned i = 0; i < cInst->getNumOperands(); ++i) {
                  cInst->setOperand(i, cloneForTestBlock(cInst->getOperand(i)));
              }
              cInst->insertInto(testBlock, testBlock->end());
              clonedVals[V] = cInst;
              return cInst;
          };

          // Re-evaluate condition for testBlock
          Value* cond     = headerBr->getCondition();
          Value* testCond = cloneForTestBlock(cond);
          bool bodyIsTrue = (headerBr->getSuccessor(0) == loopBody);

          if (bodyIsTrue)
              BranchInst::Create(landingPad, loopExit, testCond, testBlock);
          else
              BranchInst::Create(loopExit, landingPad, testCond, testBlock);

          // ---- Redirect preheader ----
          Instruction* preTerm = preheader->getTerminator();
          for (unsigned i = 0; i < preTerm->getNumSuccessors(); ++i)
              if (preTerm->getSuccessor(i) == header)
                  preTerm->setSuccessor(i, testBlock);

          // ---- Fix header PHIs ----
          for (PHINode& phi : header->phis()) {
              int idx = phi.getBasicBlockIndex(preheader);
              if (idx >= 0)
                  phi.setIncomingBlock(idx, landingPad);
          }

          // ---- Fix existing loopExit PHIs ----
          for (PHINode& phi : loopExit->phis()) {
              int idx = phi.getBasicBlockIndex(header);
              if (idx >= 0) {
                  Value* incoming = phi.getIncomingValue(idx);
                  phi.addIncoming(cloneForTestBlock(incoming), testBlock);
              }
          }

          // ------------------------------------------------------------------
          // 2. Fix direct external uses of header definitions
          // This fixes the "%p.0 used before defined" dominance error.
          // ------------------------------------------------------------------
          SmallVector<Instruction*, 8> headerInsts;
          for (Instruction& I : *header) headerInsts.push_back(&I);

          for (Instruction* I : headerInsts) {
              SmallVector<Use*, 8> externalUses;
              for (Use& U : I->uses()) {
                  Instruction* user = cast<Instruction>(U.getUser());
                  // If the use is outside the loop
                  if (!loopBlocks.count(user->getParent())) {
                      // Skip if it's already an operand of a PHI in loopExit we just handled
                      if (PHINode* userPhi = dyn_cast<PHINode>(user)) {
                          if (userPhi->getParent() == loopExit) continue;
                      }
                      externalUses.push_back(&U);
                  }
              }

              if (!externalUses.empty()) {
                  // Insert a new LCSSA PHI node to safely merge the values
                  PHINode* exitPhi = PHINode::Create(I->getType(), 2, I->getName() + ".lcssa", loopExit->getFirstNonPHIIt());
                  exitPhi->addIncoming(I, header);
                  exitPhi->addIncoming(cloneForTestBlock(I), testBlock);

                  for (Use* U : externalUses) {
                      U->set(exitPhi);
                  }
              }
          }

          errs() << "  Landing pad inserted: "
                 << testBlock->getName()  << " -> "
                 << landingPad->getName() << " -> "
                 << header->getName()     << "\n";

          return landingPad;
      }
      // ------------------------------------------------------------------
      // Process one loop
      // ------------------------------------------------------------------
      static bool processLoop(Loop* L, const DominatorAnalysis::Result& DR) {

          BasicBlock* header    = L->getHeader();
          BasicBlock* preheader = L->getLoopPreheader();

          // Assignment: skip loops with no preheader
          if (!preheader) {
              errs() << "  Skipping (no preheader): "
                    << getShortValueName(header) << "\n";
              return false;
          }

          // Build loop block set
          SmallPtrSet<BasicBlock*, 8> loopBlocks;
          for (BasicBlock* BB : L->blocks()) loopBlocks.insert(BB);

          SmallVector<BasicBlock*, 4> exitBlocks = getExitBlocks(L, loopBlocks);

          // ---- Collect invariants ----
          SmallPtrSet<const Instruction*, 16> invariants =
              collectInvariants(L, loopBlocks);

          if (invariants.empty()) return false;

          errs() << "  Header: " << getShortValueName(header)
                << "  Invariants found: " << invariants.size() << "\n";

          // ---- Partition: dominates all exits vs does not ----
          SmallVector<Instruction*, 16> toPreheader;
          SmallVector<Instruction*, 16> toLandingPad;

          for (BasicBlock* BB : L->blocks()) {
              for (Instruction& I : *BB) {
                  if (!invariants.count(&I)) continue;

                  bool domAllExits = true;
                  for (BasicBlock* exit : exitBlocks)
                      if (!DominatorAnalysis::dominates(BB, exit, DR)) {
                          domAllExits = false;
                          break;
                      }

                  if (domAllExits) toPreheader.push_back(&I);
                  else             toLandingPad.push_back(&I);
              }
          }

          bool anyHoisted = false;

          // ---- Hoist group A -> preheader ----
          if (!toPreheader.empty()) {
              Instruction* insertPt = preheader->getTerminator();
              for (Instruction* I : topoSort(toPreheader, invariants)) {
                  errs() << "  Hoist to preheader: " << *I << "\n";
                  I->moveBefore(insertPt->getIterator());
                  anyHoisted = true;
              }
          }

          // ---- Hoist group B -> landing pad ----
          if (!toLandingPad.empty()) {
              BasicBlock* lp = insertLandingPad(header, preheader, loopBlocks);
              if (lp) {
                  Instruction* insertPt = lp->getTerminator();
                  for (Instruction* I : topoSort(toLandingPad, invariants)) {
                      errs() << "  Hoist to landing pad: " << *I << "\n";
                      I->moveBefore(insertPt->getIterator());
                      anyHoisted = true;
                  }
              }
          }

          return anyHoisted;
      }

      // ------------------------------------------------------------------
      // Main entry point
      // ------------------------------------------------------------------
      PreservedAnalyses run(Function& F, FunctionAnalysisManager& FAM) {
          errs() << "=== LICM: " << F.getName() << " ===\n";

          auto& LI = FAM.getResult<LoopAnalysis>(F);

          // Compute dominators using your pass
          DominatorAnalysis::Result DR = DominatorAnalysis::computeDominators(F);

          bool anyChange = false;

          for (Loop* L : LI) {
              // Process innermost loops first so invariants bubble outward
              for (Loop* sub : L->getSubLoops()) {
                  DominatorAnalysis::Result subDR =
                      anyChange ? DominatorAnalysis::computeDominators(F) : DR;
                  anyChange |= processLoop(sub, subDR);
              }

              DominatorAnalysis::Result outerDR =
                  anyChange ? DominatorAnalysis::computeDominators(F) : DR;
              anyChange |= processLoop(L, outerDR);
          }

          return anyChange ? PreservedAnalyses::none()
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