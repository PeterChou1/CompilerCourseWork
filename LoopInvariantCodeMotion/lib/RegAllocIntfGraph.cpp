/**
 * @file Interference Graph Register Allocator
 */
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/CodeGen/CalcSpillWeights.h>
#include <llvm/CodeGen/LiveIntervals.h>
#include <llvm/CodeGen/LiveRangeEdit.h>
#include <llvm/CodeGen/LiveRegMatrix.h>
#include <llvm/CodeGen/LiveStacks.h>
#include <llvm/CodeGen/MachineBlockFrequencyInfo.h>
#include <llvm/CodeGen/MachineDominators.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/MachineLoopInfo.h>
#include <llvm/CodeGen/RegAllocRegistry.h>
#include <llvm/CodeGen/RegisterClassInfo.h>
#include <llvm/CodeGen/Spiller.h>
#include <llvm/CodeGen/TargetRegisterInfo.h>
#include <llvm/CodeGen/VirtRegMap.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <cmath>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;

namespace llvm {

void initializeRAIntfGraphPass(PassRegistry &Registry);

} // namespace llvm

namespace std {

template <> //
struct hash<Register> {
  size_t operator()(const Register &Reg) const {
    return DenseMapInfo<Register>::getHashValue(Reg);
  }
};

template <> //
struct greater<LiveInterval *> {
  bool operator()(LiveInterval *const &LHS, LiveInterval *const &RHS) const {
    return LHS->weight() > RHS->weight();
  }
};

} // namespace std

namespace {

class RAIntfGraph;

class AllocationHints {
private:
  SmallVector<MCPhysReg, 16> Hints;

public:
  AllocationHints(RAIntfGraph *const RA, const LiveInterval *const LI);
  SmallVectorImpl<MCPhysReg>::iterator begin() { return Hints.begin(); }
  SmallVectorImpl<MCPhysReg>::iterator end() { return Hints.end(); }
};

class RAIntfGraph final : public MachineFunctionPass,
                          private LiveRangeEdit::Delegate {
private:
  MachineFunction *MF;

  SlotIndexes *SI;
  VirtRegMap *VRM;
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;
  RegisterClassInfo RCI;
  LiveRegMatrix *LRM;
  MachineLoopInfo *MLI;
  LiveIntervals *LIS;

  /**
   * @brief Interference Graph
   */
  class IntfGraph {
  private:
    RAIntfGraph *RA;

    /// Interference Relations
    std::multimap<LiveInterval *, std::unordered_set<Register>,
                  std::greater<LiveInterval *>>
        IntfRels;

    /**
     * @brief  Try to materialize all the virtual registers (internal).
     *
     * @return (nullptr, VirtPhysRegMap) in the case when a successful
     *         materialization is made, (LI, *) in the case when unsuccessful
     *         (and LI is the live interval to spill)
     *
     * @sa tryMaterializeAll
     */
    using MaterializeResult_t =
        std::tuple<LiveInterval *,
                   std::unordered_map<LiveInterval *, MCPhysReg>>;
    MaterializeResult_t tryMaterializeAllInternal();

  public:
    explicit IntfGraph(RAIntfGraph *const RA) : RA(RA) {}
    /**
     * @brief Insert a virtual register @c Reg into the interference graph.
     */
    void insert(const Register &Reg);
    /**
     * @brief Erase a virtual register @c Reg from the interference graph.
     *
     * @sa RAIntfGraph::LRE_CanEraseVirtReg
     */
    void erase(const Register &Reg);
    /**
     * @brief Build the whole graph.
     */
    void build();
    /**
     * @brief Try to materialize all the virtual registers.
     */
    void tryMaterializeAll();
    void clear() { IntfRels.clear(); }
  } G;

  SmallPtrSet<MachineInstr *, 32> DeadRemats;
  std::unique_ptr<Spiller> SpillerInst;

  void postOptimization() {
    SpillerInst->postOptimization();
    for (MachineInstr *const DeadInst : DeadRemats) {
      LIS->RemoveMachineInstrFromMaps(*DeadInst);
      DeadInst->eraseFromParent();
    }
    DeadRemats.clear();
    G.clear();
  }

  friend class AllocationHints;
  friend class IntfGraph;

  /// The following two methods are inherited from @c LiveRangeEdit::Delegate
  /// and implicitly used by the spiller to edit the live ranges.
  bool LRE_CanEraseVirtReg(Register Reg) override {
    /**
     * @todo(cscd70) Please implement this method.
     */
    // If the virtual register has been materialized, undo its physical
    // assignment and erase it from the interference graph.
    LRM->unassign(LIS->getInterval(Reg));
    G.erase(Reg);
    return true;
  }
  void LRE_WillShrinkVirtReg(Register Reg) override {
    /**
     * @todo(cscd70) Please implement this method.
     */
    // If the virtual register has been materialized, undo its physical
    // assignment and re-insert it into the interference graph.
    LRM->unassign(LIS->getInterval(Reg));
    G.insert(Reg);
  }

public:
  static char ID;

  StringRef getPassName() const override {
    return "Interference Graph Register Allocator";
  }

  RAIntfGraph() : MachineFunctionPass(ID), G(this) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.setPreservesCFG();
#define REQUIRE_AND_PRESERVE_PASS(PassName)                                    \
  AU.addRequired<PassName>();                                                  \
  AU.addPreserved<PassName>()

    REQUIRE_AND_PRESERVE_PASS(SlotIndexes);
    REQUIRE_AND_PRESERVE_PASS(VirtRegMap);
    REQUIRE_AND_PRESERVE_PASS(LiveIntervals);
    REQUIRE_AND_PRESERVE_PASS(LiveRegMatrix);
    REQUIRE_AND_PRESERVE_PASS(LiveStacks);
    REQUIRE_AND_PRESERVE_PASS(AAResultsWrapperPass);
    REQUIRE_AND_PRESERVE_PASS(MachineDominatorTree);
    REQUIRE_AND_PRESERVE_PASS(MachineLoopInfo);
    REQUIRE_AND_PRESERVE_PASS(MachineBlockFrequencyInfo);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }
  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
}; // class RAIntfGraph

AllocationHints::AllocationHints(RAIntfGraph *const RA,
                                 const LiveInterval *const LI) {
  const TargetRegisterClass *const RC = RA->MRI->getRegClass(LI->reg());
  ArrayRef<MCPhysReg> Order = RA->RCI.getOrder(RA->MF->getRegInfo().getRegClass(LI->reg()));
  bool IsHardHint = RA->TRI->getRegAllocationHints(LI->reg(), Order, Hints, *RA->MF, RA->VRM, RA->LRM);
  if (!IsHardHint) {
      for (const MCPhysReg &PhysReg : Order) {
          Hints.push_back(PhysReg);
      }
  }
  outs() << "Hint Registers for Class " << RA->TRI->getRegClassName(RC) << ": [";
  for (const MCPhysReg &PhysReg : Hints) {
    outs() << RA->TRI->getRegAsmName(PhysReg) << ", ";
  }
  outs() << "]\n";
}

bool RAIntfGraph::runOnMachineFunction(MachineFunction &MF) {
  SI = &getAnalysis<SlotIndexes>();
  this->MF = &MF;
  VRM = &getAnalysis<VirtRegMap>();
  TRI = &VRM->getTargetRegInfo();
  MRI = &VRM->getRegInfo();
  MRI->freezeReservedRegs(MF);
  // Live Interval Used
  LIS = &getAnalysis<LiveIntervals>();
  LRM = &getAnalysis<LiveRegMatrix>();
  RCI.runOnMachineFunction(MF);
  MLI = &getAnalysis<MachineLoopInfo>();
  VirtRegAuxInfo VRAI(MF, *LIS, *VRM, getAnalysis<MachineLoopInfo>(),
                      getAnalysis<MachineBlockFrequencyInfo>());
  VRAI.calculateSpillWeightsAndHints();
  SpillerInst.reset(createInlineSpiller(*this, MF, *VRM, VRAI));
  G.build();
  G.tryMaterializeAll();
  postOptimization();
  return true;
}

void RAIntfGraph::IntfGraph::insert(const Register &Reg) {
    LiveInterval* LI = &RA->LIS->getInterval(Reg);
    std::unordered_set<Register> IntfRegs;
    // 1. Collect all VIRTUAL registers that interfere with 'Reg'.
    for (unsigned VirtualRegIdx = 0; VirtualRegIdx < RA->MRI->getNumVirtRegs(); ++VirtualRegIdx) {

        Register OtherReg = Register::index2VirtReg(VirtualRegIdx);
        if (RA->MRI->reg_nodbg_empty(OtherReg) || OtherReg == Reg) {
            continue;
        }
        // check if two virtual register interferes with one another if they overlap or are alias of each other
        LiveInterval* LIOther = &RA->LIS->getInterval(OtherReg);
        bool overlaps = LI->overlaps(LIOther->beginIndex(), LIOther->endIndex());
        bool alias = RA->TRI->regsOverlap(Reg, OtherReg);
        if (overlaps || alias) {
            IntfRegs.insert(OtherReg);
        }
    }
    // 2. Collect all PHYSICAL registers that interfere with 'Reg'.
    ArrayRef<MCPhysReg> Order = RA->RCI.getOrder(RA->MF->getRegInfo().getRegClass(LI->reg()));
    for (const MCPhysReg &PhysReg : Order) {
        switch (RA->LRM->checkInterference(*LI, PhysReg)) {
            case LiveRegMatrix::IK_VirtReg:
                IntfRegs.insert(PhysReg);
                break;
            case LiveRegMatrix::IK_Free:
                break;
            case LiveRegMatrix::IK_RegUnit:
                IntfRegs.insert(PhysReg);
                break;
            case LiveRegMatrix::IK_RegMask:
                IntfRegs.insert(PhysReg);
                break;
        }
    }
    // 3. Insert 'Reg' into the graph.
    IntfRels.insert({LI, IntfRegs});
}

void RAIntfGraph::IntfGraph::erase(const Register &Reg) {
  /**
   * @todo(cscd70) Please implement this method.
   */
  // 1. ∀n ∈ neighbors(Reg), erase 'Reg' from n's interfering set and update its
  //    weights accordingly.
  // 2. Erase 'Reg' from the interference graph.
  // spill the register
  SmallVector<Register, 4> SplitVirtRegs;
  LiveRangeEdit LRE(&RA->LIS->getInterval(Reg), SplitVirtRegs, *RA->MF, *RA->LIS, RA->VRM, RA, &RA->DeadRemats);
  RA->SpillerInst->spill(LRE);
  // recalculate weights after spilling
  VirtRegAuxInfo VRAI(*RA->MF, *RA->LIS, *RA->VRM, RA->getAnalysis<MachineLoopInfo>(),
                      RA->getAnalysis<MachineBlockFrequencyInfo>());
  VRAI.calculateSpillWeightsAndHints();
  RA->SpillerInst.reset(createInlineSpiller(*RA, *RA->MF, *RA->VRM, VRAI));
  // for all neightbors of n
  std::vector<Register> Neighbors;
  for (auto itr = IntfRels.find(&RA->LIS->getInterval(Reg)); itr != IntfRels.end(); itr++) {
      // iterate all neighbors of n
      std::unordered_set<Register> neighbors = itr->second;
      for (Register neighborReg : neighbors) {
          Neighbors.push_back(neighborReg);
      }
  }
  // erase register from neighbors
  for (Register neighborReg : Neighbors) {
      for (auto itr = IntfRels.find(&RA->LIS->getInterval(neighborReg)); itr != IntfRels.end(); itr++) {
          std::unordered_set<Register> neighbors = itr->second;
          neighbors.erase(Reg);
      }
  }
}

void RAIntfGraph::IntfGraph::build() {
    // build the interfence graph
    for (unsigned VirtualRegIdx = 0; VirtualRegIdx < RA->MRI->getNumVirtRegs(); ++VirtualRegIdx) {
        Register Reg = Register::index2VirtReg(VirtualRegIdx);
        if (RA->MRI->reg_nodbg_empty(Reg)) {
            continue;
        }
        insert(Reg);
    }
}

RAIntfGraph::IntfGraph::MaterializeResult_t
RAIntfGraph::IntfGraph::tryMaterializeAllInternal() {
  std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;
  std::vector<LiveInterval *> AllIntervals;
  for (auto& IntfRel: IntfRels) {
      AllIntervals.push_back(IntfRel.first);
  }
  // ∀r ∈ IntfRels.keys, try to materialize it. If successful, cache it in
  // PhysRegAssignment, else mark it as to be spilled.
  while(!AllIntervals.empty()) {
      LiveInterval* LI = AllIntervals.front();
      AllIntervals.erase(AllIntervals.begin());
      // Get candidate physical register to assign to
      AllocationHints Hints(RA, LI);
      bool isAssigned = false;
      for (const MCPhysReg &PhysReg : Hints) {
          // check if register has already assigned via PhysRegAssignment and overlaps
          bool isOccupied = false;
          for (const auto& assignment : PhysRegAssignment) {
              if ((assignment.second == PhysReg || RA->TRI->regsOverlap(PhysReg, assignment.second)) &&
                  assignment.first != LI &&
                  assignment.first->overlaps(LI->beginIndex(), LI->endIndex())) {
                  isOccupied = true;
              }
          }
          switch (RA->LRM->checkInterference(*LI, PhysReg)) {
              case LiveRegMatrix::IK_Free: {
                  if (!isOccupied) {
                      isAssigned = true;
                      PhysRegAssignment.insert({LI, PhysReg});
                  }
                  break;
              }
              case LiveRegMatrix::IK_VirtReg: {
                  break;
              }
              case LiveRegMatrix::IK_RegUnit: {
                  break;
              }
              case LiveRegMatrix::IK_RegMask: {
                  break;
              }
          }
          if (isAssigned) {
              break;
          }
      }

      if (!isAssigned) {
          // register is interfering spill it
          return std::make_tuple(LI, PhysRegAssignment);
      }

  }
  return std::make_tuple(nullptr, PhysRegAssignment);
}

void RAIntfGraph::IntfGraph::tryMaterializeAll() {
    // Keep looping until a valid assignment is made. In the case of spilling,
    // modify the interference graph accordingly.
    LiveInterval* spillLI;
    std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;
    do {
        // unassign all prev live interval
        MaterializeResult_t result = tryMaterializeAllInternal();
        spillLI = std::get<0>(result);
        PhysRegAssignment = std::get<1>(result);

        if (spillLI != nullptr) {
            // case 1: Live Interval Requires Spilling
            erase(spillLI->reg());
        } else {
            // case 2: Valid assignment found
            for (auto &PhysRegAssignPair: PhysRegAssignment) {
                RA->LRM->assign(*PhysRegAssignPair.first, PhysRegAssignPair.second);
            }
        }
    } while (spillLI != nullptr);

}
char RAIntfGraph::ID = 0;

static RegisterRegAlloc X("intfgraph", "Interference Graph Register Allocator",
                          []() -> FunctionPass * { return new RAIntfGraph(); });

} // anonymous namespace

INITIALIZE_PASS_BEGIN(RAIntfGraph, "regallointfgraph",
                      "Interference Graph Register Allocator", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(LiveRegMatrix)
INITIALIZE_PASS_DEPENDENCY(LiveStacks);
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass);
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree);
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo);
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo);
INITIALIZE_PASS_END(RAIntfGraph, "regallointfgraph",
                    "Interference Graph Register Allocator", false, false)
