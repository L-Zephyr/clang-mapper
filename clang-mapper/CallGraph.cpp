//
// Created by LZephyr on 2017/3/15.
//

#include "CallGraph.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/GraphWriter.h"
#include <iostream>
#include <string>
#include <sstream>

using namespace clang;
using namespace llvm;

#define DEBUG_TYPE "CallGraph"

STATISTIC(NumObjCCallEdges, "Number of Objective-C method call edges");
STATISTIC(NumBlockCallEdges, "Number of block call edges");

/// Split a string
template<typename Out>
void split(const std::string &s, char delim, Out result) {
    std::stringstream ss;
    ss.str(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        *(result++) = item;
    }
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

namespace clang {
/// A helper class, which walks the AST and locates all the call sites in the
/// given function body.
    class CGBuilder : public StmtVisitor<CGBuilder> {
        CallGraph *G;
        CallGraphNode *CallerNode;
        ASTContext &Context;
    public:
        CGBuilder(CallGraph *g, CallGraphNode *N, ASTContext &context)
                : G(g), CallerNode(N), Context(context) {}

        void VisitStmt(Stmt *S) {
            VisitChildren(S);
        }

        void VisitCallExpr(CallExpr *CE) {
            if (Decl *D = getDeclFromCall(CE)) {
                if (G->isInSystem(D)) {
                    return;
                }
                addCalledDecl(D);
            }
            VisitChildren(CE);
        }

        // Adds may-call edges for the ObjC message sends.
        void VisitObjCMessageExpr(ObjCMessageExpr *ME) {
            if (ObjCInterfaceDecl *IDecl = ME->getReceiverInterface()) {
                if (G->isInSystem(IDecl)) {
                    return;
                }

                Selector Sel = ME->getSelector();

                // Find the callee definition within the same translation unit.
                Decl *D = nullptr;
                if (ME->isInstanceMessage())
                    D = IDecl->lookupPrivateMethod(Sel);
                else
                    D = IDecl->lookupPrivateClassMethod(Sel);

                // if not found, create a ObjCMethodDecl with Selector and loc
                // todo: 自定义一个类型以区分Node是否有完整的Decl信息
                if (!D) {
                    D = ObjCMethodDecl::Create(Context,
                                               ME->getLocStart(),
                                               ME->getLocEnd(),
                                               ME->getSelector(),
                                               QualType(),
                                               nullptr,
                                               nullptr);
                }
                addCalledDecl(D);
                NumObjCCallEdges++;
            }
        }

        // get callee Decl
        Decl *getDeclFromCall(CallExpr *CE) {
            if (FunctionDecl *CalleeDecl = CE->getDirectCallee())
                return CalleeDecl;

            // Simple detection of a call through a block.
            Expr *CEE = CE->getCallee()->IgnoreParenImpCasts();
            if (BlockExpr *Block = dyn_cast<BlockExpr>(CEE)) {
                NumBlockCallEdges++;
                return Block->getBlockDecl();
            }

            return nullptr;
        }

        // add a callee node to caller node
        void addCalledDecl(Decl *D) {
            if (G->canIncludeInGraph(D)) {
                CallGraphNode *CalleeNode = G->getOrInsertNode(D);
                CallerNode->addCallee(CalleeNode);
            }
        }

        void VisitChildren(Stmt *S) {
            for (Stmt *SubStmt : S->children())
                if (SubStmt)
                    this->Visit(SubStmt);
        }
    };

} // end clang namespace

CallGraph::CallGraph(ASTContext &context, std::string filePath, std::string basePath):
        Context(context), FullPath(filePath), BasePath(basePath) {
}

CallGraph::~CallGraph() {}

bool CallGraph::canIncludeInGraph(const Decl *D) {
    assert(D);

    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
        // We skip function template definitions, as their semantics is
        // only determined when they are instantiated.
        if (FD->isDependentContext())
            return false;

        IdentifierInfo *II = FD->getIdentifier();
        if (II && II->getName().startswith("__inline"))
            return false;
    }

    return true;
}

bool CallGraph::canBeCallerInGraph(const Decl *D) {
    assert(D);
    // ignore empty Decl
    if (!D->hasBody())
        return false;

    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
        // We skip function template definitions, as their semantics is
        // only determined when they are instantiated.
        if (FD->isDependentContext())
            return false;

        IdentifierInfo *II = FD->getIdentifier();
        if (II && II->getName().startswith("__inline"))
            return false;
    }

    return true;
}

//void CallGraph::addRootNodesForBlocks(DeclContext *D) {
//    if (BlockDecl *BD = dyn_cast<BlockDecl>(D))
//        addRootNodeForDecl(BD, true);
//
//    for (auto *I : D->decls())
//        if (auto *DC = dyn_cast<DeclContext>(I))
//            addRootNodesForBlocks(DC);
//}
//
//void CallGraph::addRootNodeForDecl(Decl* D, bool IsGlobal) {
//    assert(D);
//
//    // Allocate a new node, mark it as root, and process it's calls.
//    CallGraphNode *Node = getOrInsertNode(D);
//
//    // Process all the calls by this function as well.
//    CGBuilder builder(this, Node, this->context);
//    if (Stmt *Body = D->getBody())
//        builder.Visit(Body);
//}

void CallGraph::addRootNode(Decl *decl) {
    if (decl && !isa<ObjCMethodDecl>(decl)) {
        decl = decl->getCanonicalDecl();
    }

    CallGraphNode *Node = getOrInsertNode(decl);

    // Process all the calls by this function as well.
    CGBuilder builder(this, Node, Context);
    if (Stmt *Body = decl->getBody())
        builder.Visit(Body);
}

CallGraphNode *CallGraph::getNode(Decl *D) {
    iterator it = Roots.find(D);
    if (it == Roots.end()) {
        return nullptr;
    }
    return it->second.get();
}

CallGraphNode *CallGraph::getOrInsertNode(Decl *decl) {
    if (decl && !isa<ObjCMethodDecl>(decl)) {
        decl = decl->getCanonicalDecl();
    }
    shared_ptr<CallGraphNode> &Node = Roots[decl];
    if (!Node) {
        Node = shared_ptr<CallGraphNode>(new CallGraphNode(decl));
    }
    return Node.get();
}

void CallGraph::print(raw_ostream &OS) const {
    OS << " --- Call graph Dump --- \n";

    // traversal root nodes
    for (const_iterator it = this->begin(); it != Roots.end(); ++it) {
        CallGraphNode *node = it->second.get();
        OS << "  Function: ";
        node->print(OS);
        OS << " calls: ";

        // traversal called node
        for (CallGraphNode::const_iterator CI = node->begin(),
                     CE = node->end(); CI != CE; ++CI) {
            (*CI)->print(OS);
            OS << " ";
        }
        OS << '\n';
    }

    OS.flush();
}

LLVM_DUMP_METHOD void CallGraph::dump() const {
    print(llvm::errs());
}

void CallGraph::output() const {
    // Get related path
    vector<string> baseComponent = split(BasePath, '/');
    vector<string> fullComponent = split(FullPath, '/');
    vector<string> related;

    vector<string>::iterator BI;
    vector<string>::iterator FI;
    for (BI = baseComponent.begin(), FI = fullComponent.begin();
         BI != baseComponent.end() && FI != fullComponent.end();
         ++BI, ++FI) {
        if (*BI != *FI) {
            break;
        }
    }

    string outputPath = "./";
    while (FI + 1 != fullComponent.end()) {
        outputPath.append(*FI);
        if (!sys::fs::exists(*FI)) {
            sys::fs::create_directory(outputPath);
        }
        outputPath.append("/");
        FI++;
    }
    outputPath.append(fullComponent.back());
    outputPath.append(".dot");

    // Write .dot
    std::error_code EC;
    raw_fd_ostream O(outputPath, EC, sys::fs::F_RW);

    if (EC) {
        llvm::errs() << "Error: " << EC.message() << "\n";
        return;
    }

    llvm::WriteGraph(O, this);
    if (Option != O_GraphOnly) {
        errs() << "Write to " << outputPath << "\n";
    }

    O.flush();
    if (Option == O_GraphOnly) {
        if (generateGraphFile(outputPath)) {
            sys::fs::remove(outputPath);
        } else {
            llvm::errs() << "Generate graph file fail: " << outputPath << "\n";
        }
    } else if (Option == O_DotAndGraph) {
        if (!generateGraphFile(outputPath)) {
            llvm::errs() << "Generate graph file fail: " << outputPath << "\n";
        }
    }
}

/// Generate graph file in dotFile's dir
bool CallGraph::generateGraphFile(std::string dotFile) const {
    ErrorOr<std::string> target = llvm::sys::findProgramByName("Graphviz");
    if (!target) {
        target = llvm::sys::findProgramByName("dot");
    }

    if (target) {
        std::string programPath = *target;
        std::vector<const char *> args;
        std::string graphPath = dotFile.substr(0, dotFile.length() - 3); // remove suffix 'dot'
        graphPath.append("png");

        // command line arg
        args.push_back(programPath.c_str());
        args.push_back(dotFile.c_str());
        args.push_back("-T");
        args.push_back("png");
        args.push_back("-o");
        args.push_back(graphPath.c_str());
        args.push_back(nullptr);

        std::string ErrMsg;
        if (sys::ExecuteAndWait(programPath, args.data(), nullptr, nullptr, 0, 0, &ErrMsg)) {
            errs() << "Error: " << ErrMsg << "\n";
            return false;
        } else {
            errs() << "Write to " << graphPath << "\n" << ErrMsg;
        }
    } else {
        llvm::errs() << "Graphviz not found! Please install Graphviz first\n";
        return false;
    }
    return true;
}

void CallGraphNode::print(raw_ostream &os) const {
    std::string name = getNameAsString();
    if (name.length() == 0) {
        os << "< >";
    } else {
        os << name;
    }
}

LLVM_DUMP_METHOD void CallGraphNode::dump() const {
    print(llvm::errs());
}

std::string CallGraphNode::getNameAsString() const {
    if (FunctionDecl *decl = dyn_cast_or_null<FunctionDecl>(FD)) {
        return decl->getNameAsString();
    } else if (ObjCMethodDecl *decl = dyn_cast_or_null<ObjCMethodDecl>(FD)) {
        return decl->getNameAsString();
    } else {
        return "";
    }
}

namespace llvm {
    template <>
    struct DOTGraphTraits<const CallGraph*> : public DefaultDOTGraphTraits {

        DOTGraphTraits (bool isSimple=false) : DefaultDOTGraphTraits(isSimple) {}

        static std::string getNodeLabel(const CallGraphNode *Node,
                                        const CallGraph *CG) {
            if (const NamedDecl *ND = dyn_cast_or_null<NamedDecl>(Node->getDecl()))
                return ND->getNameAsString();
            else
                return "< >";
        }

        static bool isNodeHidden(const CallGraphNode *Node) {
            return false;
        }

        static std::string getGraphName(const CallGraph *CG) {
            return split(CG->getFullPath(), '/').back();
        }
    };
}
