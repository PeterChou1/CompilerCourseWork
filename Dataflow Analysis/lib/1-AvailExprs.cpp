#include "DFA.h"
#include <iostream>
#include <llvm/IR/IRBuilder.h>

using namespace llvm;

AnalysisKey AvailExprs::Key;

bool AvailExprs::transferFunc(const Instruction &Inst, const DomainVal_t &IDV,
                              DomainVal_t &ODV) {
    // A instruction generates expression 𝑥 ⊕ 𝑦 if it definitely evaluates 𝑥 ⊕ 𝑦
    DomainVal_t gen(DomainVector.size());
    const Instruction *instr = &Inst;
    if (instr != nullptr && isa<BinaryOperator>(instr)) {
        const BinaryOperator *binOp = dyn_cast<BinaryOperator>(instr);
        dfa::Expression expr = dfa::Expression(*binOp);
        auto it = std::find(DomainVector.begin(), DomainVector.end(), expr);
        if (it != DomainVector.end()) {
            size_t index = std::distance(DomainVector.begin(), it);
            gen[index] = {.Value = true};
        }
    }
    // For the available expressions dataflow analysis we say that a block
    // kills expression 𝑥 ⊕ 𝑦 if it assigns (or may assign) 𝑥 or 𝑦
    DomainVal_t killset = IDV;
    for (size_t DomainId = 0; DomainId < DomainVector.size(); ++DomainId) {
        if (!static_cast<bool>(IDV[DomainId])) {
            continue;
        }
        // kill all Input domain value that contains current instruction
        if (DomainVector[DomainId].contain(instr)) {
            killset[DomainId] = {.Value = false};
        }
    }
    // gen𝐵 ∪ (𝑥 − kill𝐵)
    for (size_t DomainId = 0; DomainId < ODV.size(); ++DomainId) {
        ODV[DomainId] = gen[DomainId] | killset[DomainId];
    }
    DomainVal_t prevOutput = InstDomainValMap.at(instr);
    // compare previous and current output
    for (size_t DomainId = 0; DomainId < prevOutput.size(); ++DomainId) {
        if (prevOutput[DomainId].Value != ODV[DomainId].Value) {
            return true;
        }
    }
    return false;
}

