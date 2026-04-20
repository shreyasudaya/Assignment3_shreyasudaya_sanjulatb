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
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>
#include "llvm/Analysis/AliasAnalysis.h"
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

  //Dominator Tree should be stored to be used in LICM
  class Dominator_Tree {
    DenseMap<BasicBlock*, BasicBlock*> IDoms;

  public:
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

      //Iteratively compute dominators until convergence
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

      //Extract immediate dominators from the final dominator sets
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

    //Helper function to check if A dominates B using the IDoms map
    bool dominates(BasicBlock *A, BasicBlock *B) {
      if (A == B) return true;
      BasicBlock *Curr = B;
      while (IDoms.count(Curr) && IDoms[Curr] != Curr) {
        Curr = IDoms[Curr];
        if (Curr == A) return true;
      }
      return false;
    }
    
    //Helper function to get the immediate dominator of a block
    BasicBlock* getIDom(BasicBlock *BB) { return IDoms.lookup(BB); }
  };  

  //Dominator Analysis Pass
  struct DominatorAnalysis : public PassInfoMixin<DominatorAnalysis> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
      Dominator_Tree DT;
      DT.build(F); 
      
      errs() << "Dominator Tree for " << F.getName() << "\n";
      for (BasicBlock &BB : F) {
        BasicBlock *IDom = DT.getIDom(&BB);
        std::string name = (IDom == &BB) ? "None (Root)" : getShortValueName(IDom);
        errs() << "Block " << getShortValueName(&BB) << " -> IDom: " << name << "\n";
      }
      return PreservedAnalyses::all();
    }
  };

  //Dead Code Elimination Pass using Faint Variable Analysis
  struct DCEPass : PassInfoMixin<DCEPass> {
    //Liveness conditions (according to the assignment spec):
    static bool isLive(const Instruction* I) {
      if (I->isTerminator()) return true;
      if (isa<DbgInfoIntrinsic>(I)) return true;
      if (I->mayHaveSideEffects()) return true; 
      if (isa<LandingPadInst>(I)) return true;
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

      //Iteratively compute FaintIn and FaintOut until convergence
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
              if (!faintOut[&I].test(lhsIdx)) { 
                FaintKill |= Rhs;              
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
              errs() << I << "\n";    
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

  struct LICMSafePass : PassInfoMixin<LICMSafePass> {
    
    bool isInstructionInvariant(Instruction &I, Loop &L, const DenseSet<Instruction*> &HoistedSet) {
      if (I.getType()->isVoidTy() || I.isTerminator() || isa<PHINode>(&I)) 
        return false;
      
      //Explicitly check for side effects or memory reads
      //Uses the conditions from the assignment spec:
      if (I.mayReadFromMemory() || I.mayHaveSideEffects() || !isSafeToSpeculativelyExecute(&I))
        return false;

      for (Value *Op : I.operands()) {
        //Constants and Arguments are always invariant
        if (isa<Constant>(Op) || isa<Argument>(Op)) continue;
        if (Instruction *OpInst = dyn_cast<Instruction>(Op)) {
            //An operand is valid if it's defined outside the loop 
            //OR if it's already in our 'to-be-hoisted' set.
            //Reaching definitions can be computed easily due to SSA form, 
            //So we just check the defining instruction's block against the loop.
            if (L.contains(OpInst->getParent()) && !HoistedSet.count(OpInst)) 
                return false;
            continue;
        }
        return false; //If we encounter a non-instruction operand that isn't a constant or argument, we conservatively treat it as variant
      }
      return true; //If all checks passed, the instruction is invariant
    }

    //This function performs the loop rotation transformation as described in the assignment spec
    //According to Figure 10
    void rotateLoop(Loop &L, LoopStandardAnalysisResults &AR) {
      BasicBlock *OrigHeader = L.getHeader();
      BasicBlock *Latch      = L.getLoopLatch();
      BasicBlock *Preheader  = L.getLoopPreheader();
      Function   *F          = OrigHeader->getParent();
      LLVMContext &Ctx       = F->getContext();

      //Create the new blocks
      BasicBlock *LandingPad = BasicBlock::Create(Ctx, "loop.landingpad", F, OrigHeader);
      BasicBlock *CondBlock  = BasicBlock::Create(Ctx, "loop.cond", F);

      //Identify the Loop Body Successor and Exit Block from the original terminator
      auto *OrigTerm = cast<BranchInst>(OrigHeader->getTerminator());
      BasicBlock *LoopBodySucc = OrigTerm->getSuccessor(0);
      BasicBlock *ExitBlock    = OrigTerm->getSuccessor(1);
      //Ensure LoopBodySucc is the one that stays in the loop after rotation
      if (!L.contains(LoopBodySucc)) std::swap(LoopBodySucc, ExitBlock);

      //Store original PHIs to their initial values BEFORE replacement
      DenseMap<PHINode*, Value*> PhiToInitMap;
      std::vector<PHINode*> PHIs;
      for (PHINode &PN : OrigHeader->phis()) {
        PHIs.push_back(&PN);
        PhiToInitMap[&PN] = PN.getIncomingValueForBlock(Preheader);
      }

      //Clone condition for the new Latch (CondBlock - BB with the backedge to the header)
      Value *Cond = OrigTerm->getCondition();
      Instruction *ClonedCondInst = nullptr; 
      if (auto *CondInst = dyn_cast<Instruction>(Cond)) {
        if (CondInst->getParent() == OrigHeader) {
          ClonedCondInst = CondInst->clone();
          ClonedCondInst->insertInto(CondBlock, CondBlock->end());
          Cond = ClonedCondInst;
        }
      }

      //Setup Branch Logic for the new blocks
      //Create br OrigHeader -> {LandingPad, ExitBlock}
      BranchInst::Create(LandingPad, ExitBlock, OrigTerm->getCondition(), OrigHeader);
      OrigTerm->eraseFromParent();
      //Create br Landingpad -> LoopBodySucc 
      BranchInst::Create(LoopBodySucc, LandingPad);
      //Create br CondBlock -> {LoopBodySucc, ExitBlock}
      BranchInst::Create(LoopBodySucc, ExitBlock, Cond, CondBlock);
      //Create br Latch -> CondBlock
      CondBlock->moveAfter(Latch);
      Latch->getTerminator()->replaceSuccessorWith(OrigHeader, CondBlock);

      //Update LoopInfo to reflect the new structure
      L.removeBlockFromLoop(OrigHeader);
      L.addBasicBlockToLoop(CondBlock,AR.LI);
      L.moveToHeader(LoopBodySucc);

      //As new edges are added to the original header, 
      //We need to update PHI nodes in the ExitBlock that used to have incoming values from the original header
      //Create Rotated PHIs and update LCSSA
      for (PHINode *OrigPHI : PHIs) {
        Value *InitVal = PhiToInitMap[OrigPHI];
        Value *LoopVal = OrigPHI->getIncomingValueForBlock(Latch);
        if (ClonedCondInst) {
            ClonedCondInst->replaceUsesOfWith(OrigPHI, LoopVal);
        }
        PHINode *NewPHI = PHINode::Create(OrigPHI->getType(), 2, OrigPHI->getName() + ".rot", 
                                          LoopBodySucc->getFirstNonPHIIt());
        NewPHI->addIncoming(InitVal, LandingPad);
        NewPHI->addIncoming(LoopVal, CondBlock);

        //Update LCSSA PHIs in ExitBlock specifically
        for (PHINode &ExitPHI : ExitBlock->phis()) {
          for (unsigned i = 0; i < ExitPHI.getNumIncomingValues(); ++i) {
            if (ExitPHI.getIncomingValue(i) == OrigPHI && ExitPHI.getIncomingBlock(i) == OrigHeader) {
                //Path from Guard: Use Initial Value
                ExitPHI.setIncomingValue(i, InitVal); 
                //Path from New Latch: Use Loop Value (the Rotated PHI)
                ExitPHI.addIncoming(NewPHI, CondBlock);
            }
          }
        }

        //Update all other users (excluding the Guard block)
        OrigPHI->replaceUsesWithIf(NewPHI, [OrigHeader](Use &U) {
          if (auto *UI = dyn_cast<Instruction>(U.getUser()))
              return UI->getParent() != OrigHeader;
          return true;
        });

        OrigPHI->removeIncomingValue(Latch, false);
      }
    }
    
    PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                      LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    
      Function *F = L.getHeader()->getParent();
      
      //Rotate the loop to include a landing pad and a condition block as described in the assignment spec
      rotateLoop(L,AR);

      BasicBlock *Preheader = L.getLoopPreheader();
      //Assume that loop-simply pass is applied:
      if (!Preheader) {
          errs() << "Skipping: No preheader available for hoisting.\n";
          return PreservedAnalyses::all();
      }

      SmallVector<BasicBlock *, 4> ExitingBlocks;
      L.getExitingBlocks(ExitingBlocks);

      //For iterative worklist approach
      SmallVector<Instruction *, 16> Worklist;
      DenseSet<Instruction *> HoistedSet;        //Tracks what will be moved
      SmallVector<Instruction *, 16> OrderedHoist; //Maintains dependency order

      errs() << "\n--- Analyzing Rotated Loop: " << L.getHeader()->getName() << " ---\n";

      //Initialize worklist with all instructions
      for (BasicBlock *BB : L.getBlocks())
          for (Instruction &I : *BB)
              Worklist.push_back(&I);

      //Build Dominator Tree for the function      
      Dominator_Tree DT;
      DT.build(*F);

      //Iteratively process the worklist
      while (!Worklist.empty()) {
          Instruction *I = Worklist.pop_back_val();
          //Skip if already marked or is a PHI/Terminator
          if (HoistedSet.count(I)) continue;

          //Pass HoistedSet to check if operands are transitively invariant
          if (isInstructionInvariant(*I, L, HoistedSet)) {
              //Check if it dominates all exits
              bool dominatesAllExits = true;
              BasicBlock *InstBB = I->getParent();
              for (BasicBlock *EB : ExitingBlocks) {
                  if (!DT.dominates(InstBB, EB)) {
                      dominatesAllExits = false;
                      break;
                  }
              }

              //If an instruction is invariant and dominates all exits, it's safe to hoist.
              if (dominatesAllExits) {
                  errs() << "    [SAFE - MARKED FOR HOISTING] " << *I << "\n";
                  HoistedSet.insert(I);
                  OrderedHoist.push_back(I);

                  //Since I is now invariant, its users might become invariant
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

      //Hoist in discovered order to preserve dependencies
      Instruction *PreheaderTerminator = Preheader->getTerminator();
      auto InsertPos = PreheaderTerminator->getIterator();
      for (Instruction *Inst : OrderedHoist) {
          Inst->moveBefore(InsertPos);
      }

      errs() << "--- Successfully hoisted " << OrderedHoist.size() << " instructions. ---\n";
      return PreservedAnalyses::none();
    }
  };

  struct AggressiveLICMPass : PassInfoMixin<AggressiveLICMPass> {
    bool isInstructionInvariant(Instruction &I, Loop &L,
                                const DenseSet<Instruction *> &HoistedSet,
                                bool AllowLoads) {
      if (I.getType()->isVoidTy() || I.isTerminator() || isa<PHINode>(&I))
        return false;

      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        // Only hoist loads if the loop has no stores at all
        if (!AllowLoads)
          return false;
        if (LI->isVolatile() || !LI->isSimple())
          return false;

        // Pointer operand must be invariant
        Value *Ptr = LI->getPointerOperand();
        if (auto *PtrInst = dyn_cast<Instruction>(Ptr))
          if (L.contains(PtrInst->getParent()) && !HoistedSet.count(PtrInst))
            return false;

        return true;

      } else {
        if (I.mayHaveSideEffects() || !isSafeToSpeculativelyExecute(&I))
          return false;
      }

      for (Value *Op : I.operands()) {
        if (isa<Constant>(Op) || isa<Argument>(Op)) continue;
        if (Instruction *OpInst = dyn_cast<Instruction>(Op)) {
          if (L.contains(OpInst->getParent()) && !HoistedSet.count(OpInst))
            return false;
          continue;
        }
        return false;
      }
      return true;
    }

    PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                          LoopStandardAnalysisResults &AR, LPMUpdater &U) {

      Function *F = L.getHeader()->getParent();

      // Check for stores BEFORE rotation mutates the loop
      bool HasStore = false;
      for (BasicBlock *BB : L.getBlocks())
        for (Instruction &I : *BB)
          if (isa<StoreInst>(&I)) { HasStore = true; break; }

      bool AllowLoads = !HasStore;

      LICMSafePass helper;
      helper.rotateLoop(L, AR);

      BasicBlock *Preheader = L.getLoopPreheader();
      if (!Preheader) {
        errs() << "Skipping: No preheader available for hoisting.\n";
        return PreservedAnalyses::all();
      }

      SmallVector<BasicBlock *, 4> ExitingBlocks;
      L.getExitingBlocks(ExitingBlocks);

      SmallVector<Instruction *, 16> Worklist;
      DenseSet<Instruction *> HoistedSet;
      SmallVector<Instruction *, 16> OrderedHoist;

      errs() << "\n--- Analyzing Rotated Loop: " << L.getHeader()->getName()
            << (AllowLoads ? " [store-free: loads eligible]"
                            : " [has stores: loads blocked]")
            << " ---\n";

      for (BasicBlock *BB : L.getBlocks())
        for (Instruction &I : *BB)
          Worklist.push_back(&I);

      Dominator_Tree DT;
      DT.build(*F);

      while (!Worklist.empty()) {
        Instruction *I = Worklist.pop_back_val();
        if (HoistedSet.count(I)) continue;

        if (isInstructionInvariant(*I, L, HoistedSet, AllowLoads)) {
          bool dominatesAllExits = true;
          BasicBlock *InstBB = I->getParent();
          for (BasicBlock *EB : ExitingBlocks) {
            if (!DT.dominates(InstBB, EB)) {
              dominatesAllExits = false;
              break;
            }
          }

          if (dominatesAllExits) {
            errs() << "    [SAFE - MARKED FOR HOISTING] " << *I << "\n";
            HoistedSet.insert(I);
            OrderedHoist.push_back(I);

            for (User *U : I->users())
              if (auto *UI = dyn_cast<Instruction>(U))
                if (L.contains(UI->getParent()))
                  Worklist.push_back(UI);
          } else {
            errs() << "    [UNSAFE] Does not dominate all exits: " << *I << "\n";
          }
        }
      }

      if (OrderedHoist.empty())
        return PreservedAnalyses::all();

      Instruction *PreheaderTerminator = Preheader->getTerminator();
      auto InsertPos = PreheaderTerminator->getIterator();
      for (Instruction *Inst : OrderedHoist)
        Inst->moveBefore(InsertPos);

      errs() << "--- Successfully hoisted " << OrderedHoist.size()
            << " instructions. ---\n";
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
            FPM.addPass(createFunctionToLoopPassAdaptor(LICMSafePass()));
            return true;
          }
          else if (Name == "aggressive-licm") {
            FPM.addPass(createFunctionToLoopPassAdaptor(AggressiveLICMPass()));
            return true;
          }
          return false;
        }
      );
    }
  };
}