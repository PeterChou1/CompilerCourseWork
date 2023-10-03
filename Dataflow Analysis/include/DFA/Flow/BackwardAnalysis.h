#pragma once // NOLINT(llvm-header-guard)

#include "Framework.h"

/// @todo(CSCD70) Please instantiate for the backward pass, similar to the
///               forward one.
/// @sa @c ForwardAnalysis

namespace dfa {
/// @todo(CSCD70) Please modify the traversal ranges.
    typedef llvm::iterator_range<llvm::const_succ_iterator> BackwardMeetBBConstRange_t;
    typedef llvm::iterator_range<std::reverse_iterator<llvm::ilist_iterator<llvm::ilist_detail::node_options<llvm::BasicBlock, false, false, void>, false, true>>> BackwardBBConstRange_t;
    typedef llvm::iterator_range<llvm::ilist_iterator<llvm::ilist_detail::node_options<llvm::Instruction, false, false, void>, true, true>> BackwardInstConstRange_t;
    // TDomainElem -> dfa::Expression, TValue -> dfa::Bool,  TMeetOp -> dfa::Intersect<dfa::Bool>
    template<typename TDomainElem, typename TValue, typename TMeetOp>
    class BackwardAnalysis
            : public Framework<TDomainElem, TValue, TMeetOp, BackwardMeetBBConstRange_t,
                    BackwardBBConstRange_t, BackwardInstConstRange_t> {
    protected:
        using Framework_t =
                Framework<TDomainElem, TValue, TMeetOp, BackwardMeetBBConstRange_t,
                        BackwardBBConstRange_t, BackwardInstConstRange_t>;
        using typename Framework_t::AnalysisResult_t;
        using typename Framework_t::BBConstRange_t;
        using typename Framework_t::InstConstRange_t;
        using typename Framework_t::MeetBBConstRange_t;

        using Framework_t::BVs;
        using Framework_t::DomainIdMap;
        using Framework_t::DomainVector;
        using Framework_t::InstDomainValMap;

        using Framework_t::getName;
        using Framework_t::run;
        using Framework_t::stringifyDomainWithMask;

        void printInstDomainValMap(const llvm::Instruction &Inst) const final {
            using llvm::errs;
            using llvm::outs;
            const llvm::BasicBlock *const ParentBB = Inst.getParent();
            outs() << Inst << "\n";
            LOG_ANALYSIS_INFO << "\t"
                              << stringifyDomainWithMask(InstDomainValMap.at(&Inst));
            if (&Inst == &(ParentBB->back())) {
                LOG_ANALYSIS_INFO << "\t" << stringifyDomainWithMask(BVs.at(ParentBB));
                errs() << "\n";
            } // if (&Inst == &(*ParentBB->begin()))
        }

        MeetBBConstRange_t
        getMeetBBConstRange(const llvm::BasicBlock &BB) const final {
            return llvm::successors(&BB);
        }

        InstConstRange_t getInstConstRange(const llvm::BasicBlock &BB) const final {
            return llvm::reverse(BB);
        }

        BBConstRange_t getBBConstRange(const llvm::Function &F) const final {
            return llvm::reverse(F);
        }
    };

} // namespace dfa
