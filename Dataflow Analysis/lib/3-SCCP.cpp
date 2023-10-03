#include "DFA.h"
#include <iostream>
#include <llvm/IR/IRBuilder.h>
using namespace llvm;

AnalysisKey SCCP::Key;

bool SCCP::transferFunc(const Instruction &Inst, const DomainVal_t &IDV,
                        DomainVal_t &ODV) {
    const Instruction *instr = &Inst;
    if (InstExecutable[instr]) {
        ODV = IDV;
        dfa::Lattice currentLattice = dfa::Lattice();
        dfa::Variable var = dfa::Variable(instr);
        auto it = std::find(DomainVector.begin(), DomainVector.end(), var);
        if (it != DomainVector.end()) {
            size_t index = std::distance(DomainVector.begin(), it);
            currentLattice = ODV[index];
        }
        std::vector<dfa::Lattice> useLattice;
        std::vector<ConstantInt *> cVector;
        std::vector<dfa::Variable> vVector;
        // check operands of instr are constant
        for (auto Iter = instr->op_begin(); Iter != instr->op_end(); ++Iter) {
            Value *V = *Iter;
            if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
                dfa::Lattice use;
                use.markConstant(CI);
                useLattice.push_back(use);
                cVector.push_back(CI);

            } else if (isa<Instruction>(V) || isa<Argument>(V)) {
                dfa::Variable var = dfa::Variable(V);
                auto it = std::find(DomainVector.begin(), DomainVector.end(), var);
                if (it != DomainVector.end()) {
                    vVector.push_back(var);
                    size_t index = std::distance(DomainVector.begin(), it);
                    useLattice.push_back(IDV[index]);
                }
            }
        }
        bool allconstant = true;
        for (dfa::Lattice l: useLattice) {
            allconstant = allconstant && l.isConstant();
        }

        if (isa<PHINode>(instr)) {
            const llvm::PHINode *phi  = dyn_cast<PHINode>(instr);
            for (unsigned int i = 0; i < phi->getNumIncomingValues(); i++) {
                Value *V = phi->getIncomingValue(i);
                if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
                    dfa::Lattice use;
                    use.markConstant(CI);
                    currentLattice = currentLattice & use;
                } else {
                    BasicBlock* bb = phi->getIncomingBlock(i);
                    dfa::Variable phiVar = dfa::Variable(V);
                    auto it = std::find(DomainVector.begin(), DomainVector.end(), phiVar);
                    if (it != DomainVector.end()) {
                        size_t index = std::distance(DomainVector.begin(), it);
                        currentLattice = currentLattice & InstDomainValMap[&bb->back()][index];
                    }
                }
            }
        }
        else if (instr->getOpcode() == llvm::Instruction::Add && allconstant && useLattice.size() >= 2) {
            // if instruction is add instruction and all use constant evaluate and set currentLattice
            // also assumes all constant are int???
            llvm::APInt resultValue = useLattice[0].Constant->getValue() + useLattice[1].Constant->getValue();
            auto newCurrentLattice = dfa::Lattice();
            llvm::LLVMContext& context =  Inst.getParent()->getContext();
            newCurrentLattice.markConstant(llvm::ConstantInt::get(context, resultValue));
            currentLattice = currentLattice & newCurrentLattice;
        } else if (instr->getOpcode() == llvm::Instruction::Add) {
            for (dfa::Lattice l: useLattice) {
                currentLattice = currentLattice & l;
            }
        } else if (instr->getOpcode() == llvm::Instruction::ICmp && allconstant && useLattice.size() >= 2) {
            // if instruction is a icmp instruction and use is constant evaluate to true and false
            const llvm::ICmpInst *icmp = dyn_cast<ICmpInst>(instr);
            auto constantExpr = llvm::ConstantExpr::getICmp(icmp->getPredicate(), useLattice[0].Constant, useLattice[1].Constant);
            auto newCurrentLattice = dfa::Lattice();
            llvm::LLVMContext& context =  Inst.getParent()->getContext();
            // previous constant
            newCurrentLattice.markConstant(llvm::ConstantInt::get(context, constantExpr->getUniqueInteger()));
            currentLattice = currentLattice & newCurrentLattice;
        } else if (instr->getOpcode() == llvm::Instruction::ICmp ) {
            for (dfa::Lattice l: useLattice) {
                currentLattice = currentLattice & l;
            }
        }

        if (instr->getOpcode() == llvm::Instruction::Br && useLattice.size() >= 1) {
            const llvm::BranchInst* branchInst = dyn_cast<BranchInst>(instr);
            if (useLattice[0].isConstant()) {
                bool branchTaken = useLattice[0].Constant->getUniqueInteger().getBoolValue();
                if (branchTaken) {
                    InstExecutable[&branchInst->getSuccessor(0)->front()] = true;
                } else {
                    InstExecutable[&branchInst->getSuccessor(1)->front()] = true;
                }
            } else {
                // if branch instruction mark and evaluation of conditional is bottom the
                //                                           mark both next block as executable
                for (unsigned int i = 0; i < llvm::succ_size(Inst.getParent()); ++i) {
                    InstExecutable[&branchInst->getSuccessor(i)->front()] = true;
                }
            }
        } else if (instr->getOpcode() == llvm::Instruction::Br) {
            // mark unconditional branch next
            const llvm::BranchInst* branchInst = dyn_cast<BranchInst>(instr);
            InstExecutable[&branchInst->getSuccessor(0)->front()] = true;
        } else {
            // mark next instruction as executable
            const llvm::Instruction *nextInst = Inst.getNextNode();
            if (nextInst != nullptr) {
                InstExecutable[nextInst] = true;
            }
        }
        if (it != DomainVector.end()) {
            size_t index = std::distance(DomainVector.begin(), it);
            if (ODV[index] == currentLattice) {
                return false;
            }
            ODV[index] = ODV[index] & currentLattice;
            return true;
        }
    } else {
        ODV = InstDomainValMap[instr];
    }
    return false;
}