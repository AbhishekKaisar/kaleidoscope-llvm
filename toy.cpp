#include <cstdio>
#include <cstdlib>
#include <string>
#include <cctype>
#include <iostream>
#include <map>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"

enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,
  tok_var = -6,

  // primary
  tok_identifier = -4,
  tok_number = -5
};

static std::string IdentifierStr;
static double NumVal;

static std::map<std::string, llvm::Value *> NamedValues;

static std::map<char, int> BinopPrecedence;


static std::unique_ptr<llvm::LLVMContext> TheContext;
static std::unique_ptr<llvm::IRBuilder<>> Builder;
static std::unique_ptr<llvm::Module> TheModule;


// This class represents the "prototype" for a function.
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args)
      : Name(Name), Args(std::move(Args)) {}

  const std::string &getName() const { return Name; }

  llvm::Function *codegen() {
    std::vector<llvm::Type *> Doubles(Args.size(), llvm::Type::getDoubleTy(*TheContext));
    llvm::FunctionType *FT =
        llvm::FunctionType::get(llvm::Type::getDoubleTy(*TheContext), Doubles, false);
    llvm::Function *F =
        llvm::Function::Create(FT, llvm::Function::ExternalLinkage, Name, TheModule.get());

    unsigned Idx = 0;
    for (auto &Arg : F->args())
      Arg.setName(Args[Idx++]);

    return F;
  }
};

// Function template for error logging, with specializations.

template<typename T>
T LogError(const char *Str);

// Specialization for std::unique_ptr<PrototypeAST>
template<>
std::unique_ptr<PrototypeAST> LogError<std::unique_ptr<PrototypeAST>>(const char *Str) {
  std::cerr << "Error: " << Str << "\n";
  return nullptr;
}

// Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual llvm::Value *codegen() = 0;
};

// Dummy derived class to allow returning a unique_ptr<ExprAST>
class DummyExprAST : public ExprAST {
public:
  llvm::Value *codegen() override {
    return nullptr;
  }
};

template<>
llvm::Value *LogError<llvm::Value *>(const char *Str) {
  std::cerr << "Error: " << Str << "\n";
  return nullptr;
}

template<>
std::unique_ptr<ExprAST> LogError<std::unique_ptr<ExprAST>>(const char *Str) {
  std::cerr << "Error: " << Str << "\n";
  return std::make_unique<DummyExprAST>();
}

// Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  llvm::Value *codegen();
};

// Expression class for referencing a variable, like "a".

class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}
  llvm::Value *codegen();
};

// Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  llvm::Value *codegen() override {
    llvm::Value *L = LHS.get()->codegen();
    llvm::Value *R = RHS.get()->codegen();
    if (!L || !R)
      return nullptr;

    switch (Op) {
      case '+':
        return Builder->CreateFAdd(L, R, "addtmp");
      case '-':
        return Builder->CreateFSub(L, R, "subtmp");
      case '*':
        return Builder->CreateFMul(L, R, "multmp");
      case '<':
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        return Builder->CreateUIToFP(L, llvm::Type::getDoubleTy(*TheContext), "booltmp");
      default:
        return LogError<llvm::Value*>("invalid binary operator");
    }
  }
};

// Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<std::unique_ptr<ExprAST>> Args;

public:
  CallExprAST(const std::string &Callee,
              std::vector<std::unique_ptr<ExprAST>> Args)
      : Callee(Callee), Args(std::move(Args)) {}
  llvm::Value *codegen() override {
    llvm::Function *CalleeF = TheModule->getFunction(Callee);
    if (!CalleeF)
      return LogError<llvm::Value*>("Unknown function referenced");

    if (CalleeF->arg_size() != Args.size())
      return LogError<llvm::Value*>("Incorrect # arguments passed");

    std::vector<llvm::Value *> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
      llvm::Value *ArgVal = Args[i].get()->codegen();
      if (!ArgVal)
        return nullptr;
      ArgsV.push_back(ArgVal);
    }

    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
  }
};

// Expression class for variable declaration (var ... in ...)
class VarExprAST : public ExprAST {
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;
  std::unique_ptr<ExprAST> Body;

public:
  VarExprAST(std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames,
             std::unique_ptr<ExprAST> Body)
      : VarNames(std::move(VarNames)), Body(std::move(Body)) {}

  llvm::Value *codegen() override;
};

// Implementation of VarExprAST::codegen
llvm::Value *VarExprAST::codegen() {
  std::vector<llvm::Value *> OldBindings;

  llvm::Function *TheFunction = Builder->GetInsertBlock()->getParent();

  for (auto &Var : VarNames) {
    const std::string &VarName = Var.first;
    llvm::Value *InitVal;
    if (Var.second) {
      InitVal = Var.second->codegen();
      if (!InitVal)
        return nullptr;
    } else {
      InitVal = llvm::ConstantFP::get(*TheContext, llvm::APFloat(0.0));
    }

    llvm::AllocaInst *Alloca = Builder->CreateAlloca(llvm::Type::getDoubleTy(*TheContext), nullptr, VarName);
    Builder->CreateStore(InitVal, Alloca);

    OldBindings.push_back(NamedValues[VarName]);
    NamedValues[VarName] = Alloca;
  }

  llvm::Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  int i = 0;
  for (auto &Var : VarNames)
    NamedValues[Var.first] = OldBindings[i++];

  return BodyVal;
}

// This class represents a function definition.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}

  llvm::Function *codegen() {
    llvm::Function *TheFunction = TheModule->getFunction(Proto->getName());
    if (!TheFunction)
      TheFunction = Proto->codegen();
    if (!TheFunction)
      return nullptr;

    llvm::BasicBlock *BB = llvm::BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    NamedValues.clear();
    for (auto &Arg : TheFunction->args())
      NamedValues[std::string(Arg.getName())] = &Arg;

    if (llvm::Value *RetVal = Body->codegen()) {
      Builder->CreateRet(RetVal);
      llvm::verifyFunction(*TheFunction);
      return TheFunction;
    }

    TheFunction->eraseFromParent();
    return nullptr;
  }
};

int gettok() {
  static int LastChar = ' ';

  // Skip whitespace
  while (isspace(LastChar))
    LastChar = getchar();

  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def") return tok_def;
    if (IdentifierStr == "extern") return tok_extern;
    if (IdentifierStr == "var") return tok_var;
    return tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  if (LastChar == '#') {
    do LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF) return gettok();
  }

  if (LastChar == EOF) return tok_eof;

  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}

// === PARSER BASICS ===
static int CurTok;
int getNextToken() { return CurTok = gettok(); }

int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}


// Forward declaration for ParseExpression, used in ParseParenExpr and elsewhere.
std::unique_ptr<ExprAST> ParseExpression();

std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number
  return std::move(Result);
}

std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat '('
  auto V = ParseExpression();
  if (!V) return nullptr;

  if (CurTok != ')')
    return LogError<std::unique_ptr<ExprAST>>("expected ')'");
  getNextToken(); // eat ')'
  return V;
}

std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;

  getNextToken(); // eat identifier

  if (CurTok != '(')
    return std::make_unique<VariableExprAST>(IdName);

  // Function call
  getNextToken(); // eat '('
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression())
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')') break;

      if (CurTok != ',') return LogError<std::unique_ptr<ExprAST>>("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  getNextToken(); // eat ')'
  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

std::unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat 'var'

  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

  if (CurTok != tok_identifier)
    return LogError<std::unique_ptr<ExprAST>>("expected identifier after var");

  while (true) {
    std::string Name = IdentifierStr;
    getNextToken(); // eat identifier

    std::unique_ptr<ExprAST> Init = nullptr;
    if (CurTok == '=') {
      getNextToken(); // eat '='
      Init = ParseExpression();
      if (!Init)
        return nullptr;
    }

    VarNames.push_back(std::make_pair(Name, std::move(Init)));

    if (CurTok != ',')
      break;
    getNextToken(); // eat ','

    if (CurTok != tok_identifier)
      return LogError<std::unique_ptr<ExprAST>>("expected identifier after ','");
  }

  if (IdentifierStr != "in")
    return LogError<std::unique_ptr<ExprAST>>("expected 'in' after variable declaration");

  getNextToken(); // eat 'in'

  auto Body = ParseExpression();
  if (!Body)
    return nullptr;

  return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
    case tok_identifier: return ParseIdentifierExpr();
    case tok_number:     return ParseNumberExpr();
    case '(':            return ParseParenExpr();
    case tok_var:        return ParseVarExpr();
    default:             return LogError<std::unique_ptr<ExprAST>>("unknown token when expecting an expression");
  }
}

std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;
    getNextToken(); // eat binary operator

    auto RHS = ParsePrimary();
    if (!RHS)
      return nullptr;

    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

std::unique_ptr<PrototypeAST> ParsePrototype() {
  if (CurTok != tok_identifier)
    return LogError<std::unique_ptr<PrototypeAST>>("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken(); // eat function name

  if (CurTok != '(')
    return LogError<std::unique_ptr<PrototypeAST>>("Expected '(' in prototype");

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);

  if (CurTok != ')')
    return LogError<std::unique_ptr<PrototypeAST>>("Expected ')' in prototype");

  getNextToken(); // eat ')'
  return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat 'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat 'extern'
  return ParsePrototype();
}

std::unique_ptr<PrototypeAST> ParsePrototype();
std::unique_ptr<FunctionAST> ParseDefinition();

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen() {
  llvm::Value *V = NamedValues[Name];
  if (!V)
    return LogError<llvm::Value*>("Unknown variable name");
  return Builder->CreateLoad(llvm::Type::getDoubleTy(*TheContext), V, Name.c_str());
}

void HandleTopLevelExpression() {
  if (auto ExprAST = ParseExpression()) {
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
    auto FnAST = std::make_unique<FunctionAST>(std::move(Proto), std::move(ExprAST));
    if (auto *FnIR = FnAST->codegen()) {
      std::cout << "Read top-level expression:\n";
      FnIR->print(llvm::errs());
      std::cerr << "\n";
    }
  } else {
    getNextToken(); // skip token for error recovery
  }
}

int main() {
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("my cool jit", *TheContext);
  Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);

  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;

  getNextToken();

  while (true) {
    std::cout << "Ready> ";
    switch (CurTok) {
      case tok_eof:
        return 0;
      case ';':
        getNextToken();
        break;
      case tok_def:
        if (auto FnAST = ParseDefinition()) {
          if (auto *FnIR = FnAST->codegen()) {
            std::cout << "Read function definition:\n";
            FnIR->print(llvm::errs());
            std::cerr << "\n";
          }
        } else {
          getNextToken();
        }
        break;
      case tok_extern:
        if (auto ProtoAST = ParseExtern()) {
          if (auto *IR = ProtoAST->codegen()) {
            std::cout << "Read extern:\n";
            IR->print(llvm::errs());
            std::cerr << "\n";
          }
        } else {
          getNextToken();
        }
        break;
      default:
        HandleTopLevelExpression();
        break;
    }
  }
}