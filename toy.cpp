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

  // primary
  tok_identifier = -4,
  tok_number = -5
};

static std::string IdentifierStr;
static double NumVal;

static std::map<std::string, llvm::Value *> NamedValues;


static std::unique_ptr<llvm::LLVMContext> TheContext;
static std::unique_ptr<llvm::IRBuilder<>> Builder;
static std::unique_ptr<llvm::Module> TheModule;



// Function template for error logging, with specializations.
template<typename T>
T LogError(const char *Str);

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

// Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual llvm::Value *codegen() = 0;
};

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

// This class represents the "prototype" for a function.
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;

public:
  PrototypeAST(const std::string &Name, std::vector<std::string> Args)
      : Name(Name), Args(std::move(Args)) {}

  const std::string &getName() const { return Name; }
};

// This class represents a function definition.
class FunctionAST {
  std::unique_ptr<PrototypeAST> Proto;
  std::unique_ptr<ExprAST> Body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> Proto,
              std::unique_ptr<ExprAST> Body)
      : Proto(std::move(Proto)), Body(std::move(Body)) {}
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

std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
    case tok_identifier: return ParseIdentifierExpr();
    case tok_number:     return ParseNumberExpr();
    case '(':            return ParseParenExpr();
    default:             return LogError<std::unique_ptr<ExprAST>>("unknown token when expecting an expression");
  }
}

std::unique_ptr<ExprAST> ParseExpression() {
  return ParsePrimary();
}

llvm::Value *NumberExprAST::codegen() {
  return llvm::ConstantFP::get(*TheContext, llvm::APFloat(Val));
}

llvm::Value *VariableExprAST::codegen() {
  llvm::Value *V = NamedValues[Name];
  if (!V)
    return LogError<llvm::Value*>("Unknown variable name");
  return V;
}

int main() {
  TheContext = std::make_unique<llvm::LLVMContext>();
  TheModule = std::make_unique<llvm::Module>("my cool jit", *TheContext);
  Builder = std::make_unique<llvm::IRBuilder<>>(*TheContext);

  std::cout << "Ready> ";
  getNextToken();

  while (true) {
    std::cout << "Ready> ";
    if (CurTok == tok_eof) break;
    if (CurTok == ';') {
      getNextToken();
      continue;
    }
    if (auto IR = ParseExpression()->codegen()) {
      IR->print(llvm::errs());
      std::cerr << "\n";
    }
  }
  return 0;
}