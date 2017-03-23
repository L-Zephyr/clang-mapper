#include <iostream>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include "llvm/Option/OptTable.h"
#include "clang/Driver/Options.h"
#include "llvm/Support/FileSystem.h"
#include "CallGraphAction.h"
#include <sstream>
#include <limits.h>
#include <stdlib.h>
#include "Commons.h"

using namespace std;
using namespace clang::tooling;
using namespace clang::ast_matchers;
using namespace llvm;

static llvm::cl::OptionCategory MyToolCategory("clang-mapper option");
static std::unique_ptr<opt::OptTable> Options(clang::driver::createDriverOptTable());

static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
//static cl::extrahelp MoreHelp("\n\n");

static cl::opt<bool>
        DotOnly("dot-only", cl::desc("Generate dot file only"), cl::cat(MyToolCategory));
static cl::opt<bool>
        GraphOnly("graph-only", cl::desc("Generate graph file only"), cl::cat(MyToolCategory));
static cl::opt<bool>
        DotAndGraph("dot-graph", cl::desc("Generate both dot and graph file"), cl::cat(MyToolCategory));
static cl::opt<bool>
        IgnoreHeader("ignore-header", cl::desc("Ignore header file in the directory"), cl::cat(MyToolCategory));

/// Specification `newFrontendActionFactory`
template <>
inline std::unique_ptr<FrontendActionFactory> clang::tooling::newFrontendActionFactory(
        clang::CallGraphAction *ConsumerFactory, SourceFileCallbacks *Callbacks) {

    class FrontendActionFactoryAdapter : public FrontendActionFactory {
    public:
        explicit FrontendActionFactoryAdapter(clang::CallGraphAction *ConsumerFactory,
                                              SourceFileCallbacks *Callbacks)
                : ConsumerFactory(ConsumerFactory), Callbacks(Callbacks) {}

        clang::FrontendAction *create() override {
            return new ConsumerFactoryAdaptor(ConsumerFactory, Callbacks);
        }

    private:
        class ConsumerFactoryAdaptor : public clang::ASTFrontendAction {
        public:
            ConsumerFactoryAdaptor(clang::CallGraphAction *ConsumerFactory,
                                   SourceFileCallbacks *Callbacks)
                    : ConsumerFactory(ConsumerFactory), Callbacks(Callbacks) {}

            std::unique_ptr<clang::ASTConsumer>
            CreateASTConsumer(clang::CompilerInstance &CI, StringRef InFile) override {
                return ConsumerFactory->newASTConsumer(CI, InFile);
            }

        protected:
            bool BeginSourceFileAction(CompilerInstance &CI,
                                       StringRef Filename) override {
                if (!clang::ASTFrontendAction::BeginSourceFileAction(CI, Filename))
                    return false;
                if (Callbacks)
                    return Callbacks->handleBeginSource(CI, Filename);
                return true;
            }
            void EndSourceFileAction() override {
                if (Callbacks)
                    Callbacks->handleEndSource();
                clang::ASTFrontendAction::EndSourceFileAction();
            }

        private:
            clang::CallGraphAction *ConsumerFactory;
            SourceFileCallbacks *Callbacks;
        };
        clang::CallGraphAction *ConsumerFactory;
        SourceFileCallbacks *Callbacks;
    };

    return std::unique_ptr<FrontendActionFactory>(
            new FrontendActionFactoryAdapter(ConsumerFactory, Callbacks));
}

/// Convert related path to absolute path
std::string getAbsolutePath(std::string relatedPath) {
    char cStr[PATH_MAX];
    realpath(relatedPath.c_str(), cStr); // todo: 错误处理
    return string(cStr);
}

/// Return if filePath a code file(.h/.m/.c/.cpp)
bool isCodeFile(string filePath, bool ignoreHeader) {
    if (filePath.find(".h") == (filePath.length() - 2) && !ignoreHeader) {
        return true;
    } else if (filePath.find(".m") == (filePath.length() - 2)) {
        return true;
    } else if (filePath.find(".c") == (filePath.length() - 2)) {
        return true;
    } else if (filePath.find(".cpp") == (filePath.length() - 4)) {
        return true;
    }
    return false;
}

/// Get all .h/.m/.cpp/.c files in rootDir
void getFilesInDir(string rootDir, vector<string> &files, bool ignoreHeader) {
    if (sys::fs::exists(rootDir)) {
        std::error_code code;
        sys::fs::recursive_directory_iterator end;
        for (auto it = sys::fs::recursive_directory_iterator(rootDir, code); it != end; it.increment(code)) {
            if (code) {
                llvm::errs() << "Error: " << code.message() << "\n";
                break;
            }

            string path = (*it).path();
            if (isCodeFile(path, ignoreHeader)) {
                files.push_back(getAbsolutePath(path));
            }
        }

    } else {
        llvm::errs() << "Directory " << rootDir << " not exists!\n";
    }
}

int main(int argc, const char **argv) {
    // recursive get all files in directory path arg
    bool ignoreHeader = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp("-ignore-header", argv[i]) == 0) {
            ignoreHeader = true;
        }
    }

    vector<string> commands;
    string outputRootPath = "./";
    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        if (sys::fs::is_directory(arg)) {
            outputRootPath = string(arg);
            getFilesInDir(arg, commands, ignoreHeader);
        } else if (sys::fs::is_regular_file(arg)) {
            string fullPath = string(argv[i]);
            commands.push_back(getAbsolutePath(fullPath));
        } else {
            commands.push_back(string(argv[i]));
        }
    }

    // convert commmands to `const char **`
    const char **argList = new const char*[commands.size()];
    for (vector<string>::size_type i = 0; i < commands.size(); ++i) {
        argList[i] = commands[i].c_str();
    }
    argc = commands.size();

    CommonOptionsParser OptionsParser(argc, argList, MyToolCategory);
    ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

    clang::CallGraphAction action;
    action.setBasePath(getAbsolutePath(outputRootPath));
    if (DotOnly) {
        action.setOption(O_DotOnly);
    } else if (DotAndGraph) {
        action.setOption(O_DotAndGraph);
    } else {
        action.setOption(O_GraphOnly);
    }

    Tool.run(newFrontendActionFactory(&action).get());
    return 0;
}
