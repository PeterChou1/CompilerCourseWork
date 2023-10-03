#include <DFA/Domain/Variable.h>

using namespace llvm;
using dfa::Variable;

raw_ostream &operator<<(raw_ostream &Outs, const Variable &Var) {
  CHECK(Var.Var != nullptr);
  Var.Var->printAsOperand(Outs);
  return Outs;
}

void Variable::Initializer::visitInstruction(Instruction &I) {
    using llvm::outs;
//    outs() << "visiting instruction: ";
//    I.print(outs(), true);
//    outs() << "\n";
    for (auto Iter = I.op_begin(); Iter != I.op_end(); ++Iter) {
        Value *V = *Iter;
        if (isa<Instruction>(V) || isa<Argument>(V)) {
            Variable newVar = Variable(V);
//            outs() << "variable used " << newVar << "\n";
            if (!std::count(DomainVector.begin(), DomainVector.end(),newVar)) {
                DomainIdMap[newVar] = DomainVector.size();
                DomainVector.push_back(newVar);
            }
        }
    }



}
