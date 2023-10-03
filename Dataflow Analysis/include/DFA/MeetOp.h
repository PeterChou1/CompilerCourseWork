#pragma once // NOLINT(llvm-header-guard)

#include <vector>

namespace dfa {

    template<typename TValue>
    struct MeetOpBase {
        using DomainVal_t = std::vector<TValue>;

        /// @brief Apply the meet operator using two operands.
        /// @param LHS
        /// @param RHS
        /// @return
        virtual DomainVal_t operator()(const DomainVal_t &LHS,
                                       const DomainVal_t &RHS) const = 0;

        /// @brief Return a domain value that represents the top element, used when
        ///        doing the initialization.
        /// @param DomainSize
        /// @return
        virtual DomainVal_t top(const std::size_t DomainSize) const = 0;
    };

    template<typename TValue>
    struct Intersect final : MeetOpBase<TValue> {
        using DomainVal_t = typename MeetOpBase<TValue>::DomainVal_t;

        DomainVal_t operator()(const DomainVal_t &LHS,
                               const DomainVal_t &RHS) const final {
            /// get the intersect of the two results by & both sets
            using llvm::outs;
            DomainVal_t result;
            for (int i = 0; i < LHS.size(); i++) {
                result.push_back(LHS[i] & RHS[i]);
            }
            return result;
        }

        DomainVal_t top(const std::size_t DomainSize) const final {
            /// the top of intersect operator is just the empty set with all false
            DomainVal_t EmptySet(DomainSize);
            return EmptySet;
        }
    };

/// @todo(CSCD70) Please add another subclass for the Union meet operator.
    template<typename TValue>
    struct Union final : MeetOpBase<TValue> {
        using DomainVal_t = typename MeetOpBase<TValue>::DomainVal_t;

        DomainVal_t operator()(const DomainVal_t &LHS,
                               const DomainVal_t &RHS) const final {
            /// get the intersect of the two results by & both sets
            DomainVal_t result;
            for (size_t i = 0; i < LHS.size(); i++) {
                result.push_back(LHS[i] | RHS[i]);
            }
            return result;
        }

        DomainVal_t top(const std::size_t DomainSize) const final {
            /// the top of union operator is the entire domain
            DomainVal_t Domain(DomainSize);
            for (size_t i = 0; i < DomainSize; i++) {
                Domain[i] = TValue::top();
            }
            return Domain;
        }
    };
} // namespace dfa
