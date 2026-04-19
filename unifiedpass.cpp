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
#include <llvm/IR/IRBuilder.h>
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

  class MyDomTree {
    DenseMap<BasicBlock*, BasicBlock*> IDoms;

  public:
    // The logic we already wrote, moved here
    void build(Function &F) {
      std::vector<BasicBlock*> blocks;
      DenseMap<BasicBlock*, unsigned> blockIndices;
      for (BasicBlock &BB : F) {
        blockIndices[&BB] = blocks.size();
        blocks.push_back(&BB);
      }
      unsigned N = blocks.size();
      if (N == 0) return;

      std::vector<BitVector> doms(N, BitVector(N, true));
      doms[0].reset(); doms[0].set(0);

      bool changed = true;
      while (changed) {
        changed = false;
        for (unsigned i = 1; i < N; ++i) {
          BitVector newDoms(N, true);
          bool hasPreds = false;
          for (auto *pred : predecessors(blocks[i])) {
            if (blockIndices.count(pred)) {
              newDoms &= doms[blockIndices[pred]];
              hasPreds = true;
            }
          }
          if (!hasPreds) newDoms.reset();
          newDoms.set(i);
          if (newDoms != doms[i]) { doms[i] = newDoms; changed = true; }
        }
      }

      IDoms[blocks[0]] = blocks[0];
      for (unsigned i = 1; i < N; ++i) {
        for (unsigned d = 0; d < N; ++d) {
          if (d == i || !doms[i].test(d)) continue;
          bool isImmediate = true;
          for (unsigned s = 0; s < N; ++s) {
            if (s == i || s == d || !doms[i].test(s)) continue;
            if (doms[s].test(d)) { isImmediate = false; break; }
          }
          if (isImmediate) { IDoms[blocks[i]] = blocks[d]; break; }
        }
      }
    }

    bool dominates(BasicBlock *A, BasicBlock *B) {
      if (A == B) return true;
      BasicBlock *Curr = B;
      while (IDoms.count(Curr) && IDoms[Curr] != Curr) {
        Curr = IDoms[Curr];
        if (Curr == A) return true;
      }
      return false;
    }
    
    BasicBlock* getIDom(BasicBlock *BB) { return IDoms.lookup(BB); }
  };  

  struct DominatorAnalysis : public PassInfoMixin<DominatorAnalysis> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
      MyDomTree DT;
      DT.build(F); // Run calculation
      
      errs() << "Dominator Tree for " << F.getName() << "\n";
      for (BasicBlock &BB : F) {
        BasicBlock *IDom = DT.getIDom(&BB);
        std::string name = (IDom == &BB) ? "None (Root)" : getShortValueName(IDom);
        errs() << "Block " << getShortValueName(&BB) << " -> IDom: " << name << "\n";
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
    // TWEAK: Explicitly ignore PHI nodes to prevent breaking SSA form during hoisting
    // Added DenseSet to track instructions queued for hoisting
    bool isInstructionInvariant(Instruction &I, Loop &L, const DenseSet<Instruction*> &HoistedSet) {
        if (I.getType()->isVoidTy() || I.isTerminator() || isa<PHINode>(&I)) 
            return false;
        
        // Explicitly check for side effects or memory reads
        if (I.mayReadFromMemory() || I.mayHaveSideEffects() || !isSafeToSpeculativelyExecute(&I))
            return false;

        for (Value *Op : I.operands()) {
            if (isa<Constant>(Op) || isa<Argument>(Op)) continue;
            if (Instruction *OpInst = dyn_cast<Instruction>(Op)) {
                // An operand is valid if it's defined outside the loop 
                // OR if it's already in our 'to-be-hoisted' set.
                if (L.contains(OpInst->getParent()) && !HoistedSet.count(OpInst)) 
                    return false;
                continue;
            }
            return false;
        }
        return true;
    }


    void rotateLoop(Loop &L, LoopStandardAnalysisResults &AR) {
        BasicBlock *OrigHeader = L.getHeader();
        BasicBlock *Latch      = L.getLoopLatch();
        BasicBlock *Preheader  = L.getLoopPreheader();
        Function   *F          = OrigHeader->getParent();
        LLVMContext &Ctx       = F->getContext();

        // 1. Create the new blocks
        BasicBlock *LandingPad = BasicBlock::Create(Ctx, "loop.landing", F, OrigHeader);
        BasicBlock *CondBlock  = BasicBlock::Create(Ctx, "loop.cond", F);

        auto *OrigTerm = cast<BranchInst>(OrigHeader->getTerminator());
        BasicBlock *LoopBodySucc = OrigTerm->getSuccessor(0);
        BasicBlock *ExitBlock    = OrigTerm->getSuccessor(1);
        if (!L.contains(LoopBodySucc)) std::swap(LoopBodySucc, ExitBlock);

        // 2. Map original PHIs to their initial values BEFORE replacement
        DenseMap<PHINode*, Value*> PhiToInitMap;
        std::vector<PHINode*> PHIs;
        for (PHINode &PN : OrigHeader->phis()) {
            PHIs.push_back(&PN);
            PhiToInitMap[&PN] = PN.getIncomingValueForBlock(Preheader);
        }

        // 3. Clone condition for the new Latch (CondBlock)
        Value *Cond = OrigTerm->getCondition();
        if (auto *CondInst = dyn_cast<Instruction>(Cond)) {
            if (CondInst->getParent() == OrigHeader) {
                Instruction *Cloned = CondInst->clone();
                Cloned->insertInto(CondBlock, CondBlock->end());
                Cond = Cloned;
            }
        }

        // 4. Setup Branch Logic
        BranchInst::Create(LandingPad, ExitBlock, OrigTerm->getCondition(), OrigHeader);
        OrigTerm->eraseFromParent();
        BranchInst::Create(LoopBodySucc, LandingPad);
        BranchInst::Create(LoopBodySucc, ExitBlock, Cond, CondBlock);
        CondBlock->moveAfter(Latch);
        Latch->getTerminator()->replaceSuccessorWith(OrigHeader, CondBlock);

        // 5. Create Rotated PHIs and update LCSSA
        for (PHINode *OrigPHI : PHIs) {
          Value *InitVal = PhiToInitMap[OrigPHI];
          Value *LoopVal = OrigPHI->getIncomingValueForBlock(Latch);

          PHINode *NewPHI = PHINode::Create(OrigPHI->getType(), 2, OrigPHI->getName() + ".rot", 
                                            LoopBodySucc->getFirstNonPHI());
          NewPHI->addIncoming(InitVal, LandingPad);
          NewPHI->addIncoming(LoopVal, CondBlock);

          // Update LCSSA PHIs in ExitBlock specifically
          for (PHINode &ExitPHI : ExitBlock->phis()) {
              for (unsigned i = 0; i < ExitPHI.getNumIncomingValues(); ++i) {
                  if (ExitPHI.getIncomingValue(i) == OrigPHI && ExitPHI.getIncomingBlock(i) == OrigHeader) {
                      // Path from Guard: Use Initial Value
                      ExitPHI.setIncomingValue(i, InitVal); 
                      // Path from New Latch: Use Loop Value (the Rotated PHI)
                      ExitPHI.addIncoming(NewPHI, CondBlock);
                  }
              }
          }

          // Update all other users (excluding the Guard block)
          OrigPHI->replaceUsesWithIf(NewPHI, [OrigHeader](Use &U) {
              if (auto *UI = dyn_cast<Instruction>(U.getUser()))
                  return UI->getParent() != OrigHeader;
              return true;
          });

          
          OrigPHI->removeIncomingValue(Latch, false);
        }

        // 6. Final Analysis Update
        // 6. Final Analysis Update
        Loop *ParentLoop = L.getParentLoop();

        // 6a. OrigHeader is now the loop guard. It belongs outside this loop.
        L.removeBlockFromLoop(OrigHeader);
        AR.LI.changeLoopFor(OrigHeader, ParentLoop);

        // 6b. LandingPad is essentially the new preheader. It belongs outside.
        // (Do NOT add it to &L)
        AR.LI.changeLoopFor(LandingPad, ParentLoop);

        // 6c. CondBlock is the new latch. It belongs inside the loop.
        L.addBasicBlockToLoop(CondBlock, AR.LI);

        // 6d. Officially move the loop's entry point to the rotated body.
        L.moveToHeader(LoopBodySucc);

        // 6e. Recalculate Dominator Tree
        AR.DT.recalculate(*F);
    }
    
    PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                      LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    
      Function *F = L.getHeader()->getParent();
      
      // 1. Rotate the loop (Using the fixed logic from the previous step)
      rotateLoop(L, AR);

      BasicBlock *Preheader = L.getLoopPreheader();
      if (!Preheader) {
          errs() << "Skipping: No preheader available for hoisting.\n";
          return PreservedAnalyses::all();
      }

      SmallVector<BasicBlock *, 4> ExitingBlocks;
      L.getExitingBlocks(ExitingBlocks);

      // --- NEW: Iterative Logic Structures ---
      SmallVector<Instruction *, 16> Worklist;
      DenseSet<Instruction *> HoistedSet;        // Tracks what will be moved
      SmallVector<Instruction *, 16> OrderedHoist; // Maintains dependency order

      errs() << "\n--- Analyzing Rotated Loop: " << L.getHeader()->getName() << " ---\n";

      // Initialize worklist with all instructions
      for (BasicBlock *BB : L.getBlocks())
          for (Instruction &I : *BB)
              Worklist.push_back(&I);

      // Iteratively process the worklist
      while (!Worklist.empty()) {
          Instruction *I = Worklist.pop_back_val();

          // Skip if already marked or is a PHI/Terminator (handled by isInstructionInvariant)
          if (HoistedSet.count(I)) continue;

          // TWEAK: Pass HoistedSet to check if operands are "effectively" invariant
          if (isInstructionInvariant(*I, L, HoistedSet)) {
              
              bool dominatesAllExits = true;
              for (BasicBlock *EB : ExitingBlocks) {
                  if (!AR.DT.dominates(I, EB->getTerminator())) {
                      dominatesAllExits = false;
                      break;
                  }
              }

              if (dominatesAllExits) {
                  errs() << "    [SAFE - MARKED FOR HOISTING] " << *I << "\n";
                  HoistedSet.insert(I);
                  OrderedHoist.push_back(I);

                  // Since I is now invariant, its users might become invariant too
                  for (User *U : I->users())
                      if (auto *UI = dyn_cast<Instruction>(U))
                          if (L.contains(UI->getParent()))
                              Worklist.push_back(UI);
              } else {
                  errs() << "    [UNSAFE] Does not dominate all exits: " << *I << "\n";
              }
          }
      }
      if (OrderedHoist.empty()) {
          return PreservedAnalyses::all();
      }

      // Hoist in discovered order to preserve dependencies
      Instruction *PreheaderTerminator = Preheader->getTerminator();
      for (Instruction *Inst : OrderedHoist) {
          Inst->moveBefore(PreheaderTerminator);
      }

      errs() << "--- Successfully hoisted " << OrderedHoist.size() << " instructions. ---\n";
      return PreservedAnalyses::none();
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