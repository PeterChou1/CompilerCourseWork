#include <DFA/Domain/Expression.h>

using namespace llvm;
using dfa::Expression;

raw_ostream &operator<<(raw_ostream &Outs, const Expression &Expr) {
  Outs << "[" << Instruction::getOpcodeName(Expr.Opcode) << " ";
  Expr.LHS->printAsOperand(Outs, false);
  Outs << ", ";
  Expr.RHS->printAsOperand(Outs, false);
  Outs << "]";
  return Outs;
}

void Expression::Initializer::visitBinaryOperator(BinaryOperator &BO) {
  /// fill the domain with binary expression
  Expression newExpr = Expression(BO);
  if (!std::count(DomainVector.begin(), DomainVector.end(),newExpr)) {
      DomainIdMap[newExpr] = DomainVector.size();
      DomainVector.push_back(newExpr);
  }
}
