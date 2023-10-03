/**
 * @file Loop Invariant Code Motion
 */
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/Transforms/Utils/LoopUtils.h>
#include <map>

using namespace llvm;

namespace {

class LoopInvariantCodeMotion final : public LoopPass {
public:
  static char ID;

  LoopInvariantCodeMotion() : LoopPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    /**
     * @todo(CSCD70) Request the dominator tree and the loop simplify pass.
     */
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.setPreservesCFG();
  }

  /**
   * @todo(CSCD70) Please finish the implementation of this method.
   */
  virtual bool runOnLoop(Loop *L, LPPassManager &LPM) override {
      LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
      DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
      BasicBlock *preheader = L->getLoopPreheader();
      if (!preheader) {
          outs() << "Warning no preheader\n";
          return false;
      }
      // Create a map of strings to integers
      std::map<Instruction*, bool> isInvariantMap;
      std::map<Instruction*, bool> canMoveMap;
      std::map<Instruction*, bool> MovedMap;
      // initialize it false
      for (BasicBlock *BB : L->blocks()) {
          for (Instruction &I : *BB) {
              isInvariantMap[&I] = false;
              canMoveMap[&I] = false;
              MovedMap[&I] = false;
          }
      }
      bool invariantChanged = true;
      while (invariantChanged) {
          std::map<Instruction*, bool> copyIsInvariantMap;
          for (BasicBlock *BB : L->blocks()) {
              for (Instruction &I : *BB) {
                  bool invariant = isInvariant(&I, LI, isInvariantMap);
                  copyIsInvariantMap[&I] = invariant;
              }
          }
          invariantChanged = false;
          for (auto const& Elem : copyIsInvariantMap)
          {
              invariantChanged = invariantChanged || Elem.second != isInvariantMap[Elem.first];
              isInvariantMap[Elem.first] = Elem.second;
          }
      }
      for (auto const& invariantElem : isInvariantMap)
      {
          if (invariantElem.second) {
              // an instruction is a candidate for code motion (can move) if it is
              // 1. loop invariant
              // 2. in blocks that dominate all the exits of the loop
              // 3. assign to variable not assigned to elsewhere in the loop
              // 4. in blocks that dominate all blocks in the loop that use the variable assigned
              Instruction *I = invariantElem.first;
              // Check if the parent basic block dominates all loop exits
              bool dominatesLoopExits = true;
              SmallVector<BasicBlock *, 4> exitingBlocks;
              L->getExitingBlocks(exitingBlocks);
              for (BasicBlock *exitingBB : exitingBlocks) {
                  for (succ_iterator SI = succ_begin(exitingBB), SE = succ_end(exitingBB); SI != SE; ++SI) {
                      BasicBlock *exitBB = *SI;
                      if (!L->contains(exitBB) && !DT->dominates(I->getParent(), exitBB)) {
                          dominatesLoopExits = false;
                          break;
                      }
                  }
                  if (!dominatesLoopExits) {
                      break;
                  }
              }
              // Check if the variable assigned by the instruction is not assigned to elsewhere in the loop
              bool uniqueAssignment = true;
              for (BasicBlock *BB : L->blocks()) {
                  for (Instruction &otherI : *BB) {
                      if (&otherI != I && otherI.mayWriteToMemory() && otherI.getOperand(0) == I->getOperand(0)) {
                          uniqueAssignment = false;
                          break;
                      }
                  }
                  if (!uniqueAssignment) {
                      break;
                  }
              }

              // Check if the parent basic block dominates all the blocks in the loop that use the variable assigned
              bool dominatesAllUses = true;
              for (User *U : I->users()) {
                  if (Instruction *userInst = dyn_cast<Instruction>(U)) {
                      BasicBlock *userBB = userInst->getParent();
                      if (L->contains(userBB) && !DT->dominates(I->getParent(), userBB)) {
                          dominatesAllUses = false;
                          break;
                      }
                  }
              }

              // If all conditions are met, mark the instruction as a candidate for code motion
              if (dominatesLoopExits && uniqueAssignment && dominatesAllUses) {
                  canMoveMap[I] = true;
              }
          }
      }
      bool Changed = false;

      for (auto const& canMove : canMoveMap) {
          if (canMove.second && !MovedMap[canMove.first]) {
              // Move candidate to preheader if all the invariant operations it depends upon have been moved
              Instruction *I = canMove.first;
              bool allOperandsMoved = true;
              for (Use &U : I->operands()) {
                  Instruction *operandInst = dyn_cast<Instruction>(U.get());
                  if (operandInst && L->contains(operandInst->getParent())) {
                      if (canMoveMap[operandInst]) {
                          // move operand inst first
                          operandInst->moveBefore(preheader->getTerminator());
                          Changed = true;
                          MovedMap[operandInst] = true;
                      } else {
                          allOperandsMoved = false;
                          break;
                      }
                  }
              }
              if (allOperandsMoved) {
                  I->moveBefore(preheader->getTerminator());
                  Changed = true;
                  MovedMap[I] = true;
              }
          }
      }
      return Changed;
    }

    bool isInvariant(Instruction *const I, LoopInfo *LI, std::map<Instruction*, bool> map) {
        bool IsInvariant = true;
        if (!isa<Constant>(I)) {
            // mark invariant if all the definitions of operands is outside of loop
            // or if all operands are already marked as invariant
            for (Use &U : I->operands()) {
                Value *operand = U.get();
                Instruction *inst = dyn_cast<Instruction>(operand);
                if (inst) {
                    if (map.count(inst)) {
                        IsInvariant = IsInvariant && map[inst];
                    } else {
                        //NOTE: If the loop is not nullptr, the instruction is inside a loop
                        IsInvariant = IsInvariant && LI->getLoopFor(I->getParent()) != nullptr;
                    }
                }
            }
        }
// your implementation goes here
        return isSafeToSpeculativelyExecute(I)
               && !I->mayReadFromMemory()
               && !isa<LandingPadInst>(I)
               && IsInvariant;
    }
};

char LoopInvariantCodeMotion::ID = 0;
RegisterPass<LoopInvariantCodeMotion> X("loop-invariant-code-motion",
                                        "Loop Invariant Code Motion");

} // anonymous namespace
