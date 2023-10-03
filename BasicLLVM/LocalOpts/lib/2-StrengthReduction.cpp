#include "LocalOpts.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <cmath>

using namespace llvm;

PreservedAnalyses StrengthReductionPass::run(Function &F,
                                             FunctionAnalysisManager &) {

    std::vector<Instruction *> instruction2remove;

    for (auto &BB: F) {
        for (auto &I: BB) {
            Instruction *instr = &I;
            if (instr != nullptr && isa<BinaryOperator>(instr)) {

                BinaryOperator *binOp = dyn_cast<BinaryOperator>(instr);
                IRBuilder<> builder(binOp);

                Value *op0 = binOp->getOperand(0);
                Value *op1 = binOp->getOperand(1);

                // Case 0: x = x * 4 => x << 2
                if (binOp->getOpcode() == Instruction::Mul) {
                    if (op1 != nullptr && isa<ConstantInt>(op1)) {
                        ConstantInt *op1c = dyn_cast<ConstantInt>(op1);
                        if (op1c->getBitWidth() <= 32) {
                            int powerOf2 = op1c->getSExtValue();
                            // check if operand is a power of 2
                            // algorithm: https://stackoverflow.com/questions/600293/how-to-check-if-a-number-is-a-power-of-2
                            if ((powerOf2 != 0) && ((powerOf2 & (powerOf2 - 1)) == 0)) {
                                int shiftoperator = log2(powerOf2);
                                Value *shl = builder.CreateShl(op0, shiftoperator);
                                binOp->replaceAllUsesWith(shl);
                                instruction2remove.push_back(instr);
                            }
                        }
                    }
                    // Case 1: x = 4 * x => x << 2
                    else if (op0 != nullptr && isa<ConstantInt>(op0)) {
                        ConstantInt *op0c = dyn_cast<ConstantInt>(op0);
                        if (op0c->getBitWidth() <= 32) {
                            int powerOf2 = op0c->getSExtValue();
                            if ((powerOf2 != 0) && ((powerOf2 & (powerOf2 - 1)) == 0)) {
                                int shiftoperator = log2(powerOf2);
                                Value *shl = builder.CreateShl(op0, shiftoperator);
                                binOp->replaceAllUsesWith(shl);
                                instruction2remove.push_back(instr);
                            }
                        }
                    }
                } else if (binOp->getOpcode() == Instruction::SDiv) {
                    // Case 2: x = x / 4 => x >> 2
                    if (op1 != nullptr && isa<ConstantInt>(op1)) {
                        ConstantInt *op1c = dyn_cast<ConstantInt>(op1);
                        if (op1c->getBitWidth() <= 32) {
                            int powerOf2 = op1c->getSExtValue();
                            // check if operand is a power of 2
                            // algorithm: https://stackoverflow.com/questions/600293/how-to-check-if-a-number-is-a-power-of-2
                            if ((powerOf2 != 0) && ((powerOf2 & (powerOf2 - 1)) == 0)) {
                                int shiftoperator = log2(powerOf2);
                                Value *lshr = builder.CreateLShr(op0, shiftoperator);
                                binOp->replaceAllUsesWith(lshr);
                                instruction2remove.push_back(instr);
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
