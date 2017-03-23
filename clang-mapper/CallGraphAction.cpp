//
// Created by LZephyr on 2017/2/27.
//

#include "CallGraphAction.h"
#include "CallGraph.h"

void CallGraphConsumer::HandleTranslationUnit(clang::ASTContext &Context) {
    visitor->addToCallGraph(Context.getTranslationUnitDecl());
    visitor->dump();
    visitor->output();
}

CallGraphConsumer::CallGraphConsumer(CompilerInstance &CI, std::string filename, std::string basePath, CallGraphOption option) {
        this->visitor = new CallGraph(CI.getASTContext(), filename, basePath);
        this->visitor->setOption(option);
}

std::unique_ptr<clang::ASTConsumer> CallGraphAction::CreateASTConsumer(
        clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    Compiler.getDiagnostics().setClient(new IgnoringDiagConsumer());
    return std::unique_ptr<clang::ASTConsumer>(new CallGraphConsumer(Compiler, InFile, BasePath, option));
}

std::unique_ptr<ASTConsumer> CallGraphAction::newASTConsumer(clang::CompilerInstance &CI, StringRef InFile) {
    llvm::errs() << "Scan " << InFile << "\n";
    CI.getDiagnostics().setClient(new IgnoringDiagConsumer());
    return llvm::make_unique<CallGraphConsumer>(CI, InFile, BasePath, option);
}
