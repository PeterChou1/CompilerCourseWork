#include "DFA.h"
#include <iostream>
#include <llvm/IR/IRBuilder.h>

using namespace llvm;

AnalysisKey Liveness::Key;

bool Liveness::transferFunc(const Instruction &Inst, const DomainVal_t &IDV, DomainVal_t &ODV) {
    DomainVal_t useSet(DomainVector.size());
    const Instruction *instr = &Inst;

    std::vector<dfa::Variable> varUseSet;

    for (auto Iter = instr->op_begin(); Iter != instr->op_end(); ++Iter) {
        Value *V = *Iter;
        if (isa<Instruction>(V) || isa<Argument>(V)) {
            dfa::Variable var = dfa::Variable(V);
            // A variable is live at some point if it holds a value that may be needed in the future,
            // or equivalently if its value may be read before the next time the variable is written to.
            // therefore we must treat all incomingValue of a PHI node as uses
            auto it = std::find(DomainVector.begin(), DomainVector.end(), var);
            if (it != DomainVector.end()) {
                size_t index = std::distance(DomainVector.begin(), it);
                useSet[index] = {.Value = true};
                varUseSet.push_back(var);
            }
        }
    }


    DomainVal_t defSet = IDV;
    for (size_t DomainId = 0; DomainId < DomainVector.size(); ++DomainId) {
        if (!static_cast<bool>(IDV[DomainId])) {
            continue;
        }
        // kill all Input domain value that contains current instruction
        // since we kill all def values
        if (DomainVector[DomainId].contain(instr)) {
            defSet[DomainId] = {.Value = false};
        }
    }
    // union the two section
    for (size_t DomainId = 0; DomainId < ODV.size(); ++DomainId) {
        ODV[DomainId] = useSet[DomainId] | defSet[DomainId];
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

