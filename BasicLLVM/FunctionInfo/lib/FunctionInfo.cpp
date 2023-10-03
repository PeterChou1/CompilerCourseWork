#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/raw_ostream.h>
#include <map>

using namespace llvm;

namespace {
    class FunctionInfoPass final : public PassInfoMixin<FunctionInfoPass> {
    public:
        PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
            struct FunctionInfo {
                bool variadic;
                size_t args;
                unsigned int callCount;
                unsigned int instCount;
                unsigned int basicBlockCount;
            };
            std::map<std::string, FunctionInfo> funcmap;

            for (Function &F: M) {
                std::string name = F.getName().str();
                unsigned int instCount = 0;
                unsigned int basicblockCount = 0;
                for (BasicBlock &B: F) {
                    instCount += std::distance(B.begin(), B.end());
                    basicblockCount += 1;
                    for (Instruction &I: B) {
                        if (isa<CallInst>(&I)) {
                            Function* fp = cast<CallInst>(I).getCalledFunction();
                            if (fp != NULL) {
                                std::string callfuncname = fp->getName().str();
                                if (funcmap.find(callfuncname) == funcmap.end()) {
                                    FunctionInfo i = {
                                            .callCount = 1
                                    };
                                    funcmap[callfuncname] = i;
                                } else {
                                    funcmap[callfuncname].callCount += 1;
                                }
                            }
                        }
                    }
                }
                if (funcmap.find(name) == funcmap.end()) {
                    FunctionInfo i = {
                            .variadic = F.isVarArg(),
                            .args = F.arg_size(),
                            .callCount = 0,
                            .instCount = instCount,
                            .basicBlockCount = basicblockCount,
                    };
                    funcmap[name] = i;
                } else {
                    funcmap[name].variadic = F.isVarArg();
                    funcmap[name].args = F.arg_size();
                    funcmap[name].instCount = instCount;
                    funcmap[name].basicBlockCount = basicblockCount;
                }
            }

            for (auto const& func : funcmap) {
                outs() << " Function Name: " << func.first << "\n";
                FunctionInfo info = func.second;
                if (info.variadic) {
                    outs() << " Number of Arguments: " << info.args << "+*" << "\n";
                } else {
                    outs() << " Number of Arguments: " << info.args << "\n";
                }
                outs() << " Number of Calls: " << info.callCount << "\n";
                outs() << " Number OF BBs: " << info.basicBlockCount << "\n";
                outs() << " Number of Instructions: " << info.instCount << "\n";
            }
            return PreservedAnalyses::all();
        }
    }; // class FunctionInfoPass

} // anonymous namespace

extern "C" PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
            .APIVersion = LLVM_PLUGIN_API_VERSION,
            .PluginName = "FunctionInfo",
            .PluginVersion = LLVM_VERSION_STRING,
            .RegisterPassBuilderCallbacks =
            [](PassBuilder &PB) {
                PB.registerPipelineParsingCallback(
                        [](StringRef Name, ModulePassManager &MPM,
                           ArrayRef<PassBuilder::PipelineElement>) -> bool {
                            if (Name == "function-info") {
                                MPM.addPass(FunctionInfoPass());
                                return true;
                            }
                            return false;
                        });
            } // RegisterPassBuilderCallbacks
    };        // struct PassPluginLibraryInfo
}
