#include "LocalOpts.h"
#include <llvm/IR/IRBuilder.h>

using namespace llvm;

PreservedAnalyses MultiInstOptPass::run(Function &F,
                                        FunctionAnalysisManager &) {
    std::vector<Instruction *> instruction2remove;

    for (auto &BB: F) {
        for (auto &I: BB) {
            Instruction *instr = &I;
            if (instr != nullptr && isa<BinaryOperator>(instr)){
                BinaryOperator *binOp = dyn_cast<BinaryOperator>(instr);
                // Case 0: (+/-)
                //     sub case: a = b + t, c = a - t => c = b
                //     sub case: a = t + b, c = a - b => c = t
                if (binOp->getOpcode() == Instruction::Add) {
                    Value *op0 = binOp->getOperand(0);
                    Value *op1 = binOp->getOperand(1);
                    // iterate through all uses
                    for (auto &U : binOp->uses()) {
                        User *user = U.getUser();
                        if (isa<BinaryOperator>(user)) {
                            BinaryOperator *usebinOp = dyn_cast<BinaryOperator>(user);
                            if (usebinOp->getOpcode() == Instruction::Sub) {
                                // test if operand 1 is op0 or op1
                                Value *op1use = usebinOp->getOperand(1);
                                if (op1use == op1) {
                                    usebinOp->replaceAllUsesWith(op0);
                                    instruction2remove.push_back(usebinOp);
                                } else if (op1use == op0) {
                                    usebinOp->replaceAllUsesWith(op1);
                                    instruction2remove.push_back(usebinOp);
                                }
                            }
                        }
                    }
                }
                // Case 0: (-/+)
                //     sub case: a = b - t, c = a + t => c = b
                //     sub case: a = b - t, c = t + a => c = b
                //     sub case: a = t - b, c = a + b => c = t
                //     sub case: a = t - b, c = b + a => c = t
                else if (binOp->getOpcode() == Instruction::Sub) {
                    Value *op0 = binOp->getOperand(0);
                    Value *op1 = binOp->getOperand(1);
                    // iterate through all uses
                    for (auto &U : binOp->uses()) {
                        User *user = U.getUser();
                        if (isa<BinaryOperator>(user)) {
                            BinaryOperator *usebinOp = dyn_cast<BinaryOperator>(user);
                            if (usebinOp->getOpcode() == Instruction::Add) {
                                Value *otherOp = usebinOp->getOperand(U.getOperandNo() == 0 ? 1 : 0);
                                if (otherOp == op1) {
                                    usebinOp->replaceAllUsesWith(op0);
                                    instruction2remove.push_back(usebinOp);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (auto *I: instruction2remove) {
        // make extra sure there are zero uses
        if (I->hasNUses(0)) {
            I->eraseFromParent();
        }
    }
  return PreservedAnalyses::none();
}
