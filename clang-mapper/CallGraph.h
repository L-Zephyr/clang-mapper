//
// Created by LZephyr on 2017/3/15.
//

#ifndef LIBTOOLING_CALLGRAPHBUILDER_H
#define LIBTOOLING_CALLGRAPHBUILDER_H

#include "clang/AST/DeclBase.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SetVector.h"
#include <iostream>
#include <algorithm>
#include "CallGraphAction.h"
#include "Commons.h"

using namespace std;

namespace clang {
    class CallGraphNode;
    class CallGraphAction;

    class CallGraph : public RecursiveASTVisitor<CallGraph> {
        friend class CallGraphNode;
        typedef llvm::DenseMap<const Decl *, std::shared_ptr<CallGraphNode>>
                RootsMapType;

        ASTContext &Context;
        std::string FullPath; // full path for this code file
        std::string BasePath;
        CallGraphOption Option;

        /// owns all caller node
        RootsMapType Roots;

    public:
        CallGraph(ASTContext &context, std::string filePath, std::string basePath);

        ~CallGraph();

        /// \brief Populate the call graph with the functions in the given
        /// declaration.
        ///
        /// Recursively walks the declaration to find all the dependent Decls as well.
        void addToCallGraph(Decl *D) {
            TraverseDecl(D);
        }

        void setOption(CallGraphOption option) {
            this->Option = option;
        }

        /// \brief Determine if a declaration should be included in the graph.
        static bool canIncludeInGraph(const Decl *D);

        /// \brief Determine if a decl can be a caller node in the graph
        static bool canBeCallerInGraph(const Decl *D);

        /// \brief Lookup the node for the given declaration.
        CallGraphNode *getNode(Decl *);

        /// \brief Lookup the node for the given declaration. If none found, insert
        /// one into the graph.
        CallGraphNode *getOrInsertNode(Decl *);

//        void insertNode(Decl *);

        /// Iterators through all the elements in the graph. Note, this gives
        /// non-deterministic order.
        typedef RootsMapType::iterator iterator;
        typedef RootsMapType::const_iterator const_iterator;

        iterator begin() { return Roots.begin(); }
        iterator end() { return Roots.end(); }
        const_iterator begin() const { return Roots.begin(); }
        const_iterator end() const { return Roots.end(); }

        /// \brief Get the number of root nodes in the graph.
        unsigned size() const { return Roots.size(); }

        /// Iterators through all the nodes of the graph that have no parent. These
        /// are the unreachable nodes, which are either unused or are due to us
        /// failing to add a call edge due to the analysis imprecision.
        typedef llvm::SetVector<CallGraphNode *>::iterator nodes_iterator;
        typedef llvm::SetVector<CallGraphNode *>::const_iterator const_nodes_iterator;

        std::string getFullPath() const {
            return FullPath;
        }

        void print(raw_ostream &os) const;
        void dump() const;
        void output() const;
        bool generateGraphFile(std::string dotFile) const;

        /// Part of recursive declaration visitation. We recursively visit all the
        /// declarations to collect the root functions.
        bool VisitFunctionDecl(FunctionDecl *FD) {
            if (isInSystem(FD)) {
                return true;
            }

            // We skip function template definitions, as their semantics is
            // only determined when they are instantiated.
            if (canBeCallerInGraph(FD) && FD->isThisDeclarationADefinition()) {
                addRootNode(FD);
            }
            return true;
        }

        /// Part of recursive declaration visitation.
        bool VisitObjCMethodDecl(ObjCMethodDecl *MD) {
            if (isInSystem(MD)) {
                return true;
            }

            if (canBeCallerInGraph(MD)) {
                addRootNode(MD);
            }
            return true;
        }

        // We are only collecting the declarations, so do not step into the bodies.
        bool TraverseStmt(Stmt *S) {
            return true;
        }

        bool shouldWalkTypesOfTypeLocs() const {
            return false;
        }

        bool isInSystem(Decl *decl) {
            if (Context.getSourceManager().isInSystemHeader(decl->getLocation()) ||
                Context.getSourceManager().isInExternCSystemHeader(decl->getLocation())) {
                return true;
            }
            return false;
        }

    private:
        /// Add a root node to call graph
        void addRootNode(Decl *decl);
    };

    class CallGraphNode {
    public:
        typedef CallGraphNode* CallRecord;

    private:
        /// \brief The function/method declaration.
        Decl *FD;

        /// \brief The list of functions called from this node.
        SmallVector<CallRecord, 5> CalledFunctions;

    public:
        CallGraphNode(Decl *D) : FD(D) {}

        typedef SmallVectorImpl<CallRecord>::iterator iterator;
        typedef SmallVectorImpl<CallRecord>::const_iterator const_iterator;

        /// Iterators through all the callees/children of the node.
        inline iterator begin() { return CalledFunctions.begin(); }
        inline iterator end()   { return CalledFunctions.end(); }
        inline const_iterator begin() const { return CalledFunctions.begin(); }
        inline const_iterator end()   const { return CalledFunctions.end(); }

        bool operator==(const CallGraphNode &node) {
            return this->getDecl() == node.getDecl();
        }
        bool operator!=(const CallGraphNode &node) {
            return this->getDecl() != node.getDecl();
        }

        inline bool empty() const {return CalledFunctions.empty(); }
        inline unsigned size() const {return CalledFunctions.size(); }

        void addCallee(CallGraphNode *N) {
            if (std::find(CalledFunctions.begin(), CalledFunctions.end(), N) != CalledFunctions.end()) {
                return;
            }
            CalledFunctions.push_back(N);
        }

        Decl *getDecl() const { return FD; }

        void print(raw_ostream &os) const;
        void dump() const;

        std::string getNameAsString() const;
    };

} // end clang namespace

// Graph traits for iteration, viewing.
namespace llvm {
    template <> struct GraphTraits<clang::CallGraphNode*> {
        typedef clang::CallGraphNode NodeType;
        typedef clang::CallGraphNode *NodeRef;
        typedef NodeType::iterator ChildIteratorType;

        static NodeType *getEntryNode(clang::CallGraphNode *CGN) { return CGN; }
        static inline ChildIteratorType child_begin(NodeType *N) { return N->begin(); }
        static inline ChildIteratorType child_end(NodeType *N) { return N->end(); }
    };

    template <> struct GraphTraits<const clang::CallGraphNode*> {
        typedef const clang::CallGraphNode NodeType;
        typedef const clang::CallGraphNode *NodeRef;
        typedef NodeType::const_iterator ChildIteratorType;

        static NodeType *getEntryNode(const clang::CallGraphNode *CGN) { return CGN; }
        static inline ChildIteratorType child_begin(NodeType *N) { return N->begin();}
        static inline ChildIteratorType child_end(NodeType *N) { return N->end(); }
    };

    template <> struct GraphTraits<clang::CallGraph*>
            : public GraphTraits<clang::CallGraphNode*> {
        static clang::CallGraphNode *
        CGGetValue(clang::CallGraph::const_iterator::value_type &P) {
            return P.second.get();
        }

        // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
        typedef mapped_iterator<clang::CallGraph::iterator, decltype(&CGGetValue)>
                nodes_iterator;

        static nodes_iterator nodes_begin (clang::CallGraph *CG) {
            return nodes_iterator(CG->begin(), &CGGetValue);
        }
        static nodes_iterator nodes_end (clang::CallGraph *CG) {
            return nodes_iterator(CG->end(), &CGGetValue);
        }

        static unsigned size(clang::CallGraph *CG) {
            return CG->size();
        }
    };

    template <> struct GraphTraits<const clang::CallGraph*> :
            public GraphTraits<const clang::CallGraphNode*> {
        static clang::CallGraphNode *
        CGGetValue(clang::CallGraph::const_iterator::value_type &P) {
            return P.second.get();
        }

        // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
        typedef mapped_iterator<clang::CallGraph::const_iterator,
                decltype(&CGGetValue)>
                nodes_iterator;

        static nodes_iterator nodes_begin(const clang::CallGraph *CG) {
            return nodes_iterator(CG->begin(), &CGGetValue);
        }

        static nodes_iterator nodes_end(const clang::CallGraph *CG) {
            return nodes_iterator(CG->end(), &CGGetValue);
        }

        static unsigned size(const clang::CallGraph *CG) {
            return CG->size();
        }
    };

} // end llvm namespace

#endif //LIBTOOLING_CALLGRAPHBUILDER_H
