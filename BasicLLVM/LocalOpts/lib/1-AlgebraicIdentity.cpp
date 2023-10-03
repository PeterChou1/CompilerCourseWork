#include "LocalOpts.h"
#include <llvm/IR/IRBuilder.h>

using namespace llvm;


PreservedAnalyses AlgebraicIdentityPass::run(Function &F,
                                             FunctionAnalysisManager &) {

    std::vector<Instruction*> instruction2remove;

    for (auto &BB : F) {
        for (auto &I : BB) {
            Instruction* instr = &I;
            if (instr != nullptr && isa<BinaryOperator>(instr)) {
                BinaryOperator *binOp = dyn_cast<BinaryOperator>(instr);
                Value *op0 = binOp->getOperand(0);
                Value *op1 = binOp->getOperand(1);

                if (binOp->getOpcode() == Instruction::Add || binOp->getOpcode() == Instruction::Sub) {
                    // Case 0: x = x + 0 => x || x = x - 0 => x
                    if (op1 != nullptr && isa<ConstantInt>(op1)) {
                        ConstantInt *op1c = dyn_cast<ConstantInt>(op1);
                        if (op1c->isZero()) {
                            I.replaceAllUsesWith(op0);
                            instruction2remove.push_back(instr);
                        }
                    }
                    // Case 1: x = 0 + x => x || x = 0 - x => x
                    else if (op0 != nullptr && isa<ConstantInt>(op0)) {
                        ConstantInt *op0c = dyn_cast<ConstantInt>(op0);
                        if (op0c->isZero()) {
                            I.replaceAllUsesWith(op1);
                            instruction2remove.push_back(instr);
                        }
                    }
                } else if (binOp->getOpcode() == Instruction::Mul) {
                    // Case 2: x = 1 * x => x
                    if (op1 != nullptr && isa<ConstantInt>(op1)) {
                        ConstantInt *op1c = dyn_cast<ConstantInt>(op1);
                        if (op1c->isOne()) {
                            I.replaceAllUsesWith(op0);
                            instruction2remove.push_back(instr);
                        }
                    }
                    // Case 3: x = 1 * x => x
                    else if (op0 != nullptr && isa<ConstantInt>(op0)) {
                        ConstantInt *op0c = dyn_cast<ConstantInt>(op0);
                        if (op0c->isOne()) {
                            I.replaceAllUsesWith(op1);
                            instruction2remove.push_back(instr);
                        }
                    }
                } else if (binOp->getOpcode() == Instruction::SDiv) {
                    // Case 4: x = x / 1 => x
                    if (op1 != nullptr && isa<ConstantInt>(op1)) {
                        ConstantInt *op1c = dyn_cast<ConstantInt>(op1);
                        if (op1c->isOne()) {
                            I.replaceAllUsesWith(op0);
                            instruction2remove.push_back(instr);
                        }
                    }
                }
            }
        }
    }
    //NOTE: attempting to call eraseFromParent in the middle of above loop led to all sorts of seg faults
    for (auto *I : instruction2remove) {
        // make extra sure there are zero uses
        if (I->hasNUses(0)) {
            I->eraseFromParent();
        }
    }
  return PreservedAnalyses::none();
}
