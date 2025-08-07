# Kaleidoscope LLVM Compiler Frontend

This project is a minimal compiler frontend for the **Kaleidoscope** programming language, built using the **LLVM (Low-Level Virtual Machine)** infrastructure. It was developed as part of an academic assignment to understand the inner workings of compilers, from lexical analysis to code generation.

---

## 📘 Assignment Background

This implementation was completed for the course on compiler design and programming language concepts under the guidance of:

**Dr. Md. Shahriar Karim (MSK1)**  
Associate Professor  
Department of Electrical and Computer Engineering  
North South University (NSU)

> Developed by **Abhishek Kaisar Abhoy**, undergraduate student at NSU. ID: 2221140042

The goal was to follow the official LLVM tutorial and demonstrate practical understanding by implementing essential compiler components step-by-step.

---

## 🔍 What This Project Covers

Following the [LLVM Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html), this project includes:

- ✅ Lexical analysis (lexer)
- ✅ Syntax analysis (parser)
- ✅ AST (Abstract Syntax Tree) construction
- ✅ LLVM IR code generation
- ✅ Interactive REPL
- ✅ `var` expression support with scoping

---

## 🧪 Sample REPL Usage

```llvm
Ready> def add(a b) a + b;
Ready> add(4, 5);
; returns 9

Ready> var x = 5, y = x + 2 in x * y;
; returns 35
```

---

## ⚙️ Build & Run Instructions

### ✅ Prerequisites

- CMake ≥ 3.10
- LLVM (e.g., installed via `brew install llvm`)
- C++17 compiler (e.g., clang++)

### 🔧 Build Steps

```bash
# Clone the repo
git clone https://github.com/AbhishekKaisar/kaleidoscope-llvm.git
cd kaleidoscope-llvm

# Generate build files
cmake -B build -S .

# Build the compiler
cmake --build build

# Run the interactive REPL
./build/toy
```

---

## 💡 Features Implemented (Chapters 1–5)

| Feature                         | Implemented |
|----------------------------------|-------------|
| Number parsing                  | ✅ Yes       |
| Binary expressions              | ✅ Yes       |
| Function definitions & calls    | ✅ Yes       |
| Variables with `var` keyword    | ✅ Yes       |
| LLVM IR generation              | ✅ Yes       |
| Interactive REPL                | ✅ Yes       |

---

## 🧠 Concepts Explored

- **Lexical Analysis**: Tokenizing source code into meaningful symbols
- **Recursive Descent Parsing**: Building the syntax tree from tokens
- **AST Design**: Structuring program constructs in memory
- **LLVM IR**: Generating low-level intermediate representation from high-level syntax
- **REPL**: Creating an interactive read-eval-print loop

---

## 📁 Project Structure

```
├── toy.cpp           # Main implementation file
├── CMakeLists.txt    # CMake build config
└── README.md         # This file
```

---

## 📚 License

This project is intended for **educational purposes only** and is based on the official LLVM tutorial. No license is applied for redistribution or commercial use.

---

## 🙋‍♂️ Author

**Abhishek Kaisar Abhoy**  
📧 abhishekkaisar2015@gmail.com  
🔗 GitHub: [@AbhishekKaisar](https://github.com/AbhishekKaisar)  
🎓 North South University (NSU)