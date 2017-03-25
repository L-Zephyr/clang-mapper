//
// Created by LZephyr on 2017/2/27.
//

#ifndef LIBTOOLING_CALLGRAPHACTION_H
#define LIBTOOLING_CALLGRAPHACTION_H

#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/ASTConsumers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ASTConsumer.h>
#include <llvm/Support/Casting.h>
#include <iostream>
#include "Commons.h"

using namespace clang;
using namespace std;

namespace clang {
    class CallGraph;
    class CallGraphAction;
    class CallGraphConsumer;

    class CallGraphConsumer : public clang::ASTConsumer {
    public:
        explicit CallGraphConsumer(CompilerInstance &CI, std::string filename, std::string basePath, CallGraphOption option);
        virtual void HandleTranslationUnit(clang::ASTContext &Context);
    private:
        CallGraph *visitor;
    };

    class CallGraphAction : public clang::ASTFrontendAction {
    private:
        CallGraphOption option;
        std::string BasePath;
    public:
        virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
                clang::CompilerInstance &Compiler, llvm::StringRef InFile);

        void setOption(CallGraphOption option) {
            this->option = option;
        }

        void setBasePath(std::string basePath) {
            this->BasePath = basePath;
        }

        std::unique_ptr<ASTConsumer> newASTConsumer(clang::CompilerInstance &CI, StringRef InFile);
    };
}

#endif //LIBTOOLING_CALLGRAPHACTION_H
