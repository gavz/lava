#include <sstream>
#include <string>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory TransformationCategory("Example Transformation");

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
public:
    MyASTVisitor(Rewriter &rewriter) : rewriter(rewriter)  {}

    bool VisitFunctionDecl(FunctionDecl *f) {
        if (f->hasBody()) {
            DeclarationName n = f->getNameInfo().getName();
            
            std::stringstream query;
            query << "// Check if arguments of " << n.getAsString() << " are tainted\n";
            
            for (auto it = f->param_begin(); it != f->param_end(); ++it) {
                query << "vm_query_buffer(";
                ParmVarDecl *param = *it;
                query << "&" << param->getNameAsString() << ", ";
                query << "sizeof(" << param->getNameAsString() << ")";
                query << ", 0);\n";
            }


            CompoundStmt *funcBody;
            if (!(funcBody = dyn_cast<CompoundStmt>(f->getBody())))
                    return true;

            SourceLocation loc = (*funcBody->body_begin())->getLocStart();
            rewriter.InsertText(loc, query.str(), true, true);
        }
  
        return true;
    }


private:
    Rewriter &rewriter;
};


class MyASTConsumer : public ASTConsumer {
public:
    MyASTConsumer(Rewriter &rewriter) : visitor(rewriter) {}

    bool HandleTopLevelDecl(DeclGroupRef DR) override {
    // iterates through decls
        for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
            // for debug
            //(*b)->dump();
            visitor.TraverseDecl(*b);
        }
        return true;
    }

private:
    MyASTVisitor visitor;
};


class MyFrontendAction : public ASTFrontendAction {
public:
    MyFrontendAction() {}
  
    void EndSourceFileAction() override {
        SourceManager &sm = rewriter.getSourceMgr();
        llvm::errs() << "** EndSourceFileAction for: "
                     << sm.getFileEntryForID(sm.getMainFileID())->getName() << "\n";

        // Last thing: include the right file
        rewriter.InsertText(sm.getLocForStartOfFile(sm.getMainFileID()), "#include \"pirate_mark.h\"\n", true, true);

        rewriter.getEditBuffer(sm.getMainFileID()).write(llvm::outs());
    }

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                     StringRef file) override {
        rewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        llvm::errs() << "** Creating AST consumer for: " << file << "\n";
        return llvm::make_unique<MyASTConsumer>(rewriter);
    }

private:
    Rewriter rewriter;
};


int main(int argc, const char **argv) {
    CommonOptionsParser op(argc, argv, TransformationCategory);
  
    ClangTool Tool(op.getCompilations(), op.getSourcePathList());
    
    return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}
