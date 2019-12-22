/*
 * ActiveVariable.h - Active Varaible Detection Utilities
 * 
 * Copyright (C) 2019 William S. Moses (enzyme@wsmoses.com) - All Rights Reserved
 *
 * For commercial use of this code please contact the author(s) above.
 *
 * For research use of the code please use the following citation.
 *
 * \misc{mosesenzyme,
    author = {William S. Moses, Tim Kaler},
    title = {Enzyme: LLVM Automatic Differentiation},
    year = {2019},
    howpublished = {\url{https://github.com/wsmoses/Enzyme/}},
    note = {commit xxxxxxx}
 */
#ifndef ENZYME_ACTIVE_VAR_H
#define ENZYME_ACTIVE_VAR_H 1

#include <cstdint>

#include <llvm/Config/llvm-config.h>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

#include "llvm/Support/CommandLine.h"

extern llvm::cl::opt<bool> printconst;

enum class IntType {
    Integer,
    Float,
    Pointer,
    Unknown
};

static inline std::string to_string(IntType t) {
    switch(t) {
        case IntType::Integer: return "Integer";
        case IntType::Float: return "Float";
        case IntType::Pointer: return "Pointer";
        case IntType::Unknown: return "Unknown";
    }
    llvm_unreachable("unknown inttype");
}

llvm::Type* isKnownFloatTBAA(llvm::Instruction* inst);

IntType isIntASecretFloat(llvm::Value* val, IntType defaultType=IntType::Unknown);

//! return the secret float type of val if found, otherwise nullptr
//!   if onlyFirst is set, consider only the first element of the pointer val (e.g. if we have {int, double, double}, consider only the int)
//    onlyFirst should typically be used for store operands, whereas not used for memcpy
llvm::Type* isIntPointerASecretFloat(llvm::Value* val, bool onlyFirst);

bool isconstantValueM(llvm::Value* val, llvm::SmallPtrSetImpl<llvm::Value*> &constants, llvm::SmallPtrSetImpl<llvm::Value*> &nonconstant, llvm::SmallPtrSetImpl<llvm::Value*> &constantvals, llvm::SmallPtrSetImpl<llvm::Value*> &retvals, const llvm::SmallPtrSetImpl<llvm::Instruction*> &originalInstructions, uint8_t directions=3);

// TODO separate if the instruction is constant (i.e. could change things)
//    from if the value is constant (the value is something that could be differentiated)
bool isconstantM(llvm::Instruction* inst, llvm::SmallPtrSetImpl<llvm::Value*> &constants, llvm::SmallPtrSetImpl<llvm::Value*> &nonconstant, llvm::SmallPtrSetImpl<llvm::Value*> &constantvals, llvm::SmallPtrSetImpl<llvm::Value*> &retvals, const llvm::SmallPtrSetImpl<llvm::Instruction*> &originalInstructions, uint8_t directions=3);

#endif
