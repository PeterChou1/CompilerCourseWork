#pragma once // NOLINT(llvm-header-guard)

#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <iostream>
#include <tuple>
#include <cxxabi.h>
#include <llvm/Analysis/ValueLattice.h>
#include "Utility.h"

namespace dfa {



    template<typename TValue>
    struct ValuePrinter {
        static std::string print(const TValue &V) { return ""; }
    };

    template<typename TDomainElem, typename TValue, typename TMeetOp,
            typename TMeetBBConstRange, typename TBBConstRange,
            typename TInstConstRange>
    class Framework {
    protected:
        // Maps Id =>
        using DomainIdMap_t = typename TDomainElem::DomainIdMap_t;
        //
        using DomainVector_t = typename TDomainElem::DomainVector_t;
        //
        using DomainVal_t = typename TMeetOp::DomainVal_t;
        //
        using Initializer = typename TDomainElem::Initializer;
        //
        using MeetOperands_t = std::vector<DomainVal_t>;
        //
        using MeetBBConstRange_t = TMeetBBConstRange;
        //
        using BBConstRange_t = TBBConstRange;
        //
        using InstConstRange_t = TInstConstRange;
        // Analysis Result is The result of the data flow analysis
        // DomainIdMap?
        //
        using AnalysisResult_t =
                std::tuple<DomainIdMap_t, DomainVector_t,
                        std::unordered_map<const llvm::BasicBlock *, DomainVal_t>,
                        std::unordered_map<const llvm::Instruction *, DomainVal_t>>;

        DomainIdMap_t DomainIdMap;
        DomainVector_t DomainVector;
        std::unordered_map<const llvm::BasicBlock *, DomainVal_t> BVs;
        std::unordered_map<const llvm::Instruction *, DomainVal_t> InstDomainValMap;
        std::unordered_map<const llvm::Instruction *, bool> InstExecutable;

        /// @name Print utility functions
        /// @{

        std::string stringifyDomainWithMask(const DomainVal_t &Mask) const {
            std::string StringBuf;
            llvm::raw_string_ostream Strout(StringBuf);
            Strout << "{";
            CHECK(Mask.size() == DomainIdMap.size() &&
                  Mask.size() == DomainVector.size())
                << "The size of mask must be equal to the size of domain, but got Masks size: "
                << Mask.size() << " Domain Id Map size: " << DomainIdMap.size() << " Domain Vector size: "
                << DomainVector.size();
            for (size_t DomainId = 0; DomainId < DomainIdMap.size(); ++DomainId) {
                if (!static_cast<bool>(Mask[DomainId])) {
                    continue;
                }
                Strout << DomainVector.at(DomainId)
                       << ValuePrinter<TValue>::print(Mask[DomainId]) << ", ";
            } // for (MaskIdx : [0, Mask.size()))
            Strout << "}";
            return StringBuf;
        }

        virtual void printInstDomainValMap(const llvm::Instruction &Inst) const = 0;

        void printInstDomainValMap(const llvm::Function &F) const {
            using llvm::errs;
            for (const llvm::Instruction &Inst: llvm::instructions(&F)) {
                printInstDomainValMap(Inst);
            }
        }

        virtual std::string getName() const = 0;

        /// @}
        /// @name Boundary values
        /// @{
        DomainVal_t getBoundaryVal(const llvm::BasicBlock &BB) const {
            using llvm::outs;
            MeetOperands_t MeetOperands = getMeetOperands(BB);
            // if bb has no meet operands then initialVal ← Boundary Condition;
            if (MeetOperands.empty()) {
                // TMeetOp meetOp;
                // return meetOp.top(DomainVector.size());
                return bc();
            }
            // initialVal ← MeetOp(MeetOperands(bb));
            return meet(MeetOperands);
        }

        /// @brief Get the list of basic blocks to which the meet operator will be
        ///        applied.
        /// @param BB
        /// @return
        virtual MeetBBConstRange_t
        getMeetBBConstRange(const llvm::BasicBlock &BB) const = 0;

        /// @brief Get the list of domain values to which the meet operator will be
        ///        applied.
        /// @param BB
        /// @return
        /// @sa @c getMeetBBConstRange
        virtual MeetOperands_t getMeetOperands(const llvm::BasicBlock &BB) const {
            MeetOperands_t Operands;
            using llvm::outs;
            /// @todo(CSCD70) Please complete this method.
            for (const llvm::BasicBlock* bb : getMeetBBConstRange(BB)) {
                const llvm::Instruction* operands;
                for (const llvm::Instruction& I : getInstConstRange(*bb)) {
                    operands = &I;
                }
                Operands.push_back(InstDomainValMap.at(operands));
            }
            return Operands;
        }

        DomainVal_t bc() const { return DomainVal_t(DomainIdMap.size()); }

        DomainVal_t meet(const MeetOperands_t &MeetOperands) const {
            /// apply meet operator repeatedly until we finish
            DomainVal_t meetVal = MeetOperands[0];
            TMeetOp meetOp;
            for (int i = 1; i < MeetOperands.size(); i++) {
                meetVal = meetOp(meetVal, MeetOperands[i]);
            }
            return meetVal;
        }

        /// @}
        /// @name CFG traversal
        /// @{

        /// @brief Get the list of basic blocks from the function.
        /// @param F
        /// @return
        virtual BBConstRange_t getBBConstRange(const llvm::Function &F) const = 0;

        /// @brief Get the list of instructions from the basic block.
        /// @param BB
        /// @return
        virtual InstConstRange_t
        getInstConstRange(const llvm::BasicBlock &BB) const = 0;

        /// @brief Traverse through the CFG of the function.
        /// @param F
        /// @return True if either BasicBlock-DomainValue mapping or
        ///         Instruction-DomainValue mapping has been modified, false
        ///         otherwise.
        bool traverseCFG(const llvm::Function &F) {
            using llvm::outs;
            bool Changed = false;
            for(const llvm::BasicBlock& bb : getBBConstRange(F)) {
                 DomainVal_t IDV = getBoundaryVal(bb);
                 // Update boundary value
                 BVs[&bb] = IDV;
                 DomainVal_t ODV(DomainVector.size());

                 // check if boundary values changed
                 for (const llvm::Instruction& I : getInstConstRange(bb)) {
                     if (transferFunc(I, IDV, ODV)) {
                         Changed = true;
                     };
                     IDV = ODV;
                     // store instruction into value map
                     InstDomainValMap[&I] = IDV;
                 }
            }
            return Changed;
        }

        /// @}

        virtual ~Framework() {}

        /// @brief Apply the transfer function to the input domain value at
        ///        instruction @p inst .
        /// @param Inst instructions
        /// @param IDV input domain value
        /// @param ODV output domain value
        /// @return Whether the output domain value is to be changed.
        virtual bool transferFunc(const llvm::Instruction &Inst,
                                  const DomainVal_t &IDV, DomainVal_t &ODV) = 0;

        virtual AnalysisResult_t run(llvm::Function &F,
                                     llvm::FunctionAnalysisManager &FAM) {
            // using llvm::outs;
            // initialize domain
            Initializer initializer(DomainIdMap, DomainVector);
            initializer.visit(F);
            TMeetOp meetOp;
            for (auto &bb : F) {
                BVs[&bb] = bc();
                for (auto &inst : bb)  {
                    InstDomainValMap[&inst] = bc();
                    // whether or not the current instruction is executable
                    // used for sparse conditional execution
                    InstExecutable[&inst] = false;
                }
            }
            // always mark the first instruction as executable
            llvm::Instruction& FirstInstr = F.front().front();
            InstExecutable[&FirstInstr] = true;

            bool Changed;
            do {
                Changed = traverseCFG(F);
            } while (Changed);
            //// debug output print
            printInstDomainValMap(F);
            return std::make_tuple(DomainIdMap, DomainVector, BVs, InstDomainValMap);
        }

    }; // class Framework

    /// @brief For each domain element type, we have to define:
    ///        - The default constructor
    ///        - The meet operators (for intersect/union)
    ///        - The top element
    ///        - Conversion to bool (for logging)
    struct Bool {
        bool Value = false;

        Bool operator&(const Bool &Other) const {
            return {.Value = Value && Other.Value};
        }

        Bool operator|(const Bool &Other) const {
            return {.Value = Value || Other.Value};
        }

        static Bool top() { return {.Value = true }; }

        explicit operator bool() const { return Value; }
    };

    /// @brief For each domain element type, we have to define:
    ///        - The default constructor
    ///        - The meet operators (for intersect/union)
    ///        - The top element
    ///        - Conversion to bool (for logging)
    struct Lattice {
        // (Top Element) => not defined
        enum LatticeElement {
            undef,
            overdef,
            constant
        };
        llvm::ConstantInt* Constant = nullptr;
        LatticeElement state = LatticeElement::undef;

        void markOverdef() {
            state = LatticeElement::overdef;
            Constant = nullptr;
        }

        void markConstant(llvm::ConstantInt* c) {
            state = LatticeElement::constant;
            Constant = c;
        }

        bool isConstant() {
            return LatticeElement::constant == state;
        }

        bool isOverdef() {
            return LatticeElement::overdef == state;
        }

        bool isUndef() {
            return LatticeElement::undef == state;
        }

        Lattice operator&(const Lattice &Other) const {
            Lattice currentCopy = *this;
            using llvm::outs;
            if (Other.state == LatticeElement::undef) {
                return currentCopy;
            } else if (Other.state == LatticeElement::overdef || state == LatticeElement::overdef) {
                currentCopy.markOverdef();
                return currentCopy;
            } else if (state == LatticeElement::undef) {
                Lattice otherCopy = Other;
                return otherCopy;
            }
            // both state should be constant
            assert(Constant != nullptr && Other.Constant != nullptr);
            assert(state == LatticeElement::constant && Other.state == LatticeElement::constant);
            llvm::Constant *cmp = llvm::ConstantExpr::getCompare(llvm::CmpInst::ICMP_EQ, Constant, Other.Constant);

            if (!cmp->isOneValue()) {
                // if value is not the same get
                currentCopy.markOverdef();
            }
            return currentCopy;
        }

        static Lattice top() { return Lattice(); }


        bool operator==(const Lattice &Other) const  {
            if (state == LatticeElement::constant && Other.state == LatticeElement::constant) {
                llvm::Constant *cmp = llvm::ConstantExpr::getCompare(llvm::CmpInst::ICMP_EQ, Constant, Other.Constant);
                return cmp->isOneValue();
            } else if (state == Other.state) {
                return true;
            }
            return false;
        }

        explicit operator bool() const { return state == LatticeElement::constant; }

    };

} // namespace dfa